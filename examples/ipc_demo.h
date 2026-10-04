#ifndef HX_C_PLATFORM_EXAMPLE_IPC_DEMO_H
#define HX_C_PLATFORM_EXAMPLE_IPC_DEMO_H

#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helpers for these standalone demos, not another communication API. */
static inline int demo_check(
	hx_os_status status, const hx_os_error *error, const char *operation)
{
	char text[512] = {0};
	size_t required;
	hx_os_error detail = {0};
	if (status == HX_OS_OK)
		return 1;
	if (error)
		detail = *error;
	detail.status = status;
	(void)hx_os_error_message(&detail, text, sizeof(text), &required);
	fprintf(stderr, "%s failed: status=%d %s\n", operation, (int)status,
		text);
	return 0;
}

static inline int demo_message_is(
	const hx_ipc_message *message, const char *text)
{
	return message && message->encoding == HX_IPC_UTF8 &&
		message->bytes == strlen(text) &&
		!memcmp(message->data, text, message->bytes);
}

static inline int demo_close(hx_ipc **endpoint)
{
	hx_os_error error = {0};
	hx_os_status status = hx_ipc_close_until(
		endpoint, hx_os_deadline_after_ms(5000), &error);
	if (status == HX_OS_TIMEOUT) {
		fputs("Close still owns the endpoint; retrying.\n", stderr);
		status = hx_ipc_close_until(
			endpoint, hx_os_deadline_after_ms(1000), &error);
	}
	return demo_check(status, &error, "close");
}

static inline void demo_join(hx_os_thread **thread, hx_os_deadline deadline)
{
	hx_os_error error = {0};
	hx_os_status status =
		hx_os_thread_join_until(*thread, deadline, NULL, &error);
	if (!demo_check(status, &error, "join")) {
		/* Fatal CLI-demo failure: exit while borrowed stack data is
		 * still alive. The library neither detaches nor kills these
		 * threads.
		 */
		exit(EXIT_FAILURE);
	}
	if (!demo_check(hx_os_thread_destroy(thread, &error), &error,
		    "destroy thread"))
		exit(EXIT_FAILURE);
}

#endif
