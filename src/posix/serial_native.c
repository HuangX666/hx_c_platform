#include "serial_private.h"

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

static speed_t hx_os_serial_speed(uint32_t baud)
{
	switch (baud) {
	case 1200:
		return B1200;
	case 2400:
		return B2400;
	case 4800:
		return B4800;
	case 9600:
		return B9600;
	case 19200:
		return B19200;
	case 38400:
		return B38400;
	case 57600:
		return B57600;
	case 115200:
		return B115200;
#ifdef B230400
	case 230400:
		return B230400;
#endif
#ifdef B460800
	case 460800:
		return B460800;
#endif
#ifdef B921600
	case 921600:
		return B921600;
#endif
	default:
		return 0;
	}
}

hx_os_status hx_os_i_serial_open(hx_os_context *context, const char *device,
	const hx_os_serial_options *options, hx_os_serial *serial,
	hx_os_error *error)
{
	struct termios settings;
	speed_t speed = hx_os_serial_speed(options->baud);
	int fd, code;

	(void)context;
	if (!speed)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#ifndef CRTSCTS
	if (options->flow)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
	do {
		fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
	} while (fd < 0 && errno == EINTR);
	if (fd < 0)
		return hx_os_i_errno(error, errno);
	if (tcgetattr(fd, &settings))
		goto failed;
	settings.c_iflag = options->parity ? INPCK : 0;
	settings.c_oflag = 0;
	settings.c_lflag = 0;
	settings.c_cflag = CLOCAL | CREAD;
	switch (options->data_bits) {
	case 5:
		settings.c_cflag |= CS5;
		break;
	case 6:
		settings.c_cflag |= CS6;
		break;
	case 7:
		settings.c_cflag |= CS7;
		break;
	default:
		settings.c_cflag |= CS8;
		break;
	}
	if (options->parity)
		settings.c_cflag |= PARENB;
	if (options->parity == HX_OS_SERIAL_ODD)
		settings.c_cflag |= PARODD;
	if (options->stop_bits == 2)
		settings.c_cflag |= CSTOPB;
#ifdef CRTSCTS
	if (options->flow)
		settings.c_cflag |= CRTSCTS;
#endif
	settings.c_cc[VMIN] = 1;
	settings.c_cc[VTIME] = 0;
	if (cfsetispeed(&settings, speed) || cfsetospeed(&settings, speed) ||
		tcsetattr(fd, TCSANOW, &settings))
		goto failed;
	serial->native = fd;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
failed:
	code = errno;
	(void)close(fd);
	return hx_os_i_errno(error, code);
}

hx_os_status hx_os_i_serial_transfer(hx_os_serial *serial, void *data,
	size_t bytes, int writing, hx_os_deadline deadline,
	hx_os_cancel_token *cancel, size_t *completed, hx_os_error *error)
{
	struct pollfd wait = {
		(int)serial->native, writing ? POLLOUT : POLLIN, 0};

	for (;;) {
		uint32_t remaining = hx_os_i_wait_ms(deadline);
		int timeout = remaining > 50 ? 50 : (int)remaining;
		int ready;
		ssize_t count;

		if (hx_os_cancel_requested(cancel))
			return hx_os_i_error(error, HX_OS_CANCELLED, 0, 0);
		ready = poll(&wait, 1, timeout);
		if (ready < 0 && errno == EINTR)
			continue;
		if (ready < 0)
			return hx_os_i_errno(error, errno);
		if (!ready) {
			if (hx_os_deadline_expired(deadline))
				return hx_os_i_error(
					error, HX_OS_TIMEOUT, 0, 0);
			continue;
		}
		if (writing)
			count = write((int)serial->native, data, bytes);
		else
			count = read((int)serial->native, data, bytes);
		if (count < 0 &&
			(errno == EINTR || errno == EAGAIN ||
				errno == EWOULDBLOCK)) {
			if (hx_os_deadline_expired(deadline))
				return hx_os_i_error(
					error, HX_OS_TIMEOUT, 0, 0);
			continue;
		}
		if (count < 0)
			return hx_os_i_errno(error, errno);
		if (!count)
			return hx_os_i_error(error,
				writing ? HX_OS_IO_ERROR : HX_OS_EOF, 0, 0);
		*completed = (size_t)count;
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
}

hx_os_status hx_os_i_serial_discard(
	hx_os_serial *serial, int input, int output, hx_os_error *error)
{
	int queue = input && output ? TCIOFLUSH : input ? TCIFLUSH : TCOFLUSH;

	return tcflush((int)serial->native, queue)
		? hx_os_i_errno(error, errno)
		: hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_serial_close(hx_os_serial *serial, hx_os_error *error)
{
	return close((int)serial->native)
		? hx_os_i_errno(error, errno)
		: hx_os_i_error(error, HX_OS_OK, 0, 0);
}
