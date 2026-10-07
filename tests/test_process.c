#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

static hx_os_deadline test_deadline(void)
{
	return hx_os_deadline_after_ms(5000);
}

typedef struct allocation_state {
	size_t calls;
	size_t fail_at;
	size_t live;
} allocation_state;

static void *HX_OS_CALL test_allocate(size_t bytes, void *user)
{
	allocation_state *state = user;
	void *memory;

	if (++state->calls == state->fail_at)
		return NULL;

	memory = malloc(bytes);
	if (memory)
		++state->live;

	return memory;
}

static void HX_OS_CALL test_deallocate(void *memory, void *user)
{
	allocation_state *state = user;

	if (memory) {
		CHECK(state->live);

		--state->live;

		free(memory);
	}
}

static void allocation_failures(const char *path)
{
	hx_os_context_options options;
	hx_os_context *context = NULL;
	allocation_state state = {0, 0, 0};
	size_t i;

	hx_os_context_options_init(&options);
	options.allocator.allocate = test_allocate;
	options.allocator.deallocate = test_deallocate;
	options.allocator.user = &state;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);

	for (i = 1; i <= 64; ++i) {
		hx_os_process_capture *capture = NULL;
		hx_os_status status;

		state.calls = 0;
		state.fail_at = i;

		status = hx_os_process_capture_run(context, path, NULL, NULL,
			test_deadline(), &capture, NULL);
		state.fail_at = 0;
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		if (capture) {
			CHECK(hx_os_context_destroy(&context, NULL) ==
				HX_OS_BUSY);
			CHECK(hx_os_process_capture_close_until(&capture,
				      test_deadline(), NULL) == HX_OS_OK);
		}

		if (state.live != 1)
			fprintf(stderr,
				"allocation failure %lu: live=%lu calls=%lu\n",
				(unsigned long)i, (unsigned long)state.live,
				(unsigned long)state.calls);

		CHECK(state.live == 1);
		CHECK(hx_os_context_allocations(context) == 0);
	}

	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(!state.live);
}

static void capture_check(hx_os_process_capture *capture, int stream,
	const void *expected, size_t length)
{
	const void *data;
	size_t bytes;

	CHECK(hx_os_process_capture_view(capture, stream, &data, &bytes) ==
		HX_OS_OK);
	CHECK(bytes == length && (!length || !memcmp(data, expected, length)));
}

int main(int argc, char **argv)
{
	hx_os_process *process = NULL;
	hx_os_process_capture *capture = NULL;
	hx_os_process_result result = {99, 99};
	hx_os_process_options options = HX_OS_PROCESS_OPTIONS_INIT;
	hx_os_process_capture_options capture_options =
		HX_OS_PROCESS_CAPTURE_OPTIONS_INIT;
	hx_os_error error;

	uint64_t caps = hx_os_capabilities();

	const char *delay[] = {"delay", NULL};
	const char *bulk[] = {"bulk", NULL};
	const char *binary[] = {"binary", NULL};
	const char *input[] = {"stdin", NULL};
	const char *exit_code[] = {"exit259", NULL};
	const char *arguments[] = {"arguments", "", "a b", "a\"b", "trailing\\",
		"before\\\"quote", "中文😀", NULL};
	const char *expected = "0:\n3:a b\n3:a\"b\n9:trailing\\\n"
			       "13:before\\\"quote\n10:中文😀\n";
	const char *duplicates[] = {
		"HX_PROCESS_TEST=x", "HX_PROCESS_TEST=y", NULL};
	const char *empty_environment[] = {NULL};
	hx_string text = HX_STRING_INIT;
	const void *data;
	size_t bytes, i;

	CHECK(argc == 2);
	CHECK(hx_os_process_options_init(&options, sizeof(options) - 1) ==
		HX_OS_INVALID);
	CHECK(hx_os_process_capture_options_init(
		      &capture_options, sizeof(capture_options)) == HX_OS_OK);
	if (!(caps & HX_OS_CAP_PROCESS)) {
		CHECK(hx_os_process_spawn(NULL, argv[1], NULL, NULL, NULL,
			      &process, &error) == HX_OS_NOT_SUPPORTED);
		CHECK(!process);
		CHECK(hx_os_process_capture_run(NULL, argv[1], NULL, NULL,
			      HX_OS_DEADLINE_NOW, &capture,
			      NULL) == HX_OS_NOT_SUPPORTED);

		return 0;
	}

	{
		hx_os_status missing = hx_os_process_spawn(NULL,
			"hx-process-nonexistent-fixture", NULL, NULL, NULL,
			&process, &error);

#if !defined(_WIN32)
		if (missing == HX_OS_OK) {
			/* POSIX also permits exec failure through child exit
			 * 127. */
			CHECK(process);
			CHECK(hx_os_process_wait_until(process, test_deadline(),
				      &result, NULL) == HX_OS_OK);
			CHECK(result.exit_code == 127 && !result.signal_number);
			CHECK(hx_os_process_destroy(&process, NULL) ==
				HX_OS_OK);
			puts("POSIX startup failure reported by child exit "
			     "127");
		}
		else
#endif
		{
			if (missing != HX_OS_NOT_FOUND)
				fprintf(stderr,
					"missing spawn: status %d native "
					"%u:%lld\n",
					(int)missing, error.domain,
					(long long)error.native_code);
			CHECK(missing == HX_OS_NOT_FOUND);
			CHECK(error.domain != HX_OS_ERROR_NONE);
		}
	}
	CHECK(!process);
	result.exit_code = 99;
	result.signal_number = 99;
	CHECK(hx_os_process_spawn(NULL, argv[1], NULL, duplicates, NULL,
		      &process, NULL) == HX_OS_INVALID);
	CHECK(hx_os_process_spawn(NULL, argv[1], delay, NULL, &options,
		      &process, NULL) == HX_OS_OK);
	CHECK(hx_os_process_identifier(process));
	CHECK(hx_os_process_destroy(&process, NULL) == HX_OS_BUSY && process);
	CHECK(hx_os_process_wait_until(process, HX_OS_DEADLINE_NOW, &result,
		      NULL) == HX_OS_TIMEOUT);
	CHECK(result.exit_code == 99 && result.signal_number == 99);
	CHECK(hx_os_process_close_until(&process, HX_OS_DEADLINE_NOW, NULL) ==
			HX_OS_TIMEOUT &&
		process);
	CHECK(hx_os_process_wait_until(
		      process, test_deadline(), &result, NULL) == HX_OS_OK &&
		result.exit_code == 7 && !result.signal_number);
	CHECK(hx_os_process_wait_until(
		      process, HX_OS_DEADLINE_NOW, &result, NULL) == HX_OS_OK &&
		result.exit_code == 7);
	CHECK(hx_os_process_destroy(&process, NULL) == HX_OS_OK && !process);
	CHECK(hx_os_process_spawn(NULL, argv[1], exit_code, empty_environment,
		      NULL, &process, NULL) == HX_OS_OK);
	CHECK(hx_os_process_wait_until(
		      process, test_deadline(), &result, NULL) == HX_OS_OK);
#if defined(_WIN32)
	CHECK(result.exit_code == 259);
#else
	CHECK(result.exit_code == 3);
#endif
	CHECK(hx_os_process_destroy(&process, NULL) == HX_OS_OK);
	if ((caps & (HX_OS_CAP_ASYNC_PIPE | HX_OS_CAP_LOOP)) !=
		(HX_OS_CAP_ASYNC_PIPE | HX_OS_CAP_LOOP)) {
		CHECK(hx_os_process_capture_run(NULL, argv[1], NULL, NULL,
			      HX_OS_DEADLINE_NOW, &capture,
			      NULL) == HX_OS_NOT_SUPPORTED);

		return 0;
	}

	if (caps & HX_OS_CAP_SYSTEM_UTILS) {
		hx_os_status status = hx_os_process_capture_run(NULL, argv[1],
			arguments, NULL, test_deadline(), &capture, &error);
		if (status != HX_OS_OK)
			fprintf(stderr, "capture: %s, native %u:%lld\n",
				hx_os_status_name(status), error.domain,
				(long long)error.native_code);

		CHECK(status == HX_OS_OK);

		capture_check(capture, 0, expected, strlen(expected));
		capture_check(capture, 1, NULL, 0);
		CHECK(hx_os_process_capture_text(capture, 0, &text, NULL) ==
			HX_OS_OK);
		CHECK(!strcmp(hx_string_c_str(&text), expected));
		CHECK(hx_os_process_capture_close_until(
			      &capture, test_deadline(), NULL) == HX_OS_OK &&
			!capture);
	}

	CHECK(hx_os_process_capture_run(NULL, argv[1], delay, NULL,
		      HX_OS_DEADLINE_NOW, &capture, NULL) == HX_OS_TIMEOUT &&
		capture);
	CHECK(hx_os_process_capture_close_until(
		      &capture, HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT &&
		capture);
	CHECK(hx_os_process_capture_wait_until(
		      capture, test_deadline(), &result, NULL) == HX_OS_OK &&
		result.exit_code == 7);

	capture_check(capture, 0, "finished", 8);
	CHECK(hx_os_process_capture_close_until(
		      &capture, test_deadline(), NULL) == HX_OS_OK);
	CHECK(hx_os_process_capture_run(NULL, argv[1], bulk, NULL,
		      test_deadline(), &capture, NULL) == HX_OS_OK);

	for (i = 0; i < 2; ++i) {
		size_t index;

		CHECK(hx_os_process_capture_view(
			      capture, (int)i, &data, &bytes) == HX_OS_OK &&
			bytes == 80 * 4096);

		for (index = 0; index < bytes; ++index)
			CHECK(((const char *)data)[index] ==
				(int)('a' + (index / 4096) % 26));
	}

	CHECK(hx_os_process_capture_close_until(
		      &capture, test_deadline(), NULL) == HX_OS_OK);

	capture_options.stdout_limit = 512;
	capture_options.stderr_limit = 512;
	CHECK(hx_os_process_capture_run_with_options(NULL, argv[1], bulk, NULL,
		      &capture_options, test_deadline(), &capture,
		      NULL) == HX_OS_LIMIT);
	CHECK(capture);
	CHECK(hx_os_process_capture_close_until(
		      &capture, test_deadline(), NULL) == HX_OS_LIMIT &&
		capture);
	CHECK(hx_os_process_capture_raise_limit(capture, 0, 1024 * 1024) ==
		HX_OS_OK);
	CHECK(hx_os_process_capture_raise_limit(capture, 1, 1024 * 1024) ==
		HX_OS_OK);
	CHECK(hx_os_process_capture_wait_until(
		      capture, test_deadline(), &result, NULL) == HX_OS_OK);
	CHECK(hx_os_process_capture_view(capture, 0, &data, &bytes) ==
			HX_OS_OK &&
		bytes == 80 * 4096);
	CHECK(hx_os_process_capture_close_until(
		      &capture, test_deadline(), NULL) == HX_OS_OK);
	CHECK(hx_os_process_capture_run_with_options(NULL, argv[1], bulk, NULL,
		      &capture_options, test_deadline(), &capture,
		      NULL) == HX_OS_LIMIT);

	/* Both streams can still have an accepted read. Retry discard after
	 * another nonblocking poll if required; retained prefixes stay intact.
	 */
	for (i = 0; i < 2; ++i) {
		hx_os_status status;

		do {
			status = hx_os_process_capture_discard(capture, (int)i);
			if (status == HX_OS_BUSY)
				(void)hx_os_process_capture_wait_until(capture,
					hx_os_deadline_after_ms(1), NULL, NULL);
		} while (status == HX_OS_BUSY);
		CHECK(status == HX_OS_OK);
	}

	CHECK(hx_os_process_capture_close_until(
		      &capture, test_deadline(), NULL) == HX_OS_OK);
	CHECK(hx_os_process_capture_run(NULL, argv[1], binary, NULL,
		      test_deadline(), &capture, NULL) == HX_OS_OK);
	CHECK(hx_string_assign_cstr(&text, "preserved", NULL) == HX_OS_OK);
	CHECK(hx_os_process_capture_text(capture, 0, &text, NULL) ==
		HX_OS_INVALID_ENCODING);
	CHECK(!strcmp(hx_string_c_str(&text), "preserved"));
	CHECK(hx_os_process_capture_text_encoded(
		      capture, 0, HX_OS_ISO_8859_1, &text, NULL) == HX_OS_OK &&
		text.size == 4);
	CHECK(hx_os_process_capture_close_until(
		      &capture, test_deadline(), NULL) == HX_OS_OK);
	CHECK(hx_os_process_capture_run(NULL, argv[1], input, NULL,
		      test_deadline(), &capture, NULL) == HX_OS_OK);
	CHECK(hx_os_process_capture_wait_until(
		      capture, HX_OS_DEADLINE_NOW, &result, NULL) == HX_OS_OK &&
		!result.exit_code);
	CHECK(hx_os_process_capture_close_until(
		      &capture, test_deadline(), NULL) == HX_OS_OK);
	if (caps & HX_OS_CAP_ENV) {
		const char *env[] = {"HX_PROCESS_TEST=中文😀", NULL};
		const char *env_args[] = {"environment", NULL};

		CHECK(hx_os_process_capture_run(NULL, argv[1], env_args, env,
			      test_deadline(), &capture, NULL) == HX_OS_OK);

		capture_check(capture, 0, "中文😀", 10);
		CHECK(hx_os_process_capture_close_until(
			      &capture, test_deadline(), NULL) == HX_OS_OK);
	}

	allocation_failures(argv[1]);

	hx_string_destroy(&text);

	return 0;
}
