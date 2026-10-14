#include <hx_os.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	hx_os_serial *serial = NULL;
	const char request[] = "{\"request\":42}\n";
	char response[256];
	size_t sent = 0, received = 0;
	hx_os_status status;
	hx_os_deadline deadline = hx_os_deadline_after_ms(5000);

	if (argc != 2) {
		fprintf(stderr,
			"Usage: hx_os_serial_message COM3 or /dev/ttyUSB0\n");
		return 2;
	}
	status = hx_os_serial_open(NULL, argv[1], NULL, &serial, NULL);
	while (status == HX_OS_OK && sent < sizeof(request) - 1) {
		size_t count = 0;

		status = hx_os_serial_write_until(serial, request + sent,
			sizeof(request) - 1 - sent, deadline, NULL, &count,
			NULL);
		sent += count;
	}
	/* This example's device protocol replies with one newline-delimited
	 * JSON. */
	while (status == HX_OS_OK && received < sizeof(response) - 1) {
		size_t count = 0;

		status = hx_os_serial_read_until(serial, response + received, 1,
			deadline, NULL, &count, NULL);
		received += count;
		if (received && response[received - 1] == '\n')
			break;
	}
	if (status == HX_OS_OK &&
		(received == 0 || response[received - 1] != '\n'))
		status = HX_OS_LIMIT;
	response[received] = 0;
	if (status == HX_OS_OK)
		puts(response);
	else
		fprintf(stderr, "Serial: %s\n", hx_os_status_name(status));

	(void)hx_os_serial_close(&serial, NULL);
	return status == HX_OS_OK ? 0 : 1;
}
