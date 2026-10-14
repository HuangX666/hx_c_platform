#include <hx_os.h>
#include <stdio.h>

typedef struct shared_settings {
	hx_os_once *once;
	int port;
} shared_settings;

static hx_os_status initialize(void *user, hx_os_error *error)
{
	shared_settings *settings = user;

	(void)error;
	settings->port = 8080;
	puts("Settings initialized once");
	return HX_OS_OK;
}

static void *worker(void *user)
{
	shared_settings *settings = user;
	hx_os_status status =
		hx_os_once_run(settings->once, initialize, settings, NULL);

	if (status == HX_OS_OK)
		printf("Worker uses port %d\n", settings->port);
	return NULL;
}

int main(void)
{
	shared_settings settings = {NULL, 0};
	hx_os_thread *threads[4] = {NULL};
	hx_os_status status = hx_os_once_create(NULL, &settings.once, NULL);
	size_t started = 0, i;

	while (status == HX_OS_OK && started < 4) {
		status = hx_os_thread_create(
			NULL, worker, &settings, &threads[started], NULL);
		if (status == HX_OS_OK)
			++started;
	}
	for (i = 0; i < started; ++i) {
		hx_os_status joined = hx_os_thread_join_until(
			threads[i], HX_OS_DEADLINE_INFINITE, NULL, NULL);

		if (joined != HX_OS_OK)
			return 1;
		(void)hx_os_thread_destroy(&threads[i], NULL);
	}
	(void)hx_os_once_destroy(&settings.once, NULL);
	return status == HX_OS_OK ? 0 : 1;
}
