#include "loop_private.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>

const char *hx_os_loop_backend(void)
{
	return "epoll";
}

hx_os_status hx_os_i_loop_native_create(hx_os_loop *loop, hx_os_error *error)
{
	struct epoll_event event;
	int code;
	loop->port = epoll_create1(EPOLL_CLOEXEC);
	if (loop->port < 0)
		return hx_os_i_errno(error, errno);
	loop->wake_read = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (loop->wake_read < 0) {
		code = errno;
		(void)close(loop->port);
		return hx_os_i_errno(error, code);
	}
	loop->wake_write = loop->wake_read;
	memset(&event, 0, sizeof(event));
	event.events = EPOLLIN;
	if (epoll_ctl(loop->port, EPOLL_CTL_ADD, loop->wake_read, &event)) {
		code = errno;
		hx_os_i_loop_native_destroy(loop);
		return hx_os_i_errno(error, code);
	}
	return HX_OS_OK;
}

void hx_os_i_loop_native_destroy(hx_os_loop *loop)
{
	(void)close(loop->wake_read);
	(void)close(loop->port);
}

void hx_os_i_loop_wake(hx_os_loop *loop)
{
	uint64_t value = 1;
	ssize_t result;
	do {
		result = write(loop->wake_write, &value, sizeof(value));
	} while (result < 0 && errno == EINTR);
}

hx_os_status hx_os_i_loop_watch(hx_os_loop *loop, int descriptor,
	hx_os_i_poll_source *source, int wanted, hx_os_error *error)
{
	struct epoll_event event;
	if (wanted == source->watched)
		return HX_OS_OK;
	if (!wanted) {
		if (epoll_ctl(loop->port, EPOLL_CTL_DEL, descriptor, NULL))
			return hx_os_i_errno(error, errno);
	}
	else {
		memset(&event, 0, sizeof(event));
		event.events = EPOLLRDHUP;
		if (wanted & HX_OS_WAIT_READ)
			event.events |= EPOLLIN;
		if (wanted & HX_OS_WAIT_WRITE)
			event.events |= EPOLLOUT;
		event.data.ptr = source;
		if (epoll_ctl(loop->port,
			    source->watched ? EPOLL_CTL_MOD : EPOLL_CTL_ADD,
			    descriptor, &event))
			return hx_os_i_errno(error, errno);
	}
	source->watched = wanted;
	return HX_OS_OK;
}

hx_os_status hx_os_i_loop_refresh(hx_os_socket *socket, hx_os_error *error)
{
	return hx_os_i_loop_watch(socket->loop, socket->native, &socket->source,
		(int)hx_os_atomic_load(&socket->pending, HX_OS_ACQUIRE), error);
}

hx_os_status hx_os_i_loop_wait(
	hx_os_loop *loop, hx_os_deadline deadline, hx_os_error *error)
{
	struct epoll_event events[64];
	uint32_t milliseconds = hx_os_i_wait_ms(deadline);
	int count = epoll_wait(loop->port, events, 64,
		milliseconds == UINT32_MAX ? -1 : (int)milliseconds);
	int i;
	if (count < 0)
		return errno == EINTR ? HX_OS_OK : hx_os_i_errno(error, errno);
	for (i = 0; i < count; ++i) {
		hx_os_i_poll_source *source = events[i].data.ptr;
		if (!source) {
			uint64_t value;
			ssize_t result;
			do {
				result = read(
					loop->wake_read, &value, sizeof(value));
			} while (result > 0 || (result < 0 && errno == EINTR));
		}
		else {
			if (events[i].events & (EPOLLIN | EPOLLRDHUP))
				source->ready |= HX_OS_WAIT_READ;
			if (events[i].events & EPOLLOUT)
				source->ready |= HX_OS_WAIT_WRITE;
			if (events[i].events & (EPOLLERR | EPOLLHUP))
				source->ready |= 3;
		}
	}
	return count ? HX_OS_OK : HX_OS_TIMEOUT;
}
