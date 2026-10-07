#if !defined(_WIN32)
#define _GNU_SOURCE
#endif
#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif
#if defined(__linux__)
#include <sys/vfs.h>
#endif
#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

typedef struct watch_test {
	hx_os_event *changed;
	hx_os_event *entered;
	hx_os_event *release;
	hx_os_atomic named;
	hx_os_atomic nested;
	hx_os_atomic directory;
	hx_os_atomic overflow;
	hx_os_atomic invalid;
	hx_os_atomic failures;
	hx_os_atomic calls;
	hx_os_atomic cleanups;
	int block;
} watch_test;

typedef struct allocator_test {
	hx_os_atomic calls;
	hx_os_atomic fail_at;
	hx_os_atomic live;
} allocator_test;

static void *allocate(size_t bytes, void *user)
{
	allocator_test *state = user;
	int64_t count =
		hx_os_atomic_fetch_add(&state->calls, 1, HX_OS_RELAXED) + 1;
	void *memory;

	if (count == hx_os_atomic_load(&state->fail_at, HX_OS_RELAXED))
		return NULL;

	memory = malloc(bytes);
	if (memory)
		(void)hx_os_atomic_fetch_add(&state->live, 1, HX_OS_RELAXED);

	return memory;
}

static void deallocate(void *memory, void *user)
{
	allocator_test *state = user;

	free(memory);
	(void)hx_os_atomic_fetch_add(&state->live, -1, HX_OS_RELAXED);
}

static void changed(
	hx_os_watch *watch, const hx_os_watch_event *event, void *user)
{
	watch_test *state = user;
	size_t invalid;
	int64_t previous =
		hx_os_atomic_fetch_add(&state->calls, 1, HX_OS_RELAXED);

	CHECK(event->size >= sizeof(*event) && event->version == 1);
	if (event->path) {
		CHECK(hx_os_text_validate_utf8(event->path, strlen(event->path),
			      &invalid) == HX_OS_OK);
		if (!strcmp(event->path, "中文-😀.txt"))
			(void)hx_os_atomic_fetch_add(
				&state->named, 1, HX_OS_RELEASE);

		if (!strcmp(event->path, "new/nested.txt"))
			(void)hx_os_atomic_fetch_add(
				&state->nested, 1, HX_OS_RELEASE);

		if (!strcmp(event->path, "new"))
			(void)hx_os_atomic_fetch_add(
				&state->directory, 1, HX_OS_RELEASE);
	}

	if (event->kind == HX_OS_WATCH_OVERFLOW) {
		(void)hx_os_atomic_fetch_add(
			&state->overflow, 1, HX_OS_RELEASE);
		if (event->error.status == HX_OS_INVALID_ENCODING)
			(void)hx_os_atomic_fetch_add(
				&state->invalid, 1, HX_OS_RELEASE);
	}

	if (event->kind == HX_OS_WATCH_ERROR)
		hx_os_atomic_store(
			&state->failures, event->error.status, HX_OS_RELEASE);

	if (state->changed)
		hx_os_event_set(state->changed);

	if (state->block && !previous) {
		hx_os_watch *self = watch;

		CHECK(hx_os_watch_close_until(&self, HX_OS_DEADLINE_NOW,
			      NULL) == HX_OS_DEADLOCK &&
			self == watch);

		hx_os_event_set(state->entered);
		CHECK(hx_os_event_wait_until(state->release,
			      HX_OS_DEADLINE_INFINITE, NULL) == HX_OS_OK);
	}
}

static void cleanup(void *user)
{
	watch_test *state = user;

	(void)hx_os_atomic_fetch_add(&state->cleanups, 1, HX_OS_RELEASE);
}

static void wait_for(watch_test *state, hx_os_atomic *counter, int64_t previous)
{
	hx_os_deadline deadline = hx_os_deadline_after_ms(5000);

	while (hx_os_atomic_load(counter, HX_OS_ACQUIRE) <= previous) {
		CHECK(!hx_os_atomic_load(&state->failures, HX_OS_ACQUIRE));
		CHECK(hx_os_event_wait_until(state->changed, deadline, NULL) ==
			HX_OS_OK);
	}
}

static void write_file(const char *path)
{
	hx_os_file *file = NULL;
	size_t completed;

	CHECK(hx_os_file_open(NULL, path,
		      HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
			      HX_OS_FILE_TRUNCATE,
		      &file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_write_all(file, "text", 4, &completed, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
}

static void test_failures(const char *root)
{
	allocator_test allocations;
	hx_os_context_options options;
	hx_os_context *context = NULL;
	hx_os_watch *watch = NULL;
	watch_test state;
	int64_t fail;
	int succeeded = 0;

	memset(&allocations, 0, sizeof(allocations));
	hx_os_context_options_init(&options);
	options.allocator.allocate = allocate;
	options.allocator.deallocate = deallocate;
	options.allocator.user = &allocations;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);

	for (fail = 1; fail <= 64; ++fail) {
		hx_os_status status;

		memset(&state, 0, sizeof(state));
		hx_os_atomic_store(&allocations.calls, 0, HX_OS_RELAXED);
		hx_os_atomic_store(&allocations.fail_at, fail, HX_OS_RELAXED);

		status = hx_os_watch_create_with_options(context, root, 0, NULL,
			changed, cleanup, &state, &watch, NULL);
		hx_os_atomic_store(&allocations.fail_at, 0, HX_OS_RELAXED);
		if (status == HX_OS_OK) {
			CHECK(hx_os_watch_close_until(&watch,
				      hx_os_deadline_after_ms(2000),
				      NULL) == HX_OS_OK);
			CHECK(hx_os_atomic_load(
				      &state.cleanups, HX_OS_ACQUIRE) == 1);

			succeeded = 1;
		}
		else {
			CHECK(status == HX_OS_NO_MEMORY && !watch);
			CHECK(!hx_os_atomic_load(&state.calls, HX_OS_ACQUIRE) &&
				!hx_os_atomic_load(
					&state.cleanups, HX_OS_ACQUIRE));
		}

		CHECK(hx_os_context_allocations(context) == 0);
		CHECK(hx_os_atomic_load(&allocations.live, HX_OS_ACQUIRE) == 1);
		if (succeeded)
			break;
	}

	CHECK(succeeded);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(hx_os_atomic_load(&allocations.live, HX_OS_ACQUIRE) == 0);
}

int main(void)
{
	hx_os_watch *watch = NULL;
	hx_os_watch_options options = HX_OS_WATCH_OPTIONS_INIT;
	hx_os_context *context = NULL;
	watch_test state;
	char root[128], path[192], directory[192], nested[224], pre[192];

	memset(&state, 0, sizeof(state));
	if (!(hx_os_capabilities() & HX_OS_CAP_WATCH)) {
		CHECK(hx_os_watch_create(NULL, "unused", 0, changed, &state,
			      &watch, NULL) == HX_OS_NOT_SUPPORTED);

		return 0;
	}
#if defined(__linux__)
	{
		struct statfs filesystem;

		CHECK(statfs(".", &filesystem) == 0);
		if (filesystem.f_type == 0x01021997L)
			CHECK(hx_os_watch_create(NULL, ".", 0, changed, &state,
				      &watch, NULL) == HX_OS_NOT_SUPPORTED &&
				!watch);
	}
#endif
#if defined(_WIN32) || defined(__ANDROID__)
	(void)snprintf(root, sizeof(root), "hx-watch-%llx-监听",
		(unsigned long long)hx_os_monotonic_ns());
#else
	(void)snprintf(root, sizeof(root), "/tmp/hx-watch-%llx-监听",
		(unsigned long long)hx_os_monotonic_ns());
#endif
	(void)snprintf(path, sizeof(path), "%s/中文-😀.txt", root);
	(void)snprintf(directory, sizeof(directory), "%s/new", root);
	(void)snprintf(nested, sizeof(nested), "%s/nested.txt", directory);
	(void)snprintf(pre, sizeof(pre), "%s/pre", root);
	CHECK(hx_os_directory_create(root, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_create(pre, 0, NULL) == HX_OS_OK);

	options.max_memory_bytes = 1;
	CHECK(hx_os_watch_create_with_options(NULL, root, 0, &options, changed,
		      cleanup, &state, &watch, NULL) == HX_OS_LIMIT &&
		!watch);
	CHECK(hx_os_watch_options_init(&options, sizeof(options)) == HX_OS_OK);
#if defined(__linux__)
	options.max_directories = 1;
	CHECK(hx_os_watch_create_with_options(NULL, root, 1, &options, changed,
		      cleanup, &state, &watch, NULL) == HX_OS_LIMIT &&
		!watch);
	CHECK(hx_os_watch_options_init(&options, sizeof(options)) == HX_OS_OK);
#endif
	test_failures(root);
	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 0, 0, &state.changed, NULL) == HX_OS_OK);
	CHECK(hx_os_watch_create_with_options(context, root, 1, NULL, changed,
		      cleanup, &state, &watch, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);

	write_file(path);
	wait_for(&state, &state.named, 0);
	CHECK(hx_os_directory_create(directory, 0, NULL) == HX_OS_OK);

	wait_for(&state, &state.directory, 0);
#if defined(__linux__)
	wait_for(&state, &state.overflow, 0);
#endif
	write_file(nested);
	wait_for(&state, &state.nested, 0);
#if defined(__linux__)
	{
		char invalid_path[192];
		int descriptor;

		(void)snprintf(
			invalid_path, sizeof(invalid_path), "%s/\xff", root);
		descriptor =
			open(invalid_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
		CHECK(descriptor >= 0 && close(descriptor) == 0);

		wait_for(&state, &state.invalid, 0);
		CHECK(unlink(invalid_path) == 0);
	}
#endif
	CHECK(hx_os_watch_close_until(&watch, hx_os_deadline_after_ms(2000),
		      NULL) == HX_OS_OK &&
		!watch);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 1);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);

	hx_os_event_destroy(state.changed);
	memset(&state, 0, sizeof(state));
	state.block = 1;
	CHECK(hx_os_event_create(NULL, 1, 0, &state.entered, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.release, NULL) == HX_OS_OK);
	CHECK(hx_os_watch_create_with_options(NULL, root, 0, NULL, changed,
		      cleanup, &state, &watch, NULL) == HX_OS_OK);

	write_file(path);
	CHECK(hx_os_event_wait_until(state.entered,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(hx_os_watch_close_until(&watch, HX_OS_DEADLINE_NOW, NULL) ==
			HX_OS_TIMEOUT &&
		watch);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 0);

	hx_os_event_set(state.release);
	CHECK(hx_os_watch_close_until(&watch, hx_os_deadline_after_ms(2000),
		      NULL) == HX_OS_OK &&
		!watch);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 1);

	hx_os_event_destroy(state.entered);
	hx_os_event_destroy(state.release);
	CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(nested, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_remove(directory, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_remove(pre, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_remove(root, NULL) == HX_OS_OK);

	puts("bounded directory watch lifecycle contracts passed");

	return 0;
}
