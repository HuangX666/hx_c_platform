#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_string version = HX_STRING_INIT;
	hx_os_cpu_usage *cpu = NULL;
	hx_os_error error;
	double percent;

	hx_os_status status = hx_os_sys_version_string(&version, &error);
	if (status == HX_OS_OK) {
		puts(hx_string_c_str(&version));

		status = hx_os_cpu_usage_create(NULL, &cpu, &error);
	}

	if (status == HX_OS_OK)
		status =
			hx_os_sleep_until(hx_os_deadline_after_ms(100), &error);

	if (status == HX_OS_OK)
		status = hx_os_cpu_usage_query(cpu, &percent, &error);

	if (status == HX_OS_OK)
		printf("Process CPU: %.2f%%\n", percent);
	else
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	hx_os_cpu_usage_destroy(&cpu);
	hx_string_destroy(&version);

	return status == HX_OS_OK ? 0 : 1;
}
