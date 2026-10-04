#include <hx_os.h>
#include <stdio.h>

/* Start ipc_server first, then run hx_os_ipc_client local://demo. */
int main(int argc, char **argv)
{
	const char *address = argc > 1 ? argv[1] : "local://demo";
	const char *text = argc > 2 ? argv[2] : "Hello from C";
	hx_ipc *client = NULL;
	hx_ipc_message *reply = NULL;
	hx_os_error error = {0};

	hx_os_status status = hx_ipc_connect(address, &client, &error);
	if (status == HX_OS_OK)
		status = hx_ipc_request_text(client, text, &reply, &error);

	if (status == HX_OS_OK)
		printf("Reply: %.*s\n", (int)reply->bytes,
			(const char *)reply->data);
	else {
		char diagnostic[512];
		size_t required;

		(void)hx_os_error_message(
			&error, diagnostic, sizeof(diagnostic), &required);
		fprintf(stderr, "Client: %s\n", diagnostic);
	}

	hx_ipc_message_release(reply);
	if (hx_ipc_close_until(&client, hx_os_deadline_after_ms(5000),
		    &error) != HX_OS_OK) {
		fprintf(stderr, "Close incomplete: %d\n", (int)error.status);

		status = error.status;
	}

	return status == HX_OS_OK ? 0 : 1;
}
