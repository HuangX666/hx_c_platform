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
	json_field fields[] = {json_text("type", "sample"),
		json_int("device_id", 101 + device), json_int("seq", sequence),
		json_int("temperature_centi", 2500 + device * 100 + sequence)};
	hx_string wire = HX_STRING_INIT;
	int ok = json_encode(fields, 4, &wire) &&
		udp_send_text(job->endpoint, &job->destination,
			hx_string_c_str(&wire));
	if (ok)
		printf("[UDP send thread] %s\n", hx_string_c_str(&wire));
	hx_string_destroy(&wire);
	return ok;
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
		hx_os_config_document *document;
		const hx_os_config_node *root;
		int64_t device, sequence, accepted, gaps, stale, temperature;
		int ok;
		hx_os_status status = udp_receive_text(
			job->endpoint, &peer, text, job->deadline, &error);
		if (status == HX_OS_TIMEOUT) {
			job->ok = 1;
			return NULL;
		}
		if (!net_demo_check(status, &error, "UDP feedback"))
			return NULL;
		if (!net_demo_same_peer(&peer, &job->server))
			continue;
		document = json_decode(text, strlen(text));
		root = hx_os_config_root(document);
		ok = document && json_get_int(root, "device_id", &device) &&
			(device == 101 || device == 102);
		if (json_has_text(root, "type", "latest"))
			ok = ok && json_get_int(root, "seq", &sequence) &&
				sequence > 0 && sequence <= 10;
		else if (json_has_text(root, "type", "summary"))
			ok = ok && json_get_int(root, "newest", &sequence) &&
				sequence > 0 && sequence <= 10 &&
				json_get_int(root, "accepted", &accepted) &&
				accepted > 0 && accepted <= 10 &&
				json_get_int(root, "gaps", &gaps) &&
				gaps >= 0 && gaps <= 10 &&
				json_get_int(root, "stale", &stale) &&
				stale >= 0 && stale <= 10 &&
				json_get_int(root, "temperature_centi",
					&temperature) &&
				temperature >= -10000 && temperature <= 20000;
		else
			ok = 0;
		hx_os_config_release(document);
		if (!ok) {
			fputs("Invalid JSON feedback.\n", stderr);
			return NULL;
		}
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
