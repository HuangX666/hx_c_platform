#include "power_private.h"
#if HX_OS_I_HAVE_SLEEP_INHIBIT ||                                              \
	(HX_OS_ENABLE_PERFORMANCE_REQUEST &&                                   \
		(defined(_WIN32) || defined(__APPLE__)))
static hx_os_status hx_os_power_init(hx_os_i_power *power,
	hx_os_context *context, const char *reason, int kind,
	hx_os_error *error)
{
	hx_os_status status;

	if (!reason || !reason[0])
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (strlen(reason) > 4096)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	hx_string_init(&power->reason, context);
	power->kind = kind;

	status = hx_string_assign_cstr(&power->reason, reason, error);
	if (status == HX_OS_OK)
		status = hx_os_i_power_open(power, error);

	if (status == HX_OS_OK) {
		status = hx_os_i_power_set(power, 1, error);
		if (status == HX_OS_OK)
			power->active = 1;
		else
			(void)hx_os_i_power_dispose(power, NULL);
	}

	if (status != HX_OS_OK)
		hx_string_destroy(&power->reason);

	return status;
}

static hx_os_status hx_os_power_clear(hx_os_i_power *power, hx_os_error *error)
{
	hx_os_status status = hx_os_i_power_dispose(power, error);
	hx_string_destroy(&power->reason);

	return status;
}
#endif
hx_os_status hx_os_sleep_inhibit_create(hx_os_context *context,
	const char *reason, hx_os_sleep_inhibit **out, hx_os_error *error)
{
#if HX_OS_I_HAVE_SLEEP_INHIBIT
	hx_os_sleep_inhibit *value;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	value = hx_os_i_zero(context, sizeof(*value));
	if (!value)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	status = hx_os_power_init(
		&value->power, context, reason, HX_OS_I_SLEEP_REQUEST, error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(value);

		return status;
	}

	*out = value;

	return HX_OS_OK;
#else
	(void)context;
	(void)reason;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_sleep_inhibit_set_active(
	hx_os_sleep_inhibit *inhibit, int active, hx_os_error *error)
{
#if HX_OS_I_HAVE_SLEEP_INHIBIT
	hx_os_status status;

	if (!inhibit || (active != 0 && active != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (inhibit->power.active == active)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	status = hx_os_i_power_set(&inhibit->power, active, error);
	if (status == HX_OS_OK)
		inhibit->power.active = active;

	return status;
#else
	(void)inhibit;
	(void)active;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

int hx_os_sleep_inhibit_active(const hx_os_sleep_inhibit *inhibit)
{
	return inhibit ? inhibit->power.active : 0;
}

hx_os_status hx_os_sleep_inhibit_destroy(
	hx_os_sleep_inhibit **inhibit, hx_os_error *error)
{
#if HX_OS_I_HAVE_SLEEP_INHIBIT
	hx_os_status status;

	if (!inhibit)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!*inhibit)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	status = hx_os_power_clear(&(*inhibit)->power, error);

	hx_os_memory_free(*inhibit);
	*inhibit = NULL;

	return status;
#else
	(void)inhibit;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_performance_request(hx_os_context *context,
	const char *reason, hx_os_performance_token **out, hx_os_error *error)
{
#if HX_OS_ENABLE_PERFORMANCE_REQUEST && (defined(_WIN32) || defined(__APPLE__))
	hx_os_performance_token *value;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	value = hx_os_i_zero(context, sizeof(*value));
	if (!value)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	status = hx_os_power_init(&value->power, context, reason,
		HX_OS_I_PERFORMANCE_REQUEST, error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(value);

		return status;
	}

	*out = value;

	return HX_OS_OK;
#else
	(void)context;
	(void)reason;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_performance_release(
	hx_os_performance_token **token, hx_os_error *error)
{
#if HX_OS_ENABLE_PERFORMANCE_REQUEST && (defined(_WIN32) || defined(__APPLE__))
	hx_os_status status;

	if (!token)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!*token)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	status = hx_os_power_clear(&(*token)->power, error);

	hx_os_memory_free(*token);
	*token = NULL;

	return status;
#else
	(void)token;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
