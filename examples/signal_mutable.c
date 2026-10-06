#include <hx_os.h>
#include <stdio.h>

static hx_os_status HX_OS_CALL limit_volume(const char *signal,
	hx_os_params *parameters, void *user, hx_os_error *error)
{
	int64_t volume;
	hx_os_status status;

	(void)signal;
	(void)user;
	status = hx_os_params_get_i64(parameters, "volume", &volume, error);
	if (status != HX_OS_OK)
		return status;
	if (volume > 60)
		volume = 60;

	return hx_os_params_set_i64(parameters, "volume", volume, error);
}

static hx_os_status HX_OS_CALL display_volume(const char *signal,
	const hx_os_signal_arguments *arguments, void *user, hx_os_error *error)
{
	int64_t volume;
	hx_os_status status;

	(void)user;
	status = hx_os_signal_arguments_get_i64(
		arguments, "volume", &volume, error);
	if (status == HX_OS_OK)
		printf("Observer: %s = %lld\n", signal, (long long)volume);

	return status;
}

int main(void)
{
	hx_os_signal_handler *handler = NULL;
	hx_os_params *parameters = NULL;
	hx_os_status status;
	int64_t volume;

	status = hx_os_signal_handler_create(NULL, &handler, NULL);
	if (status == HX_OS_OK)
		status = hx_os_params_create(NULL, &parameters, NULL);
	if (status == HX_OS_OK)
		status = hx_os_params_set_i64(parameters, "volume", 80, NULL);

	if (status == HX_OS_OK)
		status = hx_os_signal_connect_mutable(handler, "volume",
			limit_volume, NULL, NULL, NULL, NULL);
	if (status == HX_OS_OK)
		status = hx_os_signal_connect_all(
			handler, display_volume, NULL, NULL, NULL, NULL);
	if (status == HX_OS_OK)
		status = hx_os_signal_emit_params(
			handler, "volume", parameters, NULL, NULL);
	if (status == HX_OS_OK)
		status = hx_os_params_get_i64(
			parameters, "volume", &volume, NULL);
	if (status == HX_OS_OK)
		printf("Caller: volume = %lld\n", (long long)volume);

	if (handler) {
		hx_os_status closed = hx_os_signal_handler_close_until(
			&handler, HX_OS_DEADLINE_NOW, NULL);

		if (status == HX_OS_OK)
			status = closed;
	}
	(void)hx_os_params_destroy(&parameters, NULL);
	if (status != HX_OS_OK)
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	return status == HX_OS_OK ? 0 : 1;
}
