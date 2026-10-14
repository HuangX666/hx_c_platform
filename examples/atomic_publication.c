#include "hx_os.h"
#include <stdio.h>

typedef struct shared_result {
	hx_os_atomic32 ready;
	int answer;
} shared_result;

static void *calculate(void *user)
{
	shared_result *result = user;

	result->answer = 42;
	hx_os_atomic32_store(&result->ready, 1, HX_OS_RELEASE);

	return NULL;
}

int main(void)
{
	shared_result result = {HX_OS_ATOMIC32_INIT(0), 0};

	hx_os_thread *thread = NULL;
	hx_os_status status =
		hx_os_thread_create(NULL, calculate, &result, &thread, NULL);

	if (status != HX_OS_OK)
		return 1;

	// For a long wait use an event/condition, not a busy loop.
	while (!hx_os_atomic32_load(&result.ready, HX_OS_ACQUIRE))
		hx_os_cpu_relax();

	printf("answer: %d\n", result.answer);

	status = hx_os_thread_join_until(
		thread, HX_OS_DEADLINE_INFINITE, NULL, NULL);
	if (status == HX_OS_OK)
		status = hx_os_thread_destroy(&thread, NULL);

	return status == HX_OS_OK && result.answer == 42 ? 0 : 1;
}
