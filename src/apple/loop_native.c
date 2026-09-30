#include "loop_private.h"
#include <sys/event.h>

const char *hx_os_loop_backend(void)
{
	return "kqueue";
}

hx_os_status hx_os_i_loop_native_create(hx_os_loop *loop, hx_os_error *error)
{
	struct kevent event;
	int code;
	loop->port = kqueue();
	if (loop->port < 0)
		return hx_os_i_errno(error, errno);
	if (fcntl(loop->port, F_SETFD, FD_CLOEXEC)) {
		code = errno;
		(void)close(loop->port);
		return hx_os_i_errno(error, code);
	}
	EV_SET(&event, 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
	if (kevent(loop->port, &event, 1, NULL, 0, NULL)) {
		code = errno;
		(void)close(loop->port);
		return hx_os_i_errno(error, code);
	}
	return HX_OS_OK;
}

void hx_os_i_loop_native_destroy(hx_os_loop *loop)
{
	(void)close(loop->port);
}

void hx_os_i_loop_wake(hx_os_loop *loop)
{
	struct kevent event;
	int result;
	EV_SET(&event, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
	do {
		result = kevent(loop->port, &event, 1, NULL, 0, NULL);
	} while (result < 0 && errno == EINTR);
}

hx_os_status hx_os_i_loop_refresh(hx_os_socket *socket, hx_os_error *error)
{
	hx_os_loop *loop = socket->loop;
	int wanted = (int)hx_os_atomic_load(&socket->pending, HX_OS_ACQUIRE);
	int direction;
	for (direction = 1; direction <= 2; direction *= 2) {
		struct kevent event;
		if (!((wanted ^ socket->watched) & direction))
			continue;
		EV_SET(&event, (uintptr_t)socket->native,
			direction == 1 ? EVFILT_READ : EVFILT_WRITE,
			(wanted & direction) ? EV_ADD : EV_DELETE, 0, 0,
			socket);
		if (kevent(loop->port, &event, 1, NULL, 0, NULL))
			return hx_os_i_errno(error, errno);
		socket->watched ^= direction;
	}
	return HX_OS_OK;
}

hx_os_status hx_os_i_loop_wait(
	hx_os_loop *loop, hx_os_deadline deadline, hx_os_error *error)
{
	struct kevent events[64];
	struct timespec duration;
	uint32_t milliseconds = hx_os_i_wait_ms(deadline);
	int count;
	int i;
	duration.tv_sec = (time_t)(milliseconds / 1000);
	duration.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
	count = kevent(loop->port, NULL, 0, events, 64,
		milliseconds == UINT32_MAX ? NULL : &duration);
	if (count < 0)
		return errno == EINTR ? HX_OS_OK : hx_os_i_errno(error, errno);
	for (i = 0; i < count; ++i) {
		hx_os_socket *socket = events[i].udata;
		if (!socket)
			continue;
		if (events[i].filter == EVFILT_READ)
			socket->ready |= HX_OS_WAIT_READ;
		if (events[i].filter == EVFILT_WRITE)
			socket->ready |= HX_OS_WAIT_WRITE;
		if (events[i].flags & EV_ERROR)
			socket->ready |= 3;
	}
	return count ? HX_OS_OK : HX_OS_TIMEOUT;
}
