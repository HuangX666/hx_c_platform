#include "archive_private.h"

int hx_os_archive_available(void)
{
	return HX_OS_HAVE_ARCHIVE;
}

int hx_os_archive_7zip_available(void)
{
	return HX_OS_HAVE_7ZIP;
}

hx_os_status hx_os_archive_options_init(
	hx_os_archive_options *options, size_t capacity)
{
	const hx_os_archive_options defaults = HX_OS_ARCHIVE_OPTIONS_INIT;

	if (!options || capacity < sizeof(*options))
		return HX_OS_INVALID;
	*options = defaults;

	return HX_OS_OK;
}

hx_os_status hx_os_archive_result_init(
	hx_os_archive_result *result, size_t capacity)
{
	if (!result || capacity < sizeof(*result))
		return HX_OS_INVALID;
	memset(result, 0, sizeof(*result));
	result->size = sizeof(*result);
	result->version = 1;

	return HX_OS_OK;
}

#if HX_OS_HAVE_ARCHIVE
#include <openssl/err.h>

static void *HX_OS_CALL hx_ar_allocate(size_t bytes, void *user)
{
	hx_ar_job *job = user;
	int64_t old = hx_os_atomic_load(&job->used, HX_OS_RELAXED);
	size_t cost = hx_os_i_allocation_cost(bytes, 0);
	void *memory;
	void *locale;

	for (;;) {
		if (!cost ||
			cost > job->options.max_memory_bytes - (size_t)old) {
			hx_os_atomic_store(
				&job->budget_failed, 1, HX_OS_RELAXED);

			return NULL;
		}
		if (hx_os_atomic_compare_exchange(&job->used, &old,
			    old + (int64_t)cost, HX_OS_ACQ_REL, HX_OS_RELAXED))
			break;
	}
	locale = hx_ar_i_callback_begin(job);
	memory = hx_os_i_alloc(job->parent, bytes, 0);
	hx_ar_i_callback_end(locale);
	if (!memory)
		(void)hx_os_atomic_fetch_add(
			&job->used, -(int64_t)cost, HX_OS_RELAXED);

	return memory;
}

static void HX_OS_CALL hx_ar_deallocate(void *memory, void *user)
{
	hx_ar_job *job = user;
	size_t bytes = hx_os_i_allocation_bytes(memory);
	void *locale = hx_ar_i_callback_begin(job);

	hx_os_memory_free(memory);
	hx_ar_i_callback_end(locale);
	(void)hx_os_atomic_fetch_add(
		&job->used, -(int64_t)bytes, HX_OS_RELAXED);
}

hx_os_status hx_ar_i_fail(hx_ar_job *job, hx_os_status status,
	const hx_os_error *error, const char *message)
{
	int64_t expected = 0;

	if (status == HX_OS_OK)
		return status;
	if (hx_os_atomic_compare_exchange(
		    &job->failed, &expected, 1, HX_OS_ACQ_REL, HX_OS_RELAXED)) {
		size_t bytes = message ? strlen(message) : 0;
		size_t invalid;

		if (error)
			job->error = *error;
		else
			(void)hx_os_i_error(&job->error, status, 0, 0);
		job->error.status = status;
		if (bytes >= sizeof(job->diagnostic))
			bytes = sizeof(job->diagnostic) - 1;
		while (bytes &&
			hx_os_text_validate_utf8(message, bytes, &invalid) !=
				HX_OS_OK)
			--bytes;
		if (bytes)
			memcpy(job->diagnostic, message, bytes);
		job->diagnostic[bytes] = 0;
	}

	return status;
}

hx_os_status hx_ar_i_progress(
	hx_ar_job *job, uint64_t content, uint64_t archive_position)
{
	int limit;

	(void)hx_os_mutex_lock(job->mutex, NULL);
	limit = content > job->options.max_content_bytes -
				job->result.content_bytes ||
		archive_position > job->options.max_archive_bytes;
	if (!limit) {
		job->result.content_bytes += content;
		if (archive_position > job->result.archive_bytes)
			job->result.archive_bytes = archive_position;
	}
	(void)hx_os_mutex_unlock(job->mutex, NULL);
	if (limit)
		return hx_ar_i_fail(
			job, HX_OS_LIMIT, NULL, "Archive byte limit");

	return HX_OS_OK;
}

hx_os_status hx_ar_i_check(hx_ar_job *job, int notify)
{
	int64_t expected = 0;

	if (hx_os_atomic_load(&job->failed, HX_OS_ACQUIRE))
		return HX_OS_CANCELLED;
	if (hx_os_deadline_expired(job->options.deadline))
		return hx_ar_i_fail(
			job, HX_OS_TIMEOUT, NULL, "Archive deadline");
	if (notify && job->options.progress &&
		hx_os_atomic_compare_exchange(&job->notifying, &expected, 1,
			HX_OS_ACQ_REL, HX_OS_RELAXED)) {
		hx_os_archive_result snapshot;
		hx_os_error error = {0};
		hx_os_status status;
		void *locale;

		(void)hx_os_mutex_lock(job->mutex, NULL);
		snapshot = job->result;
		(void)hx_os_mutex_unlock(job->mutex, NULL);
		locale = hx_ar_i_callback_begin(job);
		status = job->options.progress(
			&snapshot, job->options.user, &error);
		hx_ar_i_callback_end(locale);
		hx_os_atomic_store(&job->notifying, 0, HX_OS_RELEASE);
		if (status != HX_OS_OK)
			return hx_ar_i_fail(
				job, status, &error, "Progress cancelled");
	}

	return HX_OS_OK;
}

hx_os_status hx_ar_i_begin(hx_ar_job *job, hx_os_context *context,
	const hx_os_archive_options *input, hx_os_archive_result *result,
	hx_os_error *error)
{
	hx_os_archive_options options = HX_OS_ARCHIVE_OPTIONS_INIT;
	hx_os_context_options allocator;
	hx_os_status status;
	size_t invalid;

	memset(job, 0, sizeof(*job));
	if (input) {
		if (input->size < sizeof(options) || input->version != 1)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		options = *input;
	}
	if (result && (result->size < sizeof(*result) || result->version != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!options.max_archive_bytes ||
		options.max_archive_bytes > INT64_MAX ||
		!options.max_content_bytes ||
		options.max_content_bytes > INT64_MAX ||
		!options.max_decoded_bytes ||
		options.max_decoded_bytes > INT64_MAX || !options.max_entries ||
		options.max_entries > 1000000 || !options.max_depth ||
		options.max_depth > 256 || options.max_path_bytes < 2 ||
		options.max_path_bytes > 131072 ||
		options.max_memory_bytes < HX_AR_CHUNK * 2 ||
		options.max_memory_bytes > 256 * 1024 * 1024u ||
		options.format > HX_OS_ARCHIVE_ZSTD ||
		options.preset > HX_OS_ARCHIVE_SMALL || (options.flags & ~7u) ||
		!options.workers || options.workers > 32 ||
		(options.password &&
			(!*options.password ||
				strlen(options.password) > 1024)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (options.password &&
		hx_os_text_validate_utf8(options.password,
			strlen(options.password), &invalid) != HX_OS_OK)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	job->parent = context;
	job->options = options;
	(void)hx_os_archive_result_init(&job->result, sizeof(job->result));
	if (options.diagnostic)
		hx_string_clear(options.diagnostic);
	hx_os_context_options_init(&allocator);
	allocator.allocator.allocate = hx_ar_allocate;
	allocator.allocator.deallocate = hx_ar_deallocate;
	allocator.allocator.user = job;
	status = hx_os_context_create(&allocator, &job->context, error);
	hx_string_init(&job->source_path, job->context);
	hx_string_init(&job->archive_file, job->context);
	hx_string_init(&job->destination_path, job->context);
	if (status == HX_OS_OK)
		status =
			hx_os_mutex_create(job->context, 0, &job->mutex, error);
	if (status == HX_OS_OK) {
		job->buffer = hx_os_i_alloc(job->context, HX_AR_CHUNK, 0);
		if (!job->buffer)
			status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	if (status != HX_OS_OK)
		(void)hx_ar_i_fail(
			job, status, error, "Archive working memory");

	return status;
}

hx_os_status hx_ar_i_end(hx_ar_job *job, hx_os_status status,
	hx_os_archive_result *result, hx_os_error *error)
{
	hx_ar_i_items_free(job);
	hx_ar_i_root_close(job->source_root);
	hx_ar_i_root_close(job->output_root);
	hx_ar_i_decode_close(job);
	(void)hx_os_file_close(&job->input, NULL);
	if (job->buffer)
		hx_os_secure_zero(job->buffer, HX_AR_CHUNK);
	hx_os_memory_free(job->buffer);
	hx_os_mutex_destroy(job->mutex);
	hx_string_destroy(&job->source_path);
	hx_string_destroy(&job->archive_file);
	hx_string_destroy(&job->destination_path);
	if (job->context) {
		hx_os_error cleanup;
		hx_os_status closed =
			hx_os_context_destroy(&job->context, &cleanup);

		if (status == HX_OS_OK && closed != HX_OS_OK)
			(void)hx_ar_i_fail(job, closed, &cleanup,
				"Archive resource lease");
	}
	if (hx_os_atomic_load(&job->failed, HX_OS_ACQUIRE)) {
		status = job->error.status;
		if (status == HX_OS_NO_MEMORY &&
			hx_os_atomic_load(&job->budget_failed, HX_OS_RELAXED)) {
			status = HX_OS_LIMIT;
			job->error.status = status;
		}
		if (job->options.diagnostic)
			(void)hx_string_assign_cstr(job->options.diagnostic,
				job->diagnostic[0] ? job->diagnostic
						   : hx_os_status_name(status),
				NULL);
		if (error)
			*error = job->error;
	}
	else
		(void)hx_os_i_error(error, status, 0, 0);
	if (result)
		*result = job->result;

	return status;
}

static int hx_ar_suffix(const char *path, const char *suffix)
{
	size_t bytes = strlen(path), count = strlen(suffix), i;

	if (bytes < count)
		return 0;
	path += bytes - count;
	for (i = 0; i < count; ++i) {
		unsigned char value = (unsigned char)path[i];

		if (value >= 'A' && value <= 'Z')
			value += 'a' - 'A';
		if (value != (unsigned char)suffix[i])
			return 0;
	}

	return 1;
}

uint32_t hx_ar_i_format(const char *path)
{
	static const struct {
		const char *suffix;
		uint32_t format;
	} formats[] = {{".tar.gz", HX_OS_ARCHIVE_TAR_GZIP},
		{".tgz", HX_OS_ARCHIVE_TAR_GZIP},
		{".tar.xz", HX_OS_ARCHIVE_TAR_XZ},
		{".txz", HX_OS_ARCHIVE_TAR_XZ},
		{".tar.zst", HX_OS_ARCHIVE_TAR_ZSTD},
		{".tar.zstd", HX_OS_ARCHIVE_TAR_ZSTD},
		{".zip", HX_OS_ARCHIVE_ZIP}, {".tar", HX_OS_ARCHIVE_TAR},
		{".7z", HX_OS_ARCHIVE_7ZIP}, {".gz", HX_OS_ARCHIVE_GZIP},
		{".xz", HX_OS_ARCHIVE_XZ}, {".zst", HX_OS_ARCHIVE_ZSTD},
		{".zstd", HX_OS_ARCHIVE_ZSTD}};

	size_t i;

	for (i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i)
		if (hx_ar_suffix(path, formats[i].suffix))
			return formats[i].format;

	return HX_OS_ARCHIVE_AUTO;
}

static hx_os_status hx_ar_run(hx_os_context *context, const char *source,
	const char *archive_path, const char *destination,
	const char *selection, const hx_os_archive_options *options,
	hx_os_archive_visit_fn visit, void *user, hx_os_archive_result *result,
	hx_os_error *error)
{
	hx_ar_job job;
	hx_os_error local;
	hx_os_status status;

	if (!error)
		error = &local;
	if (!archive_path || !*archive_path ||
		(!source && !destination && !visit) ||
		(selection && !*selection))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_ar_i_begin(&job, context, options, result, error);
	if (status != HX_OS_OK) {
		if (job.context)
			return hx_ar_i_end(&job, status, result, error);

		return status;
	}
	status = hx_os_i_path_full(archive_path, &job.archive_file, error);
	if (status == HX_OS_OK && source)
		status = hx_os_i_path_full(source, &job.source_path, error);
	if (status == HX_OS_OK && destination)
		status = hx_os_i_path_full(
			destination, &job.destination_path, error);
	if (status != HX_OS_OK) {
		(void)hx_ar_i_fail(
			&job, status, error, "Archive operation paths");
		return hx_ar_i_end(&job, status, result, error);
	}
	job.source = source ? hx_string_c_str(&job.source_path) : NULL;
	job.archive_path = hx_string_c_str(&job.archive_file);
	job.destination =
		destination ? hx_string_c_str(&job.destination_path) : NULL;
	job.selection = selection;
	job.visit = visit;
	job.visit_user = user;
	job.packing = source != NULL;
	job.format = job.options.format ? job.options.format
					: hx_ar_i_format(archive_path);
	if (!source && !job.options.format && job.format < HX_OS_ARCHIVE_GZIP)
		job.format = HX_OS_ARCHIVE_AUTO;
	status = hx_ar_i_check(&job, 0);
	if (status == HX_OS_OK && !source)
		status = hx_ar_i_open_input(&job);
	if (status == HX_OS_OK) {
		if (job.format == HX_OS_ARCHIVE_7ZIP &&
			(job.options.password ||
				job.options.seven_zip_library ||
				(job.options.flags &
					HX_OS_ARCHIVE_ENCRYPT_NAMES)))
			status = hx_ar_i_7zip(&job);
		else {
			ERR_STATE *saved = NULL;
			int clear_errors = 1;

			if (ERR_peek_error()) {
				saved = OSSL_ERR_STATE_new();
				if (saved)
					OSSL_ERR_STATE_save(saved);
				else {
					clear_errors = 0;
					status = hx_ar_i_fail(&job,
						HX_OS_NO_MEMORY, NULL,
						"Preserve caller OpenSSL "
						"errors");
				}
			}
			if (status == HX_OS_OK)
				status = hx_ar_i_locale_begin(&job);
			if (status == HX_OS_OK)
				status = source ? hx_ar_i_pack(&job)
						: hx_ar_i_read(&job);
			hx_ar_i_locale_end(&job);
			if (clear_errors)
				ERR_clear_error();
			if (saved) {
				OSSL_ERR_STATE_restore(saved);
				OSSL_ERR_STATE_free(saved);
			}
		}
	}
	if (status != HX_OS_OK &&
		!hx_os_atomic_load(&job.failed, HX_OS_ACQUIRE))
		(void)hx_ar_i_fail(&job, status, NULL, NULL);

	return hx_ar_i_end(&job, status, result, error);
}
#endif

hx_os_status hx_os_archive_pack_ex(hx_os_context *context, const char *source,
	const char *archive_path, const hx_os_archive_options *options,
	hx_os_archive_result *result, hx_os_error *error)
{
#if HX_OS_HAVE_ARCHIVE
	if (!source || !*source)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_ar_run(context, source, archive_path, NULL, NULL, options,
		NULL, NULL, result, error);
#else
	(void)context;
	(void)source;
	(void)archive_path;
	(void)options;
	(void)result;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_archive_unpack_ex(hx_os_context *context,
	const char *archive_path, const char *destination,
	const hx_os_archive_options *options, hx_os_archive_result *result,
	hx_os_error *error)
{
#if HX_OS_HAVE_ARCHIVE
	if (!destination || !*destination)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_ar_run(context, NULL, archive_path, destination, NULL,
		options, NULL, NULL, result, error);
#else
	(void)context;
	(void)archive_path;
	(void)destination;
	(void)options;
	(void)result;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_archive_extract_file_ex(hx_os_context *context,
	const char *archive_path, const char *entry_path,
	const char *destination_file, const hx_os_archive_options *options,
	hx_os_archive_result *result, hx_os_error *error)
{
#if HX_OS_HAVE_ARCHIVE
	if (!destination_file || !*destination_file || !entry_path)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_ar_run(context, NULL, archive_path, destination_file,
		entry_path, options, NULL, NULL, result, error);
#else
	(void)context;
	(void)archive_path;
	(void)entry_path;
	(void)destination_file;
	(void)options;
	(void)result;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_archive_visit_ex(hx_os_context *context,
	const char *archive_path, const hx_os_archive_options *options,
	hx_os_archive_visit_fn visit, void *user, hx_os_archive_result *result,
	hx_os_error *error)
{
#if HX_OS_HAVE_ARCHIVE
	if (!visit)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_ar_run(context, NULL, archive_path, NULL, NULL, options,
		visit, user, result, error);
#else
	(void)context;
	(void)archive_path;
	(void)options;
	(void)visit;
	(void)user;
	(void)result;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_archive_pack(
	const char *source, const char *archive_path, hx_os_error *error)
{
	return hx_os_archive_pack_ex(
		NULL, source, archive_path, NULL, NULL, error);
}

hx_os_status hx_os_archive_unpack(
	const char *archive_path, const char *destination, hx_os_error *error)
{
	return hx_os_archive_unpack_ex(
		NULL, archive_path, destination, NULL, NULL, error);
}

hx_os_status hx_os_archive_extract_file(const char *archive_path,
	const char *entry_path, const char *destination_file,
	hx_os_error *error)
{
	return hx_os_archive_extract_file_ex(NULL, archive_path, entry_path,
		destination_file, NULL, NULL, error);
}

hx_os_status hx_os_archive_visit(const char *archive_path,
	hx_os_archive_visit_fn visit, void *user, hx_os_error *error)
{
	return hx_os_archive_visit_ex(
		NULL, archive_path, NULL, visit, user, NULL, error);
}
