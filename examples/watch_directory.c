#include <hx_os.h>
#include <stdio.h>

static void changed(
	hx_os_watch *watch, const hx_os_watch_event *event, void *user)
{
	(void)watch;
	(void)user;
	if (event->kind == HX_OS_WATCH_OVERFLOW)
		puts("Notifications overflowed; rescan the directory.");
	else if (event->kind == HX_OS_WATCH_ERROR)
		fprintf(stderr, "Watch stopped: %s\n",
			hx_os_status_name(event->error.status));
	else
		printf("%u: %s\n", event->kind, event->path);
}
int main(int argc, char **argv)
{
	hx_os_watch *watch = NULL;
	hx_os_error error;
	hx_os_status status;
	char owned_path[128];
	const char *path;
	if (argc != 1 && argc != 2)
		return 1;
	if (argc == 1) {
#if defined(_WIN32)
		const char *pattern = "hx-watch-example-%llx";
#else
		const char *pattern = "/tmp/hx-watch-example-%llx";
#endif
		(void)snprintf(owned_path, sizeof(owned_path), pattern,
			(unsigned long long)hx_os_monotonic_ns());
		if (hx_os_directory_create(owned_path, 0, &error) != HX_OS_OK)
			return 1;
		path = owned_path;
	}
	else
		path = argv[1];
	status = hx_os_watch_create(
		NULL, path, 1, changed, NULL, &watch, &error);
	if (status != HX_OS_OK) {
		if (argc == 1)
			(void)hx_os_directory_remove(owned_path, NULL);
		return 1;
	}
	(void)hx_os_sleep_until(hx_os_deadline_after_ms(250), NULL);
	status = hx_os_watch_close_until(
		&watch, hx_os_deadline_after_ms(2000), &error);
	if (argc == 1 && !watch)
		(void)hx_os_directory_remove(owned_path, NULL);
	return status == HX_OS_OK ? 0 : 1;
}
