#include "udp_business.h"

typedef struct publish_job {
	udp_endpoint *endpoint;
	hx_os_address destination;
	int simulate_gaps;
	int ok;
} publish_job;

typedef struct feedback_job {
	udp_endpoint *endpoint;
	hx_os_address server;
	hx_os_deadline deadline;
	unsigned int received;
	int ok;
} feedback_job;

static int publish_sample(
	publish_job *job, unsigned int device, unsigned int sequence)
{
	char sample[UDP_TEXT_CAPACITY];
	int temperature = 2500 + (int)device * 100 + (int)sequence;
	(void)snprintf(sample, sizeof(sample),
		"SAMPLE device=%u seq=%u temperature_centi=%d", 101 + device,
		sequence, temperature);
	if (!udp_send_text(job->endpoint, &job->destination, sample))
		return 0;
	printf("[UDP send thread] %s\n", sample);
	return 1;
}

static void *HX_OS_CALL publish_samples(void *user)
{
	publish_job *job = user;
	unsigned int sequence, device;
	for (sequence = 1; sequence <= 10; ++sequence) {
		if (job->simulate_gaps && (sequence == 3 || sequence == 7)) {
			printf("[UDP injection] omit seq=%u\n", sequence);
			continue;
		}
		for (device = 0; device < 2; ++device) {
			if (!publish_sample(job, device, sequence))
				return NULL;
			if (job->simulate_gaps && sequence == 4 &&
				!publish_sample(job, device, 4))
				return NULL;
			if (job->simulate_gaps && sequence == 8 &&
				!publish_sample(job, device, 6))
				return NULL;
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
		char text[UDP_TEXT_CAPACITY];
		hx_os_address peer;
		hx_os_error error = {0};
		hx_os_status status = udp_receive_text(
			job->endpoint, &peer, text, job->deadline, &error);
		if (status == HX_OS_TIMEOUT) {
			/* Feedback is best effort. Missing UDP is not retried.
			 */
			job->ok = 1;
			return NULL;
		}
		if (!net_demo_check(status, &error, "UDP feedback"))
			return NULL;
		if (!net_demo_same_peer(&peer, &job->server))
			continue;
		++job->received;
		printf("[UDP receive thread] %s\n", text);
	}
}

int main(int argc, char **argv)
{
	uint16_t port = argc > 1 ? net_demo_port(argv[1]) : 39002;
	hx_os_network *network = NULL;
	udp_endpoint endpoint = {0};
	hx_os_thread *threads[2] = {NULL};
	hx_os_error error = {0};
	publish_job publisher = {0};
	feedback_job feedback = {0};
	hx_os_status status;
	unsigned int started = 0, index;
	int ok;
	if (argc > 3 || (argc == 3 && strcmp(argv[2], "--simulate-gaps")))
		return 2;
	ok = net_demo_open(&network, &endpoint.socket, HX_OS_UDP, 0);
	if (ok) {
		status = hx_os_mutex_create(NULL, 0, &endpoint.access, &error);
		if (status == HX_OS_OK)
			status = hx_os_address_parse("127.0.0.1", port,
				&publisher.destination, &error);
		ok = net_demo_check(status, &error, "UDP setup");
	}
	if (ok) {
		publisher.endpoint = feedback.endpoint = &endpoint;
		feedback.server = publisher.destination;
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
		ok = net_demo_check(status, &error, "start UDP threads");
	}
	for (index = 0; index < started; ++index)
		net_demo_join(&threads[index], hx_os_deadline_after_ms(12000));
	ok = ok && publisher.ok && feedback.ok;
	hx_os_mutex_destroy(endpoint.access);
	if (!net_demo_close(&endpoint.socket, &network))
		ok = 0;
	if (ok)
		printf("UDP SENSOR PASS: send/receive threads joined; "
		       "feedback=%u\n",
			feedback.received);
	return ok ? 0 : 1;
}
