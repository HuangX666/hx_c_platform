#include "ipc_demo.h"

typedef struct publish_job {
	hx_ipc *client;
	int simulate_gaps;
	int ok;
} publish_job;

typedef struct feedback_job {
	hx_ipc *client;
	hx_os_deadline deadline;
	unsigned int received;
	int ok;
} feedback_job;

static int publish_sample(
	publish_job *job, unsigned int device, unsigned int sequence)
{
	char sample[64];
	hx_os_error error = {0};
	int temperature = 2500 + (int)device * 100 + (int)sequence;
	(void)snprintf(sample, sizeof(sample),
		"SAMPLE seq=%u temperature_centi=%d", sequence, temperature);
	if (!demo_check(hx_ipc_send_text_until(job->client, 0, 101 + device,
				sample, hx_os_deadline_after_ms(1000), &error),
		    &error, "publish UDP sample"))
		return 0;
	printf("[send thread] device=%u %s\n", 101 + device, sample);
	return 1;
}

static void *HX_OS_CALL publish_samples(void *user)
{
	publish_job *job = user;
	unsigned int sequence, device;
	for (sequence = 1; sequence <= 10; ++sequence) {
		if (job->simulate_gaps && (sequence == 3 || sequence == 7)) {
			printf("[demo injection] omit seq=%u for both "
			       "devices\n",
				sequence);
			continue;
		}
		for (device = 0; device < 2; ++device) {
			if (!publish_sample(job, device, sequence))
				return NULL;
			if (job->simulate_gaps && sequence == 4) {
				puts("[demo injection] duplicate seq=4");
				if (!publish_sample(job, device, 4))
					return NULL;
			}
			if (job->simulate_gaps && sequence == 8) {
				puts("[demo injection] late seq=6 after seq=8");
				if (!publish_sample(job, device, 6))
					return NULL;
			}
		}
		(void)hx_os_sleep_until(hx_os_deadline_after_ms(30), NULL);
	}
	job->ok = 1;
	return NULL;
}

static void *HX_OS_CALL receive_feedback(void *user)
{
	feedback_job *job = user;
	for (;;) {
		hx_ipc_message *message = NULL;
		hx_os_error error = {0};
		hx_os_status status = hx_ipc_receive_until(
			job->client, job->deadline, &message, &error);
		if (status == HX_OS_TIMEOUT) {
			/* Missing feedback is valid UDP behavior; never wait
			 * forever. */
			job->ok = 1;
			return NULL;
		}
		if (!demo_check(status, &error, "receive UDP feedback"))
			return NULL;
		if (message->kind == HX_IPC_DATA &&
			message->encoding == HX_IPC_UTF8 &&
			message->bytes <= 96 &&
			(message->channel == 101 || message->channel == 102)) {
			++job->received;
			printf("[feedback thread] device=%llu %.*s\n",
				(unsigned long long)message->channel,
				(int)message->bytes,
				(const char *)message->data);
		}
		else if (message->kind != HX_IPC_CONNECTED) {
			fputs("Unexpected UDP event or feedback.\n", stderr);
			hx_ipc_message_release(message);
			return NULL;
		}
		hx_ipc_message_release(message);
	}
}

int main(int argc, char **argv)
{
	const char *address = argc > 1 ? argv[1] : "udp://127.0.0.1:39002";
	hx_ipc *client = NULL;
	hx_os_thread *threads[2] = {NULL};
	hx_os_error error = {0};
	publish_job publisher = {0};
	feedback_job feedback = {0};
	hx_os_status status;
	unsigned int started = 0, index;
	int ok;
	if (strncmp(address, "udp://", 6) || argc > 3 ||
		(argc == 3 && strcmp(argv[2], "--simulate-gaps")))
		return 2;
	status = hx_ipc_connect(address, &client, &error);
	if (demo_check(status, &error, "connect UDP session")) {
		publisher.client = feedback.client = client;
		publisher.simulate_gaps = argc == 3;
		feedback.deadline = hx_os_deadline_after_ms(2000);
		status = hx_os_thread_create(
			NULL, receive_feedback, &feedback, &threads[0], &error);
		if (status == HX_OS_OK) {
			started = 1;
			status = hx_os_thread_create(NULL, publish_samples,
				&publisher, &threads[1], &error);
			if (status == HX_OS_OK)
				started = 2;
		}
	}
	ok = demo_check(status, &error, "start UDP business threads");
	{
		hx_os_deadline deadline = hx_os_deadline_after_ms(10000);
		for (index = 0; index < started; ++index)
			demo_join(&threads[index], deadline);
	}
	ok = ok && publisher.ok && feedback.ok;
	if (!demo_close(&client))
		ok = 0;
	if (ok)
		printf("UDP SENSOR PASS: send/receive threads joined; "
		       "feedback=%u\n",
			feedback.received);
	return ok ? 0 : 1;
}
