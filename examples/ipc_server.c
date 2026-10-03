#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>

/* Run in one terminal: hx_os_ipc_server local://demo 1
 * The last argument is the number of business messages before shutdown.
 */
int main(int argc, char **argv)
{
	const char *address = argc > 1 ? argv[1] : "local://demo";
	unsigned long count = argc > 2 ? strtoul(argv[2], NULL, 10) : 1;
	hx_ipc *server = NULL;
	hx_string actual = HX_STRING_INIT;
	hx_os_error error = {0};
	hx_os_status status;
	unsigned long received = 0;
	if (!count || count > 100000)
		return 2;
	status = hx_ipc_listen(address, &server, &error);
	if (status == HX_OS_OK)
		status = hx_ipc_address(server, &actual, &error);
	if (status == HX_OS_OK) {
		printf("Listening: %s\n", hx_string_c_str(&actual));
		fflush(stdout);
	}
	while (status == HX_OS_OK && received < count) {
		hx_ipc_message *message = NULL;
		status = hx_ipc_receive_until(server,
			hx_os_deadline_after_ms(30000), &message, &error);
		if (status != HX_OS_OK)
			break;
		if (message->kind == HX_IPC_REQUEST) {
			status = hx_ipc_reply_until(server, message,
				message->data, message->bytes,
				hx_os_deadline_after_ms(5000), &error);
			++received;
		}
		else if (message->kind == HX_IPC_DATA)
			++received;
		hx_ipc_message_release(message);
	}
	if (status != HX_OS_OK) {
		char diagnostic[512];
		size_t required;
		(void)hx_os_error_message(
			&error, diagnostic, sizeof(diagnostic), &required);
		fprintf(stderr, "Server: %s\n", diagnostic);
	}
	if (hx_ipc_close_until(&server, hx_os_deadline_after_ms(5000),
		    &error) != HX_OS_OK) {
		fprintf(stderr, "Close incomplete: %d\n", (int)error.status);
		status = error.status;
	}
	hx_string_destroy(&actual);
	if (status == HX_OS_OK)
		printf("Handled %lu complete messages.\n", received);
	return status == HX_OS_OK ? 0 : 1;
}
