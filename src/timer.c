#include "timer_private.h"
#if HX_OS_ENABLE_LOOP
#include "loop_private.h"
#endif

#if HX_OS_ENABLE_TIMER && HX_OS_ENABLE_LOOP
static void hx_os_timer_retired(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	hx_os_timer *timer = user;
	hx_os_timer_cleanup_fn cleanup = timer->cleanup;
	void *data = timer->user;

	(void)completion;
	timer->function = NULL;
	timer->cleanup = NULL;
	timer->user = NULL;
	request->timer = NULL;
	if (cleanup)
		cleanup(data);
}

static size_t hx_os_timer_failed(hx_os_request *request)
{
	hx_os_timer *timer = request->timer;

	request->stop_status = request->completion.error.status;
	request->ready = 1;
	timer->function(timer, &request->completion.error, timer->user);

	return 1;
}

size_t hx_os_i_timer_step(hx_os_request *request)
{
	hx_os_timer *timer = request->timer;
	hx_os_deadline due = request->operation.deadline;
	uint64_t now, steps;
	hx_os_error tick;

	if (request->stop_status) {
		request->ready = 1;

		return 0;
	}

	if (hx_os_monotonic_read(&now, &request->completion.error) != HX_OS_OK)
		return hx_os_timer_failed(request);

	if (now < due)
		return 0;

	hx_os_error_clear(&tick);
	timer->function(timer, &tick, timer->user);
	if (hx_os_atomic_load(&request->cancelled, HX_OS_ACQUIRE)) {
		request->stop_status = HX_OS_CANCELLED;
		request->ready = 1;

		return 1;
	}

	if (hx_os_monotonic_read(&now, &request->completion.error) != HX_OS_OK)
		return hx_os_timer_failed(request);

	steps = (now - due) / timer->interval + 1;
	if (steps > (UINT64_MAX - 1 - due) / timer->interval) {
		(void)hx_os_i_error(
			&request->completion.error, HX_OS_RANGE, 0, 0);

		return hx_os_timer_failed(request);
	}

	request->operation.deadline = due + steps * timer->interval;

	return 1;
}
#elif HX_OS_ENABLE_LOOP
size_t hx_os_i_timer_step(hx_os_request *request)
{
	(void)hx_os_i_error(
		&request->completion.error, HX_OS_NOT_SUPPORTED, 0, 0);
	request->ready = 1;

	return 0;
}
#endif

hx_os_status hx_os_timer_create_with_cleanup(hx_os_loop *loop,
	uint64_t interval_ns, hx_os_timer_fn function,
	hx_os_timer_cleanup_fn cleanup, void *user, hx_os_timer **out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_TIMER && HX_OS_ENABLE_LOOP
	hx_os_timer *timer;
	hx_os_operation operation;
	uint64_t now;
	hx_os_status status;

	if (!loop || !function || !out || !interval_ns)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (loop->owner != hx_os_thread_id())
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	status = hx_os_monotonic_read(&now, error);
	if (status != HX_OS_OK)
		return status;

	if (now >= UINT64_MAX - 1 || interval_ns > UINT64_MAX - 1 - now)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	timer = hx_os_i_zero(loop->context, sizeof(*timer));
	if (!timer)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	timer->loop = loop;
	timer->function = function;
	timer->cleanup = cleanup;
	timer->user = user;
	timer->interval = interval_ns;
	hx_os_operation_init(&operation, HX_OS_OP_TIMER);
	operation.deadline = now + interval_ns;

	status = hx_os_loop_submit(loop, &operation, hx_os_timer_retired, timer,
		&timer->request, error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(timer);

		return status;
	}

	timer->request->timer = timer;
	timer->next = loop->timers;
	loop->timers = timer;
	*out = timer;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)loop;
	(void)interval_ns;
	(void)function;
	(void)cleanup;
	(void)user;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_timer_create(hx_os_loop *loop, uint64_t interval_ns,
	hx_os_timer_fn function, void *user, hx_os_timer **out,
	hx_os_error *error)
{
	return hx_os_timer_create_with_cleanup(
		loop, interval_ns, function, NULL, user, out, error);
}

void hx_os_timer_stop(hx_os_timer *timer)
{
#if HX_OS_ENABLE_TIMER && HX_OS_ENABLE_LOOP
	if (timer)
		hx_os_request_cancel(timer->request);
#else
	(void)timer;
#endif
}

hx_os_status hx_os_timer_wait_until(
	hx_os_timer *timer, hx_os_deadline deadline, hx_os_error *error)
{
#if HX_OS_ENABLE_TIMER && HX_OS_ENABLE_LOOP
	hx_os_completion completion;
	hx_os_status status;

	if (!timer)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_request_wait_until(
		timer->request, deadline, &completion, error);
	if (status != HX_OS_OK)
		return status;

	if (completion.error.status == HX_OS_CANCELLED)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (error)
		*error = completion.error;

	return completion.error.status;
#else
	(void)timer;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_timer_close_until(
	hx_os_timer **timer, hx_os_deadline deadline, hx_os_error *error)
{
#if HX_OS_ENABLE_TIMER && HX_OS_ENABLE_LOOP
	hx_os_timer *value, **cursor;
	hx_os_loop *loop;
	hx_os_status status;

	if (!timer)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!*timer)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	value = *timer;
	loop = value->loop;

	status = hx_os_i_loop_cancel_until(
		loop, value->request, deadline, &value->close_running, error);
	if (status != HX_OS_OK)
		return status;

	status = hx_os_timer_wait_until(value, HX_OS_DEADLINE_NOW, error);

	for (cursor = &loop->timers; *cursor; cursor = &(*cursor)->next)
		if (*cursor == value) {
			*cursor = value->next;
			break;
		}

	hx_os_request_release(value->request);
	hx_os_memory_free(value);
	*timer = NULL;

	return status;
#else
	(void)timer;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
