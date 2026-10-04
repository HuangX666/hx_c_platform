#include "format_private.h"
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

hx_os_status hx_os_i_format_print(
	int stream, const char *text, size_t bytes, hx_os_error *error)
{
	FILE *output = stream == HX_PRINT_STDERR ? stderr : stdout;
	sigset_t mask, saved, pending;
	size_t position = 0;
	int code, restore, existed, descriptor;

	sigemptyset(&mask);
	sigaddset(&mask, SIGPIPE);
	code = pthread_sigmask(SIG_BLOCK, &mask, &saved);
	if (code)
		return hx_os_i_errno(error, code);

	if (sigpending(&pending)) {
		code = errno;
		(void)pthread_sigmask(SIG_SETMASK, &saved, NULL);

		return hx_os_i_errno(error, code);
	}

	existed = sigismember(&pending, SIGPIPE);
	flockfile(output);
	descriptor = fileno(output);
	if (descriptor < 0 || fflush(output))
		code = errno;
	else
		while (position < bytes) {
			size_t part = bytes - position;
			ssize_t written;

			if (part > (size_t)SSIZE_MAX)
				part = (size_t)SSIZE_MAX;

			do {
				written = write(
					descriptor, text + position, part);
			} while (written < 0 && errno == EINTR);
			if (written <= 0) {
				code = written < 0 ? errno : EIO;
				break;
			}

			position += (size_t)written;
		}

	funlockfile(output);
	if (code == EPIPE && !existed && !sigpending(&pending) &&
		sigismember(&pending, SIGPIPE)) {
		int signal_number;

		(void)sigwait(&mask, &signal_number);
	}

	restore = pthread_sigmask(SIG_SETMASK, &saved, NULL);
	if (code == EPIPE)
		return hx_os_i_error(
			error, HX_OS_CLOSED, HX_OS_ERROR_ERRNO, code);

	return hx_os_i_errno(error, code ? code : restore);
}
