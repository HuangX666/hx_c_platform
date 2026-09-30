#if !defined(_WIN32)
#define _GNU_SOURCE
#endif
#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

typedef struct thread_test {
	hx_os_semaphore *ready;
	hx_os_semaphore *release;
	hx_os_status name_status;
	uint64_t affinity;
} thread_test;
#if defined(__linux__)
static void test_restricted_affinity(const cpu_set_t *available, int selected)
{
	cpu_set_t actual;
	hx_os_error error;
	hx_os_status status;
	int unavailable;
	for (unavailable = 0; unavailable < 64; ++unavailable)
		if (!CPU_ISSET(unavailable, available))
			break;
	if (unavailable == 64)
		return;
	status = hx_os_thread_set_current_affinity(
		(UINT64_C(1) << selected) | (UINT64_C(1) << unavailable),
		&error);
	CHECK(status == HX_OS_NOT_SUPPORTED || status == HX_OS_INVALID);
	CHECK(error.cause_code == 0);
	CHECK(!pthread_getaffinity_np(pthread_self(), sizeof(actual), &actual));
	CHECK(CPU_COUNT(&actual) == 1 && CPU_ISSET(selected, &actual));
}
#endif

static void *thread_entry(void *user)
{
	thread_test *state = user;
	CHECK(hx_os_thread_set_current_priority(
		      HX_OS_THREAD_PRIORITY_NORMAL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_set_current_priority(99, NULL) == HX_OS_INVALID);
	CHECK(hx_os_thread_set_current_affinity(0, NULL) == HX_OS_INVALID);
#if defined(__linux__)
	{
		cpu_set_t available, actual;
		int i;
		CHECK(!pthread_getaffinity_np(
			pthread_self(), sizeof(available), &available));
		for (i = 0; i < 64; ++i)
			if (CPU_ISSET(i, &available)) {
				state->affinity = UINT64_C(1) << i;
				break;
			}
		if (state->affinity) {
			CHECK(hx_os_thread_set_current_affinity(
				      state->affinity, NULL) == HX_OS_OK);
			CHECK(!pthread_getaffinity_np(
				pthread_self(), sizeof(actual), &actual));
			CHECK(CPU_COUNT(&actual) == 1 && CPU_ISSET(i, &actual));
			test_restricted_affinity(&available, i);
		}
	}
#elif defined(_WIN32)
	{
		DWORD_PTR process_mask, system_mask;
		CHECK(GetProcessAffinityMask(
			GetCurrentProcess(), &process_mask, &system_mask));
		state->affinity =
			(uint64_t)(process_mask & (~process_mask + 1));
		CHECK(hx_os_thread_set_current_affinity(
			      state->affinity, NULL) == HX_OS_OK);
	}
#else
	CHECK(hx_os_thread_set_current_affinity(1, NULL) ==
		HX_OS_NOT_SUPPORTED);
#endif
	state->name_status =
		hx_os_thread_set_current_name("中文测试名称超过限制", NULL);
#if defined(__linux__)
	{
		char name[64];
		size_t invalid, stack_size;
		void *stack;
		pthread_attr_t attributes;
		CHECK(pthread_getname_np(pthread_self(), name, sizeof(name)) ==
			0);
		CHECK(strlen(name) == 15);
		CHECK(hx_os_text_validate_utf8(name, strlen(name), &invalid) ==
			HX_OS_OK);
		CHECK(pthread_getattr_np(pthread_self(), &attributes) == 0);
		CHECK(pthread_attr_getstack(&attributes, &stack, &stack_size) ==
			0);
		CHECK(stack_size >= 2 * 1024 * 1024);
		CHECK(pthread_attr_destroy(&attributes) == 0);
	}
#endif
	CHECK(hx_os_semaphore_post(state->ready, 1, NULL) == HX_OS_OK);
	CHECK(hx_os_semaphore_wait_until(state->release,
		      HX_OS_DEADLINE_INFINITE, NULL) == HX_OS_OK);
	return state;
}

int main(void)
{
	hx_os_thread *thread = NULL;
	hx_os_thread_options options = HX_OS_THREAD_OPTIONS_INIT;
	struct {
		hx_os_thread_options value;
		unsigned char canary[8];
	} storage;
	thread_test state;
	void *result = NULL;
	hx_os_status status;
	memset(&storage, 0xa5, sizeof(storage));
	CHECK(hx_os_thread_options_init(&storage.value,
		      sizeof(storage.value) - 1) == HX_OS_INVALID);
	CHECK(((unsigned char *)&storage.value)[0] == 0xa5);
	CHECK(hx_os_thread_options_init(&storage.value, sizeof(storage)) ==
		HX_OS_OK);
	CHECK(storage.canary[0] == 0xa5 && storage.canary[7] == 0xa5);
	CHECK(storage.value.stack_size == 0 && storage.value.version == 1);
	memset(&state, 0, sizeof(state));
	if (!(hx_os_capabilities() & HX_OS_CAP_THREAD_EX)) {
		CHECK(hx_os_thread_create_with_options(NULL, &options,
			      thread_entry, &state, &thread,
			      NULL) == HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_thread_set_current_name("disabled", NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_thread_set_current_priority(
			      HX_OS_THREAD_PRIORITY_NORMAL, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_thread_set_current_affinity(1, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_semaphore_create(NULL, 0, 1, &state.ready, NULL) ==
			HX_OS_NOT_SUPPORTED);
		return 0;
	}
	CHECK(hx_os_semaphore_create(NULL, 2, 1, &state.ready, NULL) ==
		HX_OS_INVALID);
	CHECK(hx_os_semaphore_create(NULL, 0, 1, &state.ready, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_semaphore_create(NULL, 0, 2, &state.release, NULL) ==
		HX_OS_OK);
	options.version = 99;
	CHECK(hx_os_thread_create_with_options(NULL, &options, thread_entry,
		      &state, &thread, NULL) == HX_OS_INVALID &&
		!thread);
	options.version = 1;
#if !defined(_WIN32)
	options.stack_size = 1;
	CHECK(hx_os_thread_create_with_options(NULL, &options, thread_entry,
		      &state, &thread, NULL) == HX_OS_INVALID &&
		!thread);
#else
	if (sizeof(size_t) > sizeof(unsigned int)) {
		options.stack_size = SIZE_MAX;
		CHECK(hx_os_thread_create_with_options(NULL, &options,
			      thread_entry, &state, &thread,
			      NULL) == HX_OS_RANGE &&
			!thread);
	}
#endif
	options.stack_size = 2 * 1024 * 1024;
	CHECK(hx_os_thread_create_with_options(NULL, &options, thread_entry,
		      &state, &thread, NULL) == HX_OS_OK);
	CHECK(hx_os_semaphore_wait_until(state.ready,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	CHECK(state.name_status == HX_OS_OK ||
		state.name_status == HX_OS_NOT_SUPPORTED);
	CHECK(hx_os_thread_set_priority(
		      thread, HX_OS_THREAD_PRIORITY_NORMAL, NULL) == HX_OS_OK);
	if (state.affinity)
		CHECK(hx_os_thread_set_affinity(thread, state.affinity, NULL) ==
			HX_OS_OK);
	CHECK(hx_os_thread_set_affinity(thread, 0, NULL) == HX_OS_INVALID);
	{
		hx_os_error scheduling;
		status = hx_os_thread_set_priority(thread,
			HX_OS_THREAD_PRIORITY_TIME_CRITICAL, &scheduling);
		CHECK(status == HX_OS_OK || status == HX_OS_ACCESS_DENIED ||
			status == HX_OS_NOT_SUPPORTED);
		if (status == HX_OS_OK)
			CHECK(hx_os_thread_set_priority(thread,
				      HX_OS_THREAD_PRIORITY_NORMAL,
				      NULL) == HX_OS_OK);
		else
			CHECK(scheduling.domain != HX_OS_ERROR_NONE ||
				status == HX_OS_NOT_SUPPORTED);
	}
	status = hx_os_thread_set_name(thread, "other name", NULL);
#if defined(__APPLE__)
	CHECK(status == HX_OS_NOT_SUPPORTED);
#else
	CHECK(status == state.name_status);
#endif
	CHECK(hx_os_thread_set_name(thread, "\xff", NULL) ==
		HX_OS_INVALID_ENCODING);
	CHECK(hx_os_thread_join_until(thread, HX_OS_DEADLINE_NOW, NULL, NULL) ==
		HX_OS_TIMEOUT);
	CHECK(hx_os_semaphore_wait_until(state.ready,
		      hx_os_deadline_after_ms(5), NULL) == HX_OS_TIMEOUT);
	CHECK(hx_os_semaphore_post(state.release, 2, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(2000),
		      &result, NULL) == HX_OS_OK &&
		result == &state);
	CHECK(hx_os_thread_set_name(thread, "closed", NULL) == HX_OS_CLOSED);
	CHECK(hx_os_thread_set_priority(thread, HX_OS_THREAD_PRIORITY_NORMAL,
		      NULL) == HX_OS_CLOSED);
	CHECK(hx_os_thread_set_affinity(thread, 1, NULL) == HX_OS_CLOSED);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	CHECK(hx_os_semaphore_post(state.release, 2, NULL) == HX_OS_LIMIT);
	CHECK(hx_os_semaphore_wait_until(
		      state.release, HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
	CHECK(hx_os_semaphore_wait_until(state.release, HX_OS_DEADLINE_NOW,
		      NULL) == HX_OS_TIMEOUT);
	CHECK(hx_os_semaphore_post(state.release, 0, NULL) == HX_OS_INVALID);
	hx_os_semaphore_destroy(state.release);
	hx_os_semaphore_destroy(state.ready);
	puts("thread extension contracts passed");
	return 0;
}
