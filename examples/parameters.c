#include <hx_os.h>
#include <stdio.h>

static hx_os_status fill_request(hx_os_params *params)
{
	const unsigned char frame[] = {0x00, 0xff, 0x10, 0x80};
	hx_os_params_edit edit = hx_os_params_edit_begin(params);

	hx_os_params_edit_set_u64(&edit, "device_id", 42);
	hx_os_params_edit_set_cstr(&edit, "name", "camera");
	hx_os_params_edit_set_bool(&edit, "enabled", 1);
	hx_os_params_edit_set_bytes(&edit, "frame", frame, sizeof(frame));

	// Strings and bytes have been copied; local frame storage can expire.
	return hx_os_params_edit_finish(&edit, NULL);
}

static hx_os_status show_request(const hx_os_params *params)
{
	uint64_t device_id;
	hx_string_view name;
	hx_os_bytes_view frame;
	hx_os_status status =
		hx_os_params_get_u64(params, "device_id", &device_id, NULL);

	if (status == HX_OS_OK)
		status = hx_os_params_get_string(params, "name", &name, NULL);
	if (status == HX_OS_OK)
		status = hx_os_params_get_bytes(params, "frame", &frame, NULL);
	if (status != HX_OS_OK)
		return status;

	printf("Device %llu (%.*s): %zu binary bytes\n",
		(unsigned long long)device_id, (int)name.size, name.data,
		frame.size);

	return HX_OS_OK;
}

int main(void)
{
	hx_os_params *params = NULL;
	hx_os_status status = hx_os_params_create(NULL, &params, NULL);

	if (status == HX_OS_OK)
		status = fill_request(params);
	if (status == HX_OS_OK)
		status = show_request(params);

	(void)hx_os_params_destroy(&params, NULL);
	if (status != HX_OS_OK)
		fprintf(stderr, "Parameters failed: %s\n",
			hx_os_status_name(status));

	return status == HX_OS_OK ? 0 : 1;
}
