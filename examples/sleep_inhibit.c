#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_os_sleep_inhibit *request = NULL;
	hx_os_status status, released;

	status = hx_os_sleep_inhibit_create(
		NULL, "Finishing a file export", &request, NULL);
	if (status != HX_OS_OK) {
		fprintf(stderr, "Sleep request: %s\n",
			hx_os_status_name(status));

		return status == HX_OS_NOT_SUPPORTED ? 0 : 1;
	}

	puts("Sleep inhibition requested for this work interval.");
	// Replace this short demonstration interval with application work.
	status = hx_os_sleep_until(hx_os_deadline_after_ms(20), NULL);
	released = hx_os_sleep_inhibit_destroy(&request, NULL);

	if (status != HX_OS_OK || released != HX_OS_OK)
		return 1;

	puts("The request has been released.");

	return 0;
}
