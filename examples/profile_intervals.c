#include <hx_os.h>
#include <stdio.h>
int main(void)
{
	hx_os_profiler *profiler = NULL;
	hx_string text = HX_STRING_INIT;
	hx_os_error error;
	hx_os_status status = hx_os_profiler_create(NULL, &profiler, &error);
	if (status == HX_OS_OK)
		status = hx_os_profiler_start_root(profiler, "frame", &error);
	if (status == HX_OS_OK)
		status = hx_os_profiler_reach(profiler, "work", &error);
	if (status == HX_OS_OK)
		status = hx_os_profiler_leave(profiler, &error);
	if (status == HX_OS_OK)
		status = hx_os_profiler_leave(profiler, &error);
	if (status == HX_OS_OK)
		status = hx_os_profiler_dump_string(profiler, &text, &error);
	if (status == HX_OS_OK)
		fputs(hx_string_c_str(&text), stdout);
	if (profiler) {
		hx_os_status closed;
		(void)hx_os_profiler_restart_thread(profiler, NULL);
		closed = hx_os_profiler_destroy(&profiler, NULL);
		if (status == HX_OS_OK)
			status = closed;
	}
	hx_string_destroy(&text);
	if (status != HX_OS_OK)
		fprintf(stderr, "%s\n", hx_os_status_name(status));
	return status == HX_OS_OK ? 0 : 1;
}
