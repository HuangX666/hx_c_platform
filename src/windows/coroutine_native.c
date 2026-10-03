#include "coroutine_native.h"
#include <windows.h>

hx_os_status hx_os_i_coro_stack_cost(
	size_t bytes, size_t *cost, hx_os_error *error)
{
	SYSTEM_INFO info;
	size_t rounded;
	GetSystemInfo(&info);
	if (!hx_os_i_add_size(bytes, (size_t)info.dwPageSize * 2, &rounded) ||
		!hx_os_i_add_size(
			rounded, info.dwAllocationGranularity - 1, &rounded))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	*cost = rounded / info.dwAllocationGranularity *
		info.dwAllocationGranularity;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_coro_context_root(
	hx_os_i_coro_context *context, hx_os_error *error)
{
	if (IsThreadAFiber())
		context->fiber = GetCurrentFiber();
	else {
		context->fiber =
			ConvertThreadToFiberEx(NULL, FIBER_FLAG_FLOAT_SWITCH);
		context->converted = context->fiber != NULL;
	}
	return context->fiber ? hx_os_i_error(error, HX_OS_OK, 0, 0)
			      : hx_os_i_win32(error, GetLastError());
}

void hx_os_i_coro_context_root_close(hx_os_i_coro_context *context)
{
	if (context->converted)
		(void)ConvertFiberToThread();
	context->fiber = NULL;
}

static void WINAPI hx_os_coro_fiber_start(void *argument)
{
	hx_os_i_coro_context *context = argument;
	context->function(context->user);
	context->finished = 1;
	/* A completed fiber must switch back; returning would exit its thread.
	 */
	for (;;)
		SwitchToFiber(context->caller->fiber);
}

hx_os_status hx_os_i_coro_context_create(hx_os_i_coro_context *context,
	size_t bytes, void (*function)(void *), void *user, hx_os_error *error)
{
	SYSTEM_INFO info;
	hx_os_status status =
		hx_os_i_coro_stack_cost(bytes, &context->mapping_bytes, error);
	if (status != HX_OS_OK)
		return status;
	context->function = function;
	context->user = user;
	GetSystemInfo(&info);
	context->fiber = CreateFiberEx(info.dwPageSize, context->mapping_bytes,
		FIBER_FLAG_FLOAT_SWITCH, hx_os_coro_fiber_start, context);
	return context->fiber ? hx_os_i_error(error, HX_OS_OK, 0, 0)
			      : hx_os_i_win32(error, GetLastError());
}

void hx_os_i_coro_context_destroy(hx_os_i_coro_context *context)
{
	if (context->fiber)
		DeleteFiber(context->fiber);
	context->fiber = NULL;
}

void hx_os_i_coro_context_switch(
	hx_os_i_coro_context *from, hx_os_i_coro_context *to)
{
	(void)from;
	SwitchToFiber(to->fiber);
}
