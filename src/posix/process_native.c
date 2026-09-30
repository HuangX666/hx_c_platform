#include "process_private.h"
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

static int hx_os_process_env_compare(const void *left, const void *right)
{
	return strcmp(*(const char *const *)left, *(const char *const *)right);
}
hx_os_status hx_os_i_process_spawn(hx_os_context *context, const char *path,
	const hx_os_string_list *arguments,
	const hx_os_string_list *environment,
	const hx_os_process_options *options, hx_os_process *process,
	hx_os_error *error)
{
	posix_spawn_file_actions_t actions;
	int descriptors[3] = {-1, -1, -1}, code, i;
	hx_os_file *streams[3] = {options->standard_input,
		options->standard_output, options->standard_error};
	char **env = hx_os_i_alloc(
		context, (environment->count + 1) * sizeof(*env), 0);
	pid_t identifier;
	size_t index;
	if (!env)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	memcpy(env, environment->items,
		(environment->count + 1) * sizeof(*env));
	qsort(env, environment->count, sizeof(*env), hx_os_process_env_compare);
	for (index = 1; index < environment->count; ++index) {
		const char *separator = strchr(env[index - 1], '=');
		if (separator &&
			!strncmp(env[index - 1], env[index],
				(size_t)(separator - env[index - 1]) + 1)) {
			hx_os_memory_free(env);
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		}
	}
	code = posix_spawn_file_actions_init(&actions);
	if (code) {
		hx_os_memory_free(env);
		return hx_os_i_errno(error, code);
	}
	for (i = 0; i < 3 && !code; ++i) {
		if (!streams[i])
			continue;
		do {
			descriptors[i] = fcntl(
				streams[i]->descriptor, F_DUPFD_CLOEXEC, 3);
		} while (descriptors[i] < 0 && errno == EINTR);
		if (descriptors[i] < 0) {
			code = errno;
			break;
		}
		code = posix_spawn_file_actions_adddup2(
			&actions, descriptors[i], i);
		if (!code)
			code = posix_spawn_file_actions_addclose(
				&actions, descriptors[i]);
	}
	if (!code)
		code = posix_spawn(&identifier, path, &actions, NULL,
			(char *const *)arguments->items, env);
	(void)posix_spawn_file_actions_destroy(&actions);
	for (i = 0; i < 3; ++i)
		if (descriptors[i] >= 0)
			(void)close(descriptors[i]);
	hx_os_memory_free(env);
	if (code)
		return hx_os_i_errno(error, code);
	process->native = identifier;
	process->identifier = (uint64_t)identifier;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
hx_os_status hx_os_i_process_poll(hx_os_process *process, hx_os_error *error)
{
	int native_status;
	pid_t value;
	if (process->finished)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	do {
		value = waitpid(
			(pid_t)process->native, &native_status, WNOHANG);
	} while (value < 0 && errno == EINTR);
	if (value < 0)
		return hx_os_i_errno(error, errno);
	if (!value)
		return hx_os_i_error(error, HX_OS_WOULD_BLOCK, 0, 0);
	if (WIFEXITED(native_status)) {
		process->result.exit_code =
			(uint32_t)WEXITSTATUS(native_status);
		process->result.signal_number = 0;
	}
	else if (WIFSIGNALED(native_status)) {
		process->result.exit_code = 0;
		process->result.signal_number = WTERMSIG(native_status);
	}
	else
		return hx_os_i_error(error, HX_OS_WOULD_BLOCK, 0, 0);
	process->finished = 1;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
hx_os_status hx_os_i_process_dispose(hx_os_process *process, hx_os_error *error)
{
	(void)process;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
