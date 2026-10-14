#include <hx_os.h>
#include <stdio.h>
#include <string.h>

static hx_os_status publish(hx_os_persistent_queue *queue, hx_os_error *error)
{
	static const char *const ids[] = {
		"recognition-001", "recognition-002", "recognition-003"};
	static const char *const records[] = {
		"{\"person\":17,\"device\":\"factory-gate\",\"success\":true}",
		"{\"person\":23,\"device\":\"hospital-gate\",\"success\":true}",
		"{\"person\":31,\"device\":\"factory-gate\",\"success\":true}"};
	hx_os_persistent_item items[3];
	size_t i;

	for (i = 0; i < 3; ++i) {
		items[i].id = ids[i];
		items[i].data.data = records[i];
		items[i].data.size = strlen(records[i]);
	}

	return hx_os_persistent_queue_push_many(queue, items, 3, error);
}

static hx_os_status consume(hx_os_persistent_queue *queue, hx_os_error *error)
{
	for (;;) {
		hx_os_persistent_message *message = NULL;
		hx_os_bytes_view data;
		hx_os_status status = hx_os_persistent_queue_take_until(queue,
			hx_os_deadline_after_ms(1000), NULL, &message, error);

		if (status == HX_OS_TIMEOUT)
			return HX_OS_OK;
		if (status != HX_OS_OK)
			return status;

		data = hx_os_persistent_message_data(message);
		printf("Handle %s: %.*s\n",
			hx_os_persistent_message_id(message), (int)data.size,
			(const char *)data.data);
		/* Replace the print with an upload. Ack only after the server
		 * confirms the record; otherwise release it for a later retry.
		 */
		status = hx_os_persistent_queue_ack(queue, message, error);
		hx_os_persistent_message_destroy(&message);
		if (status != HX_OS_OK)
			return status;
	}
}

int main(int argc, char **argv)
{
	hx_os_persistent_queue *queue = NULL;
	hx_os_error error;
	hx_os_status status, closed;

	if (argc != 3 ||
		(strcmp(argv[1], "publish") && strcmp(argv[1], "consume"))) {
		fputs("Usage: persistent_queue publish|consume queue.db\n",
			stderr);
		return 1;
	}

	status = hx_os_persistent_queue_open(
		NULL, argv[2], NULL, &queue, &error);
	if (status == HX_OS_OK)
		status = !strcmp(argv[1], "publish") ? publish(queue, &error)
						     : consume(queue, &error);
	if (status != HX_OS_OK)
		fprintf(stderr, "Queue failed: %s\n",
			hx_os_status_name(status));

	closed = hx_os_persistent_queue_close(&queue, NULL);
	return status == HX_OS_OK && closed == HX_OS_OK ? 0 : 1;
}
