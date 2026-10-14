#include <hx_os.h>
#include <stdio.h>

static hx_os_status HX_OS_CALL on_signal(const char *name,
	const hx_os_signal_arguments *arguments, void *user, hx_os_error *error)
{
	int64_t items;
	hx_os_status status;

	(void)user;
	status = hx_os_signal_arguments_get_i64(
		arguments, "items", &items, error);
	if (status == HX_OS_OK)
		printf("%s: %lld items\n", name, (long long)items);

	return status;
}

int main(void)
{
	hx_os_signal_handler *signals = NULL;
	hx_os_calldata data = HX_OS_CALLDATA_INIT;
	hx_os_status status = hx_os_signal_handler_create(NULL, &signals, NULL);

	if (status == HX_OS_OK)
		status = hx_os_signal_connect_all(
			signals, on_signal, NULL, NULL, NULL, NULL);
	if (status == HX_OS_OK)
		status = hx_os_calldata_set_i64(&data, "items", 42, NULL);

	if (status == HX_OS_OK)
		status = hx_os_signal_emit(signals, "ready", &data, NULL, NULL);
	if (status == HX_OS_OK)
		status = hx_os_signal_emit(signals, "saved", &data, NULL, NULL);

	// This example has no concurrent emitters or retained callbacks.
	if (signals) {
		hx_os_status closed = hx_os_signal_handler_close_until(
			&signals, HX_OS_DEADLINE_NOW, NULL);

		if (status == HX_OS_OK)
			status = closed;
	}
	if (status != HX_OS_OK)
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	return status == HX_OS_OK ? 0 : 1;
}
