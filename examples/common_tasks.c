#include <hx_os.h>
#include <stdio.h>

/* A typed POD vector still uses the same bounded C container underneath. */
HX_OS_DECLARE_VECTOR(sample_values, int)

static hx_os_status HX_OS_CALL twice(
	hx_os_calldata *data, void *user, hx_os_error *error)
{
	int64_t value;
	hx_os_status status;

	(void)user;

	status = hx_os_calldata_get_i64(data, "input", &value, error);

	return status == HX_OS_OK
		? hx_os_calldata_set_i64(data, "output", value * 2, error)
		: status;
}

static hx_os_status HX_OS_CALL tick(void *user, hx_os_error *error)
{
	unsigned *count = user;

	(void)error;
	printf("tick %u\n", ++*count);

	return HX_OS_OK;
}

int main(void)
{
	hx_string text = HX_STRING_INIT;
	hx_string_parts *parts = NULL;
	hx_os_proc_handler *procedures = NULL;
	hx_os_calldata data = HX_OS_CALLDATA_INIT;
	hx_os_error error;
	hx_os_status status;
	uint64_t interval = 0;
	int64_t result = 0;
	unsigned count = 0;

	status = hx_string_assign_cstr(&text, " first,second,中文 ", &error);
	if (status == HX_OS_OK) {
		hx_string_trim_ascii_in_place(&text);

		status = hx_string_split(
			&text, hx_string_view_from_cstr(","), &parts, &error);
	}

	if (status == HX_OS_OK)
		printf("pieces: %lu\n",
			(unsigned long)hx_string_parts_count(parts));

	if (status == HX_OS_OK &&
		(hx_os_capabilities() & HX_OS_CAP_WIDE_MATH)) {
		status = hx_os_muldiv_u64(1001, 1000000000, 30000,
			HX_OS_ROUND_NEAREST, &interval, &error);
		if (status == HX_OS_OK)
			printf("frame interval: %lu ns\n",
				(unsigned long)interval);
	}

	if (status == HX_OS_OK &&
		(hx_os_capabilities() & HX_OS_CAP_PROCEDURES)) {
		status = hx_os_proc_create(NULL, &procedures, &error);
		if (status == HX_OS_OK)
			status = hx_os_proc_register(
				procedures, "twice", twice, NULL, &error);

		if (status == HX_OS_OK)
			status = hx_os_calldata_set_i64(
				&data, "input", 21, &error);

		if (status == HX_OS_OK)
			status = hx_os_proc_call(
				procedures, "twice", &data, &error);

		if (status == HX_OS_OK)
			status = hx_os_calldata_get_i64(
				&data, "output", &result, &error);

		if (status == HX_OS_OK)
			printf("procedure result: %ld\n", (long)result);
	}

	/* Synchronous periodic work needs no event-loop setup. */
	if (status == HX_OS_OK)
		status = hx_os_repeat(1, 2, tick, &count, &error);

	if (procedures)
		(void)hx_os_proc_destroy(&procedures, NULL);

	hx_string_parts_destroy(&parts);
	hx_string_destroy(&text);
	if (status != HX_OS_OK)
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	return status == HX_OS_OK ? 0 : 1;
}
