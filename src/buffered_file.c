#include "internal.h"
#include "hx_os_buffered_file.h"

struct hx_os_file_writer {
	hx_os_stream *sink;
	hx_os_mutex *mutex;
	hx_os_condition *condition;
	hx_os_thread *thread;
	unsigned char *queue;
	size_t capacity, chunk, head, used, active;
	uint64_t accepted, written, flush_target;
	hx_os_error failure, flush_error, close_error;
	int started, producer, flusher, flush_pending, flush_native;
	int flush_persistent, closing, aborting, exited, close_claim;
};

static hx_os_status hx_os_writer_error(
	hx_os_error *out, const hx_os_error *source)
{
	if (out)
		*out = *source;

	return source->status;
}

static void hx_os_writer_notify(hx_os_file_writer *writer)
{
	hx_os_condition_notify_all(writer->condition);
}

static hx_os_status hx_os_writer_wait(
	hx_os_file_writer *writer, hx_os_deadline deadline, hx_os_error *error)
{
	return hx_os_condition_wait_until(
		writer->condition, writer->mutex, deadline, error);
}

static void hx_os_writer_fail(hx_os_file_writer *writer, hx_os_status status,
	const hx_os_error *error)
{
	writer->failure = *error;
	writer->failure.status = status;
	hx_os_writer_notify(writer);
}

static void hx_os_writer_flush(hx_os_file_writer *writer, int persistent)
{
	hx_os_error error = {0, 0, 0, 0};
	hx_os_status status;

	writer->flush_native = 1;
	hx_os_mutex_unlock(writer->mutex, NULL);
	status = hx_os_stream_flush(writer->sink, persistent, &error);
	hx_os_mutex_lock(writer->mutex, NULL);
	writer->flush_native = 0;
	writer->flush_pending = 0;
	writer->flush_error = error;
	writer->flush_error.status = status;
	if (status != HX_OS_OK)
		hx_os_writer_fail(writer, status, &error);
	hx_os_writer_notify(writer);
}

static void hx_os_writer_transfer(hx_os_file_writer *writer)
{
	size_t count = writer->capacity - writer->head, completed = 0;
	hx_os_error error = {0, 0, 0, 0};
	hx_os_status status;

	if (count > writer->used)
		count = writer->used;
	if (count > writer->chunk)
		count = writer->chunk;
	if (writer->flush_pending &&
		count > writer->flush_target - writer->written)
		count = (size_t)(writer->flush_target - writer->written);

	// This occupied span cannot be overwritten by a producer.
	hx_os_mutex_unlock(writer->mutex, NULL);
	status = hx_os_stream_write_some(writer->sink,
		writer->queue + writer->head, count, &completed, &error);
	hx_os_mutex_lock(writer->mutex, NULL);
	writer->head = completed >= writer->capacity - writer->head
		? completed - (writer->capacity - writer->head)
		: writer->head + completed;
	writer->used -= completed;
	writer->written += completed;
	if (status == HX_OS_OK && !completed)
		status = hx_os_i_error(&error, HX_OS_IO_ERROR, 0, 0);
	if (status != HX_OS_OK)
		hx_os_writer_fail(writer, status, &error);
	hx_os_writer_notify(writer);
}

static void *HX_OS_CALL hx_os_writer_worker(void *user)
{
	hx_os_file_writer *writer = user;
	hx_os_error error = {0, 0, 0, 0};
	hx_os_status status;

	hx_os_mutex_lock(writer->mutex, NULL);
	for (;;) {
		if (writer->started) {
			if (writer->aborting) {
				writer->used = 0;
				writer->flush_pending = 0;
				break;
			}
			if (writer->failure.status == HX_OS_OK) {
				if (writer->flush_pending &&
					writer->written >=
						writer->flush_target) {
					hx_os_writer_flush(writer,
						writer->flush_persistent);
					continue;
				}
				if (writer->used) {
					hx_os_writer_transfer(writer);
					continue;
				}
				if (writer->closing) {
					hx_os_writer_flush(writer, 0);
					if (writer->failure.status == HX_OS_OK)
						break;
					continue;
				}
			}
		}

		status = hx_os_writer_wait(
			writer, HX_OS_DEADLINE_INFINITE, &error);
		if (status != HX_OS_OK) {
			hx_os_writer_fail(writer, status, &error);
			break;
		}
	}
	hx_os_mutex_unlock(writer->mutex, NULL);
	status = hx_os_stream_close(&writer->sink, &error);
	hx_os_mutex_lock(writer->mutex, NULL);
	writer->close_error = error;
	writer->close_error.status = status;
	writer->exited = 1;
	hx_os_writer_notify(writer);
	hx_os_mutex_unlock(writer->mutex, NULL);

	return NULL;
}

int hx_os_file_writer_available(void)
{
	return HX_OS_ENABLE_STREAM && HX_OS_ENABLE_BUFFERED_FILE;
}

hx_os_status hx_os_file_writer_options_init(
	hx_os_file_writer_options *options, size_t capacity)
{
	hx_os_file_writer_options defaults = HX_OS_FILE_WRITER_OPTIONS_INIT;

	if (!options || capacity < sizeof(*options))
		return HX_OS_INVALID;

	*options = defaults;
	return HX_OS_OK;
}

static void hx_os_writer_free(hx_os_file_writer *writer)
{
	hx_os_thread_destroy(&writer->thread, NULL);
	hx_os_condition_destroy(writer->condition);
	hx_os_mutex_destroy(writer->mutex);
	hx_os_memory_free(writer->queue);
	hx_os_memory_free(writer);
}

hx_os_status hx_os_file_writer_from_stream(hx_os_context *context,
	hx_os_stream **sink, const hx_os_file_writer_options *options,
	hx_os_file_writer **out, hx_os_error *error)
{
	hx_os_file_writer_options defaults = HX_OS_FILE_WRITER_OPTIONS_INIT;
	hx_os_file_writer *writer;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!hx_os_file_writer_available())
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!options)
		options = &defaults;
	if (!sink || !*sink || options->size < sizeof(*options) ||
		options->version != 1 || !options->queue_bytes ||
		options->chunk_bytes > options->queue_bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	writer = hx_os_i_zero(context, sizeof(*writer));
	if (!writer)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	writer->capacity = options->queue_bytes;
	writer->chunk = options->chunk_bytes ? options->chunk_bytes : 65536;
	if (writer->chunk > writer->capacity)
		writer->chunk = writer->capacity;
	writer->queue = hx_os_i_alloc(context, writer->capacity, 0);
	status = writer->queue ? HX_OS_OK : HX_OS_NO_MEMORY;
	if (status != HX_OS_OK)
		hx_os_i_error(error, status, 0, 0);
	if (status == HX_OS_OK)
		status = hx_os_mutex_create(context, 0, &writer->mutex, error);
	if (status == HX_OS_OK)
		status = hx_os_condition_create(
			context, &writer->condition, error);
	if (status == HX_OS_OK)
		status = hx_os_thread_create(context, hx_os_writer_worker,
			writer, &writer->thread, error);
	if (status != HX_OS_OK) {
		hx_os_writer_free(writer);
		return status;
	}

	hx_os_mutex_lock(writer->mutex, NULL);
	writer->sink = *sink;
	*sink = NULL;
	writer->started = 1;
	hx_os_writer_notify(writer);
	hx_os_mutex_unlock(writer->mutex, NULL);
	*out = writer;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_file_writer_open_ex(hx_os_context *context, const char *path,
	uint32_t flags, const hx_os_file_writer_options *options,
	hx_os_file_writer **out, hx_os_error *error)
{
	hx_os_stream *sink = NULL;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!hx_os_file_writer_available())
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!(flags & HX_OS_FILE_WRITE))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_stream_open(context, path, flags, &sink, error);
	if (status == HX_OS_OK)
		status = hx_os_file_writer_from_stream(
			context, &sink, options, out, error);
	hx_os_stream_close(&sink, NULL);

	return status;
}

hx_os_status hx_os_file_writer_open(
	const char *path, hx_os_file_writer **out, hx_os_error *error)
{
	return hx_os_file_writer_open_ex(NULL, path,
		HX_OS_FILE_WRITE | HX_OS_FILE_CREATE | HX_OS_FILE_TRUNCATE,
		NULL, out, error);
}

static hx_os_status hx_os_writer_enter(
	hx_os_file_writer *writer, hx_os_error *error)
{
	hx_os_status status;

	if (!hx_os_file_writer_available())
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!writer)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (hx_os_thread_is_current(writer->thread))
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	status = hx_os_mutex_lock(writer->mutex, error);
	if (status == HX_OS_OK)
		++writer->active;

	return status;
}

static void hx_os_writer_leave(hx_os_file_writer *writer)
{
	--writer->active;
	hx_os_writer_notify(writer);
	hx_os_mutex_unlock(writer->mutex, NULL);
}

static hx_os_status hx_os_writer_accepting(
	hx_os_file_writer *writer, hx_os_error *error)
{
	if (writer->failure.status != HX_OS_OK)
		return hx_os_writer_error(error, &writer->failure);
	if (writer->closing || writer->exited)
		return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);

	return HX_OS_OK;
}

hx_os_status hx_os_file_writer_write_until(hx_os_file_writer *writer,
	const void *data, size_t bytes, size_t *accepted,
	hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status;
	size_t copied = 0;
	int claimed = 0;

	if (accepted)
		*accepted = 0;
	if (!accepted || (!data && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_writer_enter(writer, error);
	if (status != HX_OS_OK)
		return status;

	for (;;) {
		status = hx_os_writer_accepting(writer, error);
		if (status != HX_OS_OK)
			break;
		if (!writer->producer) {
			writer->producer = claimed = 1;
			break;
		}
		status = hx_os_writer_wait(writer, deadline, error);
		if (status != HX_OS_OK)
			break;
	}
	if (status == HX_OS_OK && bytes > UINT64_MAX - writer->accepted)
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	while (status == HX_OS_OK && copied < bytes) {
		size_t count, tail;

		status = hx_os_writer_accepting(writer, error);
		if (status != HX_OS_OK)
			break;
		if (writer->used == writer->capacity) {
			status = hx_os_writer_wait(writer, deadline, error);
			continue;
		}

		tail = writer->head >= writer->capacity - writer->used
			? writer->head - (writer->capacity - writer->used)
			: writer->head + writer->used;
		count = writer->capacity - tail;
		if (count > writer->capacity - writer->used)
			count = writer->capacity - writer->used;
		if (count > bytes - copied)
			count = bytes - copied;
		memcpy(writer->queue + tail,
			(const unsigned char *)data + copied, count);
		writer->used += count;
		writer->accepted += count;
		copied += count;
		hx_os_writer_notify(writer);
	}

	if (claimed)
		writer->producer = 0;
	*accepted = copied;
	hx_os_writer_leave(writer);

	return status == HX_OS_OK ? hx_os_i_error(error, status, 0, 0) : status;
}

hx_os_status hx_os_file_writer_flush_until(hx_os_file_writer *writer,
	int persistent, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status;
	uint64_t target;
	int claimed = 0;

	if (persistent != 0 && persistent != 1)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_writer_enter(writer, error);
	if (status != HX_OS_OK)
		return status;

	target = writer->accepted;
	while ((status = hx_os_writer_accepting(writer, error)) == HX_OS_OK) {
		if (!writer->flusher && !writer->flush_pending) {
			writer->flusher = claimed = 1;
			writer->flush_pending = 1;
			writer->flush_target = target;
			writer->flush_persistent = persistent;
			hx_os_writer_notify(writer);
			break;
		}
		status = hx_os_writer_wait(writer, deadline, error);
		if (status != HX_OS_OK)
			break;
	}
	while (status == HX_OS_OK && writer->flush_pending) {
		if (writer->failure.status != HX_OS_OK) {
			status = hx_os_writer_error(error, &writer->failure);
			if (!writer->flush_native)
				writer->flush_pending = 0;
			break;
		}
		if (writer->aborting || writer->exited) {
			status = hx_os_i_error(error, HX_OS_CLOSED, 0, 0);
			break;
		}
		status = hx_os_writer_wait(writer, deadline, error);
	}
	if (status == HX_OS_OK && claimed)
		status = hx_os_writer_error(error, &writer->flush_error);
	if (status == HX_OS_OK && writer->aborting)
		status = hx_os_i_error(error, HX_OS_CLOSED, 0, 0);
	if (claimed)
		writer->flusher = 0;
	hx_os_writer_leave(writer);

	return status == HX_OS_OK ? hx_os_i_error(error, status, 0, 0) : status;
}

hx_os_status hx_os_file_writer_retry(
	hx_os_file_writer *writer, hx_os_error *error)
{
	hx_os_status status = hx_os_writer_enter(writer, error);

	if (status != HX_OS_OK)
		return status;
	status = writer->exited || writer->aborting ? HX_OS_CLOSED : HX_OS_OK;
	if (status == HX_OS_OK)
		hx_os_error_clear(&writer->failure);
	hx_os_writer_leave(writer);

	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_file_writer_pending(
	hx_os_file_writer *writer, size_t *bytes, hx_os_error *error)
{
	hx_os_status status;

	if (!bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_writer_enter(writer, error);
	if (status != HX_OS_OK)
		return status;
	*bytes = writer->used;
	status = hx_os_writer_error(error, &writer->failure);
	hx_os_writer_leave(writer);

	return status;
}

hx_os_status hx_os_file_writer_stop(
	hx_os_file_writer *writer, hx_os_error *error)
{
	hx_os_status status = hx_os_writer_enter(writer, error);

	if (status != HX_OS_OK)
		return status;
	writer->closing = 1;
	hx_os_writer_leave(writer);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_writer_close(hx_os_file_writer **owner,
	hx_os_deadline deadline, int aborting, hx_os_error *error)
{
	hx_os_file_writer *writer;
	hx_os_status status;

	if (!owner)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!*owner)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	writer = *owner;
	if (hx_os_thread_is_current(writer->thread))
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	status = hx_os_mutex_lock(writer->mutex, error);
	if (status != HX_OS_OK)
		return status;
	if (writer->close_claim) {
		hx_os_mutex_unlock(writer->mutex, NULL);
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	}

	writer->close_claim = writer->closing = 1;
	writer->aborting |= aborting;
	hx_os_writer_notify(writer);
	while (!writer->exited || writer->active) {
		if (!writer->aborting && writer->failure.status != HX_OS_OK) {
			status = hx_os_writer_error(error, &writer->failure);
			break;
		}
		status = hx_os_writer_wait(writer, deadline, error);
		if (status != HX_OS_OK)
			break;
	}
	if (status == HX_OS_OK && !writer->aborting &&
		writer->failure.status != HX_OS_OK)
		status = hx_os_writer_error(error, &writer->failure);
	hx_os_mutex_unlock(writer->mutex, NULL);
	if (status == HX_OS_OK)
		status = hx_os_thread_join_until(
			writer->thread, deadline, NULL, error);
	if (status != HX_OS_OK) {
		hx_os_mutex_lock(writer->mutex, NULL);
		writer->close_claim = 0;
		hx_os_mutex_unlock(writer->mutex, NULL);
		return status;
	}

	status = hx_os_writer_error(error, &writer->close_error);
	*owner = NULL;
	hx_os_writer_free(writer);

	return status;
}

hx_os_status hx_os_file_writer_close_until(
	hx_os_file_writer **writer, hx_os_deadline deadline, hx_os_error *error)
{
	return hx_os_writer_close(writer, deadline, 0, error);
}

hx_os_status hx_os_file_writer_abort_until(
	hx_os_file_writer **writer, hx_os_deadline deadline, hx_os_error *error)
{
	return hx_os_writer_close(writer, deadline, 1, error);
}
