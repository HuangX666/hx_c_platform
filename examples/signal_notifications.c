#include <hx_os.h>
#include <stdio.h>
static hx_os_status HX_OS_CALL on_ready(
	const hx_os_calldata *data, void *user, hx_os_error *error)
{
	int64_t items;
	hx_os_status status;
	(void)user;
	status = hx_os_calldata_get_i64(data, "items", &items, error);
	if (status == HX_OS_OK)
		printf("ready: %lld items\n", (long long)items);
	return status;
}
int main(void)
{
	hx_os_signal_handler *signals = NULL;
	hx_os_calldata data = HX_OS_CALLDATA_INIT;
	hx_os_error error;
	hx_os_status status =
		hx_os_signal_handler_create(NULL, &signals, &error);
	if (status == HX_OS_OK)
		status = hx_os_signal_connect(
			signals, "ready", on_ready, NULL, NULL, NULL, &error);
	if (status == HX_OS_OK)
		status = hx_os_calldata_set_i64(&data, "items", 42, &error);
	if (status == HX_OS_OK)
		status = hx_os_signal_emit(
			signals, "ready", &data, NULL, &error);
	if (signals) {
		hx_os_status closed =
			hx_os_signal_handler_destroy(&signals, &error);
		if (status == HX_OS_OK)
			status = closed;
	}
	if (status != HX_OS_OK)
		fprintf(stderr, "%s\n", hx_os_status_name(status));
	return status == HX_OS_OK ? 0 : 1;
}
