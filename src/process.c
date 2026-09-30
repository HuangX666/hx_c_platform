#include "process_private.h"

hx_os_status hx_os_process_options_init(
	hx_os_process_options *options, size_t capacity)
{
	hx_os_process_options value = HX_OS_PROCESS_OPTIONS_INIT;
	if (!options || capacity < sizeof(value))
		return HX_OS_INVALID;
	*options = value;
	return HX_OS_OK;
}
#if HX_OS_ENABLE_PROCESS
static hx_os_status hx_os_process_strings(hx_os_context *context,
	const char *const entries[], const char *first, int environment,
	hx_os_string_list **out, hx_os_error *error)
{
	size_t bytes = first ? strlen(first) + 1 : 0, count = 0, i, offset = 0;
	char *block;
	hx_os_status status;
	if (bytes > HX_OS_I_SYSTEM_BYTES)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	while (entries && entries[count]) {
		size_t length = strlen(entries[count]) + 1;
		if (environment) {
			const char *separator = strchr(entries[count], '=');
			if (!separator || separator == entries[count])
				return hx_os_i_error(
					error, HX_OS_INVALID, 0, 0);
		}
		if (length > HX_OS_I_SYSTEM_BYTES - bytes ||
			count >= HX_OS_I_SYSTEM_ITEMS - (first ? 1u : 0u))
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		++count;
		bytes += length;
	}
	block = hx_os_i_alloc(context, bytes ? bytes : 1, 0);
	if (!block)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	if (first) {
		offset = strlen(first) + 1;
		memcpy(block, first, offset);
	}
	for (i = 0; i < count; ++i) {
		size_t length = strlen(entries[i]) + 1;
		memcpy(block + offset, entries[i], length);
		offset += length;
	}
	status = hx_os_i_strings_from_block(
		context, block, bytes, 1, out, error);
	hx_os_memory_free(block);
	return status;
}
#endif
hx_os_status hx_os_process_spawn(hx_os_context *context, const char *path,
	const char *const arguments[], const char *const environment[],
	const hx_os_process_options *options, hx_os_process **out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_PROCESS
	hx_os_process_options defaults = HX_OS_PROCESS_OPTIONS_INIT;
	hx_os_string_list *args = NULL, *env = NULL;
	hx_os_process *process = NULL;
	char *block = NULL;
	size_t bytes;
	hx_os_status status;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!options)
		options = &defaults;
	if (options->size < sizeof(*options) || options->version != 1)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_path_validate(path, error);
	if (status != HX_OS_OK)
		return status;
	if ((options->standard_input &&
		    !(options->standard_input->flags & HX_OS_FILE_READ)) ||
		(options->standard_output &&
			!(options->standard_output->flags &
				HX_OS_FILE_WRITE)) ||
		(options->standard_error &&
			!(options->standard_error->flags & HX_OS_FILE_WRITE)))
		return hx_os_i_error(error, HX_OS_ACCESS_DENIED, 0, 0);
	status = hx_os_process_strings(
		context, arguments, path, 0, &args, error);
	if (status != HX_OS_OK)
		goto done;
	if (environment)
		status = hx_os_process_strings(
			context, environment, NULL, 1, &env, error);
	else {
		status = hx_os_i_env_snapshot(&block, &bytes, error);
		if (status == HX_OS_OK)
			status = hx_os_i_strings_from_block(
				context, block, bytes, 0, &env, error);
		free(block);
	}
	if (status != HX_OS_OK)
		goto done;
	process = hx_os_i_zero(context, sizeof(*process));
	if (!process) {
		status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		goto done;
	}
	status = hx_os_i_process_spawn(
		context, path, args, env, options, process, error);
	if (status == HX_OS_OK) {
		*out = process;
		process = NULL;
	}
done:
	hx_os_memory_free(process);
	hx_os_string_list_destroy(&args);
	hx_os_string_list_destroy(&env);
	return status;
#else
	(void)context;
	(void)path;
	(void)arguments;
	(void)environment;
	(void)options;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_process_wait_until(hx_os_process *process,
	hx_os_deadline deadline, hx_os_process_result *result,
	hx_os_error *error)
{
#if HX_OS_ENABLE_PROCESS
	hx_os_status status;
	if (!process)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	for (;;) {
		uint64_t now;
		hx_os_deadline wake;
		status = hx_os_i_process_poll(process, error);
		if (status != HX_OS_WOULD_BLOCK)
			break;
		status = hx_os_monotonic_read(&now, error);
		if (status != HX_OS_OK)
			return status;
		if (deadline != HX_OS_DEADLINE_INFINITE && now >= deadline)
			return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
		wake = hx_os_deadline_after_ms(5);
		if (wake > deadline)
			wake = deadline;
		status = hx_os_sleep_until(wake, error);
		if (status != HX_OS_OK)
			return status;
	}
	if (status == HX_OS_OK && result)
		*result = process->result;
	return status;
#else
	(void)process;
	(void)deadline;
	(void)result;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
uint64_t hx_os_process_identifier(const hx_os_process *process)
{
	return process ? process->identifier : 0;
}
hx_os_status hx_os_process_destroy(hx_os_process **process, hx_os_error *error)
{
#if HX_OS_ENABLE_PROCESS
	hx_os_status status;
	if (!process)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!*process)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	status = hx_os_i_process_poll(*process, error);
	if (status == HX_OS_WOULD_BLOCK)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_i_process_dispose(*process, error);
	if (status == HX_OS_OK) {
		hx_os_memory_free(*process);
		*process = NULL;
	}
	return status;
#else
	(void)process;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_process_close_until(
	hx_os_process **process, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status;
	if (!process)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!*process)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	status = hx_os_process_wait_until(*process, deadline, NULL, error);
	return status == HX_OS_OK ? hx_os_process_destroy(process, error)
				  : status;
}
