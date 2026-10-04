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
	const char *command, const char *expected)
{
	hx_ipc_message *reply = NULL;
	hx_os_error error = {0};
	hx_os_status status = hx_ipc_request_text_until(client, 0, device_id,
		command, hx_os_deadline_after_ms(5000), &reply, &error);
	int ok = demo_check(status, &error, command);
	if (ok) {
		ok = reply->channel == device_id &&
			demo_message_is(reply, expected);
		printf("[%s] device=%llu %s -> %.*s\n",
			device_id ? "request thread" : "main",
			(unsigned long long)device_id, command,
			(int)(reply->bytes > 160 ? 160 : reply->bytes),
			(const char *)reply->data);
		if (!ok)
			fprintf(stderr, "Expected reply: %s\n", expected);
	}
	hx_ipc_message_release(reply);
	return ok;
}

static void *HX_OS_CALL device_work(void *user)
{
	device_job *job = user;
	char command[64], applied[64], state[64];
	(void)snprintf(command, sizeof(command), "SET %u", job->interval_ms);
	(void)snprintf(applied, sizeof(applied), "OK interval_ms=%u",
		job->interval_ms);
	(void)snprintf(
		state, sizeof(state), "STATE interval_ms=%u", job->interval_ms);
	/* The result of each business operation is checked before the next. */
	job->ok = request_expected(
			  job->client, job->device_id, "OPEN", "OK online") &&
		request_expected(job->client, job->device_id, "GET",
			"STATE interval_ms=1000") &&
		request_expected(
			job->client, job->device_id, command, applied) &&
		request_expected(job->client, job->device_id, "GET", state) &&
		request_expected(job->client, job->device_id, "SET 0",
			"ERROR interval must be 50..5000") &&
		request_expected(job->client, job->device_id, "GET", state) &&
		request_expected(
			job->client, job->device_id, "CLOSE", "OK offline");
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
		if (!demo_check(status, &error, "receive notification"))
			return NULL;
		if (message->kind == HX_IPC_DATA) {
			unsigned int bit = message->channel == 101 ? 1
				: message->channel == 102	   ? 2
								   : 0;
			const char *expected = bit == 1
				? "CHANGED interval_ms=250"
				: "CHANGED interval_ms=500";
			if (!bit || (seen & bit) ||
				!demo_message_is(message, expected)) {
				fputs("Unexpected business notification.\n",
					stderr);
				hx_ipc_message_release(message);
				return NULL;
			}
			seen |= bit;
			printf("[receive thread] device=%llu %s\n",
				(unsigned long long)message->channel, expected);
		}
		else if (message->kind != HX_IPC_CONNECTED) {
			fputs("Session ended before all notifications "
			      "arrived.\n",
				stderr);
			hx_ipc_message_release(message);
			return NULL;
		}
		hx_ipc_message_release(message);
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
		ok = request_expected(client, 0, "QUIT", "BYE");
	if (!demo_close(&client))
		ok = 0;
	if (ok)
		puts("DEVICE CLIENT PASS: 2 devices, 2 request threads, 2 "
		     "pushes");
	return ok ? 0 : 1;
}
