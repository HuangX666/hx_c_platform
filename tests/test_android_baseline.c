#include "utility_test.h"
typedef struct baseline_state {
	hx_os_tls_key *key;
	hx_os_event *entered, *release;
	hx_os_atomic destroyed;
} baseline_state;
static void HX_OS_CALL thread_cleanup(void *value, void *user)
{
	baseline_state *state = user;
	CHECK(value == state);
	hx_os_atomic_store(&state->destroyed, 1, HX_OS_RELEASE);
}
static void *HX_OS_CALL thread_main(void *user)
{
	baseline_state *state = user;
	CHECK(hx_os_tls_set(state->key, state, NULL) == HX_OS_OK);
	hx_os_event_set(state->entered);
	CHECK(hx_os_event_wait_until(state->release,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	return user;
}
static void test_thread_baseline(void)
{
	baseline_state state = {0};
	hx_os_thread *thread = NULL;
	void *result = NULL;
	hx_os_status status;
	CHECK(hx_os_tls_key_create(NULL, thread_cleanup, &state, &state.key,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.entered, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.release, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_create(NULL, thread_main, &state, &thread, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.entered,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	status =
		hx_os_thread_join_until(thread, HX_OS_DEADLINE_NOW, NULL, NULL);
	CHECK(status ==
		((hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN)
				? HX_OS_TIMEOUT
				: HX_OS_NOT_SUPPORTED));
	hx_os_event_set(state.release);
	CHECK(hx_os_thread_join_until(thread, HX_OS_DEADLINE_INFINITE, &result,
		      NULL) == HX_OS_OK &&
		result == &state);
	CHECK(hx_os_atomic_load(&state.destroyed, HX_OS_ACQUIRE) == 1);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	CHECK(hx_os_tls_key_destroy(&state.key, NULL) == HX_OS_OK);
	hx_os_event_destroy(state.entered);
	hx_os_event_destroy(state.release);
}
static void test_file_baseline(void)
{
	hx_os_file *file = NULL;
	const char *path = "hx_android_baseline_中文😀.tmp";
	char buffer[16] = {0};
	size_t bytes;
	CHECK(hx_os_file_open(NULL, path,
		      HX_OS_FILE_READ | HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
			      HX_OS_FILE_EXCLUSIVE,
		      &file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_write_all(
		      file, "中文", strlen("中文"), &bytes, NULL) == HX_OS_OK &&
		bytes == strlen("中文"));
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_open(NULL, path, HX_OS_FILE_READ, &file, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_read_some(
		      file, buffer, sizeof(buffer), &bytes, NULL) == HX_OS_OK &&
		bytes == strlen("中文"));
	CHECK(!memcmp(buffer, "中文", bytes));
	CHECK(hx_os_file_read_some(file, buffer, sizeof(buffer), &bytes,
		      NULL) == HX_OS_EOF &&
		!bytes);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
}
int main(void)
{
	printf("platform=%s cpu=%s pointer_bits=%u\n", hx_os_platform_name(),
		hx_os_cpu_name(), (unsigned)(sizeof(void *) * 8));
#if defined(__ANDROID__)
	printf("android_build_api=%d (runtime API is recorded by adb runner)\n",
		__ANDROID_API__);
	CHECK(!strcmp(hx_os_platform_name(), "Android"));
	CHECK(!(hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN));
#if __ANDROID_API__ < 28
	CHECK(!(hx_os_capabilities() & HX_OS_CAP_PROCESS));
#endif
	{
		hx_string path = HX_STRING_INIT;
		CHECK(hx_os_path_config_home_string(&path, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(!path.size);
	}
#endif
	test_thread_baseline();
	test_file_baseline();
	return 0;
}
