#include "utility_test.h"
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif
#if defined(__linux__)
#include <sys/stat.h>
#endif

static const char payload[] = "{\"device\":\"传感器\",\"samples\":[1,2,3]}\n";
static char root[1024], source[1200], archive_path[1200], destination[1200];
static const char *seven_library, *seven_adapter;

typedef struct archive_thread {
	char output[1400];
	char restored[1400];
} archive_thread;

static void *HX_OS_CALL run_archive_thread(void *user)
{
	archive_thread *state = user;
	hx_os_archive_options options = HX_OS_ARCHIVE_OPTIONS_INIT;
	hx_string diagnostic = HX_STRING_INIT;

	options.seven_zip_library = seven_library;
	options.seven_zip_adapter = seven_adapter;
	options.password = seven_library ? "thread-test" : NULL;
	options.workers = 2;
	options.diagnostic = &diagnostic;
	CHECK(hx_os_archive_pack_ex(NULL, source, state->output, &options, NULL,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_archive_unpack_ex(NULL, state->output, state->restored,
		      &options, NULL, NULL) == HX_OS_OK);
	hx_string_destroy(&diagnostic);
	return 0;
}

static void check_concurrency(void)
{
	archive_thread states[2];
	hx_os_thread *threads[2] = {NULL, NULL};
	hx_os_file *file = NULL;
	unsigned char chunk[4096];
	char path[1300];
	size_t i, written;
	void *code;

	for (i = 0; i < sizeof(chunk); ++i)
		chunk[i] = (unsigned char)(i * 37u);
	(void)snprintf(path, sizeof(path), "%s/load.bin", source);
	CHECK(hx_os_file_open(NULL, path,
		      HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
			      HX_OS_FILE_EXCLUSIVE,
		      &file, NULL) == HX_OS_OK);
	for (i = 0; i < 256; ++i)
		CHECK(hx_os_file_write_all(file, chunk, sizeof(chunk), &written,
			      NULL) == HX_OS_OK);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	for (i = 0; i < 2; ++i) {
		(void)snprintf(states[i].output, sizeof(states[i].output),
			"%s/thread-%u.%s", root, (unsigned)i,
			seven_library ? "7z" : "tar.xz");
		(void)snprintf(states[i].restored, sizeof(states[i].restored),
			"%s/thread-%u", root, (unsigned)i);
		CHECK(hx_os_thread_create(NULL, run_archive_thread, &states[i],
			      &threads[i], NULL) == HX_OS_OK);
	}
	for (i = 0; i < 2; ++i) {
		CHECK(hx_os_thread_join_until(threads[i],
			      hx_os_deadline_after_ms(10000), &code,
			      NULL) == HX_OS_OK &&
			code == 0);
		CHECK(hx_os_thread_destroy(&threads[i], NULL) == HX_OS_OK);
	}
}

static void require_status(
	hx_os_status status, hx_string *diagnostic, const char *operation)
{
	if (status != HX_OS_OK) {
		fprintf(stderr, "%s: %s: %s\n", operation,
			hx_os_status_name(status), hx_string_c_str(diagnostic));
		exit(1);
	}
}

static hx_os_status HX_OS_CALL visit_entry(
	const hx_os_archive_entry *entry, void *user, hx_os_error *error)
{
	size_t *count = user;
	(void)error;

	CHECK(entry->path.size && entry->path.data[entry->path.size] == 0);
	CHECK(entry->kind == HX_OS_ARCHIVE_FILE ||
		entry->kind == HX_OS_ARCHIVE_DIRECTORY);
	++*count;
	return HX_OS_OK;
}

static hx_os_status HX_OS_CALL cancel_progress(
	const hx_os_archive_result *progress, void *user, hx_os_error *error)
{
	(void)progress;
	(void)user;
	(void)error;
	return HX_OS_CANCELLED;
}

static void round_trip(const char *suffix, const char *password, int raw)
{
	hx_os_archive_options options = HX_OS_ARCHIVE_OPTIONS_INIT;
	hx_os_archive_result result;
	hx_string diagnostic = HX_STRING_INIT, text = HX_STRING_INIT;
	char input[1300], output[1300];
	size_t entries = 0;

	(void)snprintf(
		archive_path, sizeof(archive_path), "%s/data%s", root, suffix);
	(void)snprintf(
		destination, sizeof(destination), "%s/out%s", root, suffix);
	(void)snprintf(input, sizeof(input), "%s/deep/中文.json", source);
	options.diagnostic = &diagnostic;
	options.password = password;
	options.seven_zip_library = seven_library;
	options.seven_zip_adapter = seven_adapter;
	if (password && seven_library)
		options.flags |= HX_OS_ARCHIVE_ENCRYPT_NAMES;
	CHECK(hx_os_archive_result_init(&result, sizeof(result)) == HX_OS_OK);
	require_status(hx_os_archive_pack_ex(NULL, raw ? input : source,
			       archive_path, &options, &result, NULL),
		&diagnostic, "pack");
	CHECK(result.published == 1 && result.files == 1);
	CHECK(result.content_bytes == sizeof(payload) - 1);
	CHECK(hx_os_archive_pack_ex(NULL, raw ? input : source, archive_path,
		      &options, NULL, NULL) == HX_OS_EXISTS);
	require_status(hx_os_archive_visit_ex(NULL, archive_path, &options,
			       visit_entry, &entries, NULL, NULL),
		&diagnostic, "list");
	CHECK(entries == (raw ? 1u : 3u));
	require_status(hx_os_archive_unpack_ex(NULL, archive_path, destination,
			       &options, &result, NULL),
		&diagnostic, "unpack");
	CHECK(result.published == 1);
	(void)snprintf(output, sizeof(output), "%s/%s", destination,
		raw ? "data" : "deep/中文.json");
	CHECK(hx_os_file_read_text(output, &text, NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), payload));
	CHECK(hx_os_archive_unpack_ex(NULL, archive_path, destination, &options,
		      NULL, NULL) == HX_OS_EXISTS);
	options.flags |= HX_OS_ARCHIVE_OVERWRITE;
	require_status(hx_os_archive_unpack_ex(NULL, archive_path, destination,
			       &options, NULL, NULL),
		&diagnostic, "overwrite");
	(void)snprintf(output, sizeof(output), "%s/chosen.json", destination);
	require_status(hx_os_archive_extract_file_ex(NULL, archive_path,
			       raw ? "data" : "deep/中文.json", output,
			       &options, NULL, NULL),
		&diagnostic, "extract one");
	CHECK(hx_os_archive_extract_file_ex(NULL, archive_path, "absent",
		      output, &options, NULL, NULL) == HX_OS_NOT_FOUND);
	if (password) {
		options.password = "incorrect";
		CHECK(hx_os_archive_extract_file_ex(NULL, archive_path,
			      "deep/中文.json", output, &options, NULL,
			      NULL) != HX_OS_OK);
		CHECK(hx_os_file_read_text(output, &text, NULL) == HX_OS_OK);
		CHECK(!strcmp(hx_string_c_str(&text), payload));
	}
	hx_string_destroy(&text);
	hx_string_destroy(&diagnostic);
	printf("archive %s encrypted=%d passed\n", suffix, password != NULL);
}

static void check_failures(void)
{
	static const char *formats[] = {
		"zip", "tar.gz", "tar.xz", "tar.zst", "7z"};
	hx_os_archive_options options = HX_OS_ARCHIVE_OPTIONS_INIT;
	hx_os_archive_result result;
	char target[1300];
	size_t i;

	(void)snprintf(target, sizeof(target), "%s/failure.zip", root);
	CHECK(hx_os_archive_result_init(&result, sizeof(result)) == HX_OS_OK);
	options.max_content_bytes = 4;
	CHECK(hx_os_archive_pack_ex(NULL, source, target, &options, &result,
		      NULL) == HX_OS_LIMIT &&
		!result.published);
	options.max_content_bytes = 65536;
	options.max_depth = 1;
	CHECK(hx_os_archive_pack_ex(NULL, source, target, &options, NULL,
		      NULL) == HX_OS_LIMIT);
	options.max_depth = 64;
	options.max_archive_bytes = 32;
	for (i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
		(void)snprintf(target, sizeof(target), "%s/failure.%s", root,
			formats[i]);
		CHECK(hx_os_archive_pack_ex(NULL, source, target, &options,
			      NULL, NULL) == HX_OS_LIMIT);
	}
	options.max_archive_bytes = 65536;
	options.max_entries = 1;
	CHECK(hx_os_archive_pack_ex(NULL, source, target, &options, NULL,
		      NULL) == HX_OS_LIMIT);
	options.max_entries = 65536;
	options.max_memory_bytes = 128 * 1024;
	CHECK(hx_os_archive_pack_ex(NULL, source, target, &options, NULL,
		      NULL) == HX_OS_LIMIT);
	options.max_memory_bytes = 16 * 1024 * 1024;
	options.progress = cancel_progress;
	CHECK(hx_os_archive_pack_ex(NULL, source, target, &options, NULL,
		      NULL) == HX_OS_CANCELLED);
	options.progress = NULL;
	options.deadline = 0;
	CHECK(hx_os_archive_pack_ex(NULL, source, target, &options, NULL,
		      NULL) == HX_OS_TIMEOUT);
	(void)snprintf(target, sizeof(target), "%s/inside.zip", source);
	CHECK(hx_os_archive_pack(source, target, NULL) == HX_OS_INVALID);
}

static void check_allocation(void)
{
	size_t i;
	char target[1300];
	hx_os_archive_options options = HX_OS_ARCHIVE_OPTIONS_INIT;

	(void)snprintf(target, sizeof(target), "%s/allocation.%s", root,
		seven_library ? "7z" : "zip");
	options.flags = HX_OS_ARCHIVE_OVERWRITE;
	options.seven_zip_library = seven_library;
	options.seven_zip_adapter = seven_adapter;
	options.password = seven_library ? "allocation-test" : NULL;
	for (i = 1; i <= (seven_library ? 180u : 100u); ++i) {
		test_memory memory = {0, 0, 0};
		hx_os_context *context = test_context(&memory);
		hx_os_status status;

		memory.fail = memory.calls + i;
		status = hx_os_archive_pack_ex(
			context, source, target, &options, NULL, NULL);
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
		CHECK(!memory.live);
	}
}

static void check_search_only_ancestor(void)
{
#if defined(__linux__)
	char parent[1300], directory[1350], target[1400];
	hx_os_status status;

	(void)snprintf(parent, sizeof(parent), "%s/search-only", root);
	(void)snprintf(directory, sizeof(directory), "%s/output", parent);
	(void)snprintf(target, sizeof(target), "%s/test.zip", directory);
	CHECK(hx_os_directory_create(directory, 1, NULL) == HX_OS_OK);
	CHECK(chmod(parent, S_IXUSR) == 0);
	status = hx_os_archive_pack(source, target, NULL);
	/* Restore access before checking, including a failed regression. */
	CHECK(chmod(parent, S_IRWXU) == 0);
	CHECK(status == HX_OS_OK);
	CHECK(hx_os_directory_remove_recursive(parent, NULL) == HX_OS_OK);
#endif
}

int main(int argc, char **argv)
{
	hx_string temporary = HX_STRING_INIT;
	char path[1300];
#if defined(_WIN32)
	unsigned long process_id = (unsigned long)_getpid();
#else
	unsigned long process_id = (unsigned long)getpid();
#endif

	if (!hx_os_archive_available()) {
		CHECK(hx_os_archive_pack("source", "out.zip", NULL) ==
			HX_OS_NOT_SUPPORTED);
		return 0;
	}
#if defined(__ANDROID__)
	/* The Android host supplies writable app/test storage. */
	{
		const char *directory = getenv("HX_OS_TEST_TMPDIR");

		CHECK(directory && *directory);
		CHECK(hx_string_assign_cstr(&temporary, directory, NULL) ==
			HX_OS_OK);
	}
#else
	CHECK(hx_os_path_temp_string(&temporary, NULL) == HX_OS_OK);
#endif
	/* Backend tests run concurrently; a coarse clock alone can collide. */
	(void)snprintf(root, sizeof(root), "%s/hx-archive-%lu-%llu",
		hx_string_c_str(&temporary), process_id,
		(unsigned long long)hx_os_monotonic_ns());
	hx_string_destroy(&temporary);
	(void)snprintf(source, sizeof(source), "%s/source", root);
	(void)snprintf(path, sizeof(path), "%s/deep", source);
	CHECK(hx_os_directory_create(path, 1, NULL) == HX_OS_OK);
	(void)snprintf(path, sizeof(path), "%s/empty", source);
	CHECK(hx_os_directory_create(path, 1, NULL) == HX_OS_OK);
	(void)snprintf(path, sizeof(path), "%s/deep/中文.json", source);
	CHECK(hx_os_file_save(path, payload, sizeof(payload) - 1, 0, NULL) ==
		HX_OS_OK);
	check_search_only_ancestor();
	if (argc == 3) {
		CHECK(hx_os_archive_7zip_available());
		seven_library = argv[1];
		seven_adapter = argv[2];
		round_trip(".7z", "密码🔑-test-only", 0);
		check_allocation();
		check_concurrency();
		CHECK(hx_os_directory_remove_recursive(root, NULL) == HX_OS_OK);
		return 0;
	}

	round_trip(".zip", NULL, 0);
	round_trip(".tar", NULL, 0);
	round_trip(".tar.gz", NULL, 0);
	round_trip(".tar.xz", NULL, 0);
	round_trip(".tar.zst", NULL, 0);
	round_trip(".7z", NULL, 0);
	round_trip(".gz", NULL, 1);
	round_trip(".xz", NULL, 1);
	round_trip(".zst", NULL, 1);
	(void)snprintf(path, sizeof(path), "%s/data.zip", root);
	CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
	(void)snprintf(path, sizeof(path), "%s/out.zip", root);
	CHECK(hx_os_directory_remove_recursive(path, NULL) == HX_OS_OK);
	round_trip(".zip", "密码🔑-test-only", 0);
	check_failures();
	check_allocation();
	check_concurrency();
	CHECK(hx_os_directory_remove_recursive(root, NULL) == HX_OS_OK);
	return 0;
}
