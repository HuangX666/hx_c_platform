#include "ipc_demo.h"

typedef struct device_job {
	hx_ipc *client;
	uint64_t device_id;
	unsigned int interval_ms;
	int ok;
} device_job;

typedef struct notifications {
	hx_ipc *client;
	int ok;
} notifications;

static int request_expected(hx_ipc *client, uint64_t device_id,
	const char *type, int64_t interval, const char *expected,
	int64_t expected_interval)
{
	json_field fields[] = {json_text("type", type),
		json_int("device_id", (int64_t)device_id),
		json_int("interval_ms", interval)};
	hx_string wire = HX_STRING_INIT;
	hx_ipc_message *reply = NULL;
	hx_os_error error = {0};
	int ok = json_encode(fields, interval < 0 ? 2 : 3, &wire);
	if (ok)
		ok = demo_check(
			hx_ipc_request_text_until(client, 0, device_id,
				hx_string_c_str(&wire),
				hx_os_deadline_after_ms(5000), &reply, &error),
			&error, "JSON request");
	if (ok) {
		hx_os_config_document *document = reply->encoding == HX_IPC_UTF8
			? json_decode(reply->data, reply->bytes)
			: NULL;
		const hx_os_config_node *root = hx_os_config_root(document);
		ok = document && reply->channel == device_id &&
			json_has_text(root, "type", expected) &&
			json_has_int(root, "device_id", (int64_t)device_id) &&
			json_has_bool(
				root, "ok", strcmp(expected, "error") != 0);
		if (expected_interval >= 0)
			ok = ok &&
				json_has_int(
					root, "interval_ms", expected_interval);
		if (!strcmp(expected, "error"))
			ok = ok &&
				json_has_text(root, "code", "invalid_interval");
		printf("[IPC reply] %.*s\n", (int)reply->bytes,
			(const char *)reply->data);
		hx_os_config_release(document);
		if (!ok)
			fputs("Unexpected JSON device result.\n", stderr);
	}
	hx_ipc_message_release(reply);
	hx_string_destroy(&wire);
	return ok;
}

static void *HX_OS_CALL device_work(void *user)
{
	device_job *job = user;
	job->ok = request_expected(job->client, job->device_id, "open", -1,
			  "opened", 1000) &&
		request_expected(job->client, job->device_id, "get", -1,
			"state", 1000) &&
		request_expected(job->client, job->device_id, "set",
			job->interval_ms, "updated", job->interval_ms) &&
		request_expected(job->client, job->device_id, "get", -1,
			"state", job->interval_ms) &&
		request_expected(job->client, job->device_id, "set", 0, "error",
			job->interval_ms) &&
		request_expected(job->client, job->device_id, "get", -1,
			"state", job->interval_ms) &&
		request_expected(job->client, job->device_id, "close", -1,
			"closed", job->interval_ms);
	return NULL;
}

static void *HX_OS_CALL receive_notifications(void *user)
{
	notifications *receiver = user;
	hx_os_deadline deadline = hx_os_deadline_after_ms(10000);
	unsigned int seen = 0;
	while (seen != 3) {
		hx_ipc_message *message = NULL;
		hx_os_error error = {0};
		hx_os_status status = hx_ipc_receive_until(
			receiver->client, deadline, &message, &error);
		int ok = 1;
		if (!demo_check(status, &error, "receive JSON notification"))
			return NULL;
		if (message->kind == HX_IPC_DATA) {
			unsigned int bit = message->channel == 101 ? 1
				: message->channel == 102	   ? 2
								   : 0;
			hx_os_config_document *document =
				message->encoding == HX_IPC_UTF8
				? json_decode(message->data, message->bytes)
				: NULL;
			const hx_os_config_node *root =
				hx_os_config_root(document);
			ok = document && bit && !(seen & bit) &&
				json_has_text(root, "type", "changed") &&
				json_has_int(root, "device_id",
					bit == 1 ? 101 : 102) &&
				json_has_int(root, "interval_ms",
					bit == 1 ? 250 : 500);
			hx_os_config_release(document);
			if (ok) {
				seen |= bit;
				printf("[IPC notification] %.*s\n",
					(int)message->bytes,
					(const char *)message->data);
			}
		}
		else if (message->kind != HX_IPC_CONNECTED)
			ok = 0;
		hx_ipc_message_release(message);
		if (!ok) {
			fputs("Unexpected JSON notification or session "
			      "event.\n",
				stderr);
			return NULL;
		}
	}
	receiver->ok = 1;
	return NULL;
}

int main(int argc, char **argv)
{
	const char *address = argc > 1 ? argv[1] : "local://device-demo";
	hx_ipc *client = NULL;
	hx_os_error error = {0};
	hx_os_thread *threads[3] = {NULL};
	device_job jobs[2] = {{0}, {0}};
	notifications receiver = {0};
	hx_os_status status;
	unsigned int index, started = 0;
	int ok;
	if (strncmp(address, "local://", 8) && strncmp(address, "pipe://", 7) &&
		strncmp(address, "unix://", 7)) {
		fputs("This device demo only accepts native local IPC.\n",
			stderr);
		return 2;
	}
	status = hx_ipc_connect(address, &client, &error);
	if (demo_check(status, &error, "connect")) {
		receiver.client = client;
		status = hx_os_thread_create(NULL, receive_notifications,
			&receiver, &threads[0], &error);
		if (status == HX_OS_OK)
			started = 1;
		for (index = 0; index < 2 && status == HX_OS_OK; ++index) {
			jobs[index].client = client;
			jobs[index].device_id = 101 + index;
			jobs[index].interval_ms = index ? 500 : 250;
			status = hx_os_thread_create(NULL, device_work,
				&jobs[index], &threads[started], &error);
			if (status == HX_OS_OK)
				++started;
		}
	}
	ok = demo_check(status, &error, "start business threads");
	{
		hx_os_deadline deadline = hx_os_deadline_after_ms(15000);
		for (index = 0; index < started; ++index)
			demo_join(&threads[index], deadline);
	}
	ok = ok && receiver.ok && jobs[0].ok && jobs[1].ok;
	if (ok)
		ok = request_expected(client, 0, "quit", -1, "bye", -1);
	if (!demo_close(&client))
		ok = 0;
	if (ok)
		puts("DEVICE CLIENT PASS: 2 devices, 2 request threads, 2 "
		     "pushes");
	return ok ? 0 : 1;
}
