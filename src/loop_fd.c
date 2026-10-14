#include "loop_fd_private.h"
#if HX_OS_ENABLE_LOOP
#include "loop_private.h"
#endif

#if HX_OS_ENABLE_LOOP_FD && HX_OS_ENABLE_LOOP && !defined(_WIN32)
static hx_os_status hx_os_loop_fd_unregister(
	hx_os_loop_watch *watch, hx_os_error *error)
{
	hx_os_status status;

	if (watch->descriptor < 0)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	status = hx_os_i_loop_watch(
		watch->loop, watch->descriptor, &watch->source, 0, error);
	if (status == HX_OS_OK) {
		int result = close(watch->descriptor);

		int code = errno;

		watch->descriptor = -1;
		if (result)
			return hx_os_i_errno(error, code);
	}

	return status == HX_OS_OK ? hx_os_i_error(error, HX_OS_OK, 0, 0)
				  : status;
}

hx_os_status hx_os_i_loop_fd_register(
	hx_os_loop_watch *watch, hx_os_error *error)
{
	hx_os_status status = hx_os_i_loop_watch(watch->loop, watch->descriptor,
		&watch->source, watch->events, error);
#if defined(__linux__)
	if (status != HX_OS_OK && error && error->domain == HX_OS_ERROR_ERRNO &&
		error->native_code == EPERM)
		return hx_os_i_error(
			error, HX_OS_NOT_SUPPORTED, HX_OS_ERROR_ERRNO, EPERM);
#endif
	return status;
}

static void hx_os_loop_fd_retired(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	hx_os_loop_watch *watch = user;
	hx_os_error error;
	hx_os_loop_fd_cleanup_fn cleanup = watch->cleanup;
	void *data = watch->user;

	(void)completion;
	// A failed unregister keeps the fd/source alive for close retry.
	if (hx_os_loop_fd_unregister(watch, &error) != HX_OS_OK)
		request->completion.error = error;

	watch->function = NULL;
	watch->cleanup = NULL;
	watch->user = NULL;
	request->fd_watch = NULL;
	if (cleanup)
		cleanup(data);
}

size_t hx_os_i_loop_fd_step(hx_os_request *request)
{
	hx_os_loop_watch *watch = request->fd_watch;
	int ready;

	if (request->stop_status) {
		request->ready = 1;

		return 0;
	}

	ready = watch->source.ready & watch->events;
	watch->source.ready = 0;
	if (!ready)
		return 0;

	watch->function(watch, ready, watch->user);
	if (hx_os_atomic_load(&request->cancelled, HX_OS_ACQUIRE)) {
		request->stop_status = HX_OS_CANCELLED;
		request->ready = 1;
	}

	return 1;
}
#else
hx_os_status hx_os_i_loop_fd_register(
	hx_os_loop_watch *watch, hx_os_error *error)
{
	(void)watch;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}
#if HX_OS_ENABLE_LOOP
size_t hx_os_i_loop_fd_step(hx_os_request *request)
{
	(void)hx_os_i_error(
		&request->completion.error, HX_OS_NOT_SUPPORTED, 0, 0);
	request->ready = 1;

	return 0;
}
#endif
#endif

hx_os_status hx_os_loop_attach_fd_with_cleanup(hx_os_loop *loop, int descriptor,
	int events, hx_os_loop_fd_fn function, hx_os_loop_fd_cleanup_fn cleanup,
	void *user, hx_os_loop_watch **out, hx_os_error *error)
{
#if HX_OS_ENABLE_LOOP_FD && HX_OS_ENABLE_LOOP && !defined(_WIN32)
	hx_os_loop_watch *watch;
	hx_os_operation operation;
	hx_os_status status;
	hx_os_error native;

	if (!loop || descriptor < 0 || !events || (events & ~3) || !function ||
		!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (loop->owner != hx_os_thread_id())
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	watch = hx_os_i_zero(loop->context, sizeof(*watch));
	if (!watch)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	watch->loop = loop;
	watch->function = function;
	watch->cleanup = cleanup;
	watch->user = user;
	watch->events = events;
	do {
		watch->descriptor = fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
	} while (watch->descriptor < 0 && errno == EINTR);
	if (watch->descriptor < 0) {
		int code = errno;

		hx_os_memory_free(watch);

		return hx_os_i_errno(error, code);
	}

	hx_os_operation_init(&operation, HX_OS_OP_TIMER);

	status = hx_os_i_loop_submit(loop, &operation, NULL, watch,
		hx_os_loop_fd_retired, watch, &watch->request, &native);
	if (status != HX_OS_OK) {
		// kqueue registration can have installed only one filter.
		(void)hx_os_i_loop_watch(
			loop, watch->descriptor, &watch->source, 0, NULL);

		(void)close(watch->descriptor);
		hx_os_memory_free(watch);
		if (error)
			*error = native;

		return status;
	}

	watch->next = loop->fd_watches;
	loop->fd_watches = watch;
	*out = watch;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)loop;
	(void)descriptor;
	(void)events;
	(void)function;
	(void)cleanup;
	(void)user;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_loop_attach_fd(hx_os_loop *loop, int descriptor, int events,
	hx_os_loop_fd_fn function, void *user, hx_os_loop_watch **out,
	hx_os_error *error)
{
	return hx_os_loop_attach_fd_with_cleanup(
		loop, descriptor, events, function, NULL, user, out, error);
}

int hx_os_loop_watch_descriptor(const hx_os_loop_watch *watch)
{
	return watch ? watch->descriptor : -1;
}

void hx_os_loop_watch_stop(hx_os_loop_watch *watch)
{
#if HX_OS_ENABLE_LOOP_FD && HX_OS_ENABLE_LOOP && !defined(_WIN32)
	if (watch)
		hx_os_request_cancel(watch->request);
#else
	(void)watch;
#endif
}

hx_os_status hx_os_loop_watch_wait_until(
	hx_os_loop_watch *watch, hx_os_deadline deadline, hx_os_error *error)
{
#if HX_OS_ENABLE_LOOP_FD && HX_OS_ENABLE_LOOP && !defined(_WIN32)
	hx_os_completion completion;
	hx_os_status status;

	if (!watch)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_request_wait_until(
		watch->request, deadline, &completion, error);
	if (status != HX_OS_OK)
		return status;

	if (completion.error.status == HX_OS_CANCELLED)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (error)
		*error = completion.error;

	return completion.error.status;
#else
	(void)watch;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_loop_watch_close_until(
	hx_os_loop_watch **watch, hx_os_deadline deadline, hx_os_error *error)
{
#if HX_OS_ENABLE_LOOP_FD && HX_OS_ENABLE_LOOP && !defined(_WIN32)
	hx_os_loop_watch *value, **cursor;
	hx_os_loop *loop;
	hx_os_status status;

	if (!watch)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!*watch)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	value = *watch;
	loop = value->loop;

	status = hx_os_i_loop_cancel_until(
		loop, value->request, deadline, &value->close_running, error);
	if (status != HX_OS_OK)
		return status;

	// Retry an earlier unregister failure before freeing source storage.
	if (value->descriptor >= 0) {
		status = hx_os_loop_fd_unregister(value, error);
		if (value->descriptor >= 0)
			return status;
	}
	else
		status = hx_os_loop_watch_wait_until(
			value, HX_OS_DEADLINE_NOW, error);

	for (cursor = &loop->fd_watches; *cursor; cursor = &(*cursor)->next)
		if (*cursor == value) {
			*cursor = value->next;
			break;
		}

	hx_os_request_release(value->request);
	hx_os_memory_free(value);
	*watch = NULL;

	return status;
#else
	(void)watch;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_loop_detach_fd(hx_os_loop_watch **watch, hx_os_error *error)
{
	return hx_os_loop_watch_close_until(watch, HX_OS_DEADLINE_NOW, error);
}
