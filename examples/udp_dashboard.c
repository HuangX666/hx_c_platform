#include "udp_business.h"

typedef struct sensor_state {
	hx_os_address peer;
	unsigned int accepted;
	unsigned int newest;
	unsigned int gaps;
	unsigned int stale;
	int temperature_centi;
} sensor_state;

static int receive_sample(udp_endpoint *dashboard, const char *sample,
	const hx_os_address *peer, sensor_state sensors[2])
{
	unsigned int device, sequence;
	int temperature, consumed = -1;
	sensor_state *sensor;
	char feedback[UDP_TEXT_CAPACITY];
	if (sscanf(sample, "SAMPLE device=%9u seq=%9u temperature_centi=%9d%n",
		    &device, &sequence, &temperature, &consumed) != 3 ||
		consumed < 0 || (size_t)consumed != strlen(sample) ||
		device < 101 || device > 102 || !sequence || sequence > 10 ||
		temperature < -10000 || temperature > 20000)
		return 1;
	sensor = &sensors[device - 101];
	if (sensor->accepted && !net_demo_same_peer(peer, &sensor->peer))
		return 1;
	sensor->peer = *peer;
	if (sequence <= sensor->newest) {
		++sensor->stale;
		return 1;
	}
	/* An observed gap is not a measurement of permanent network loss. */
	sensor->gaps += sequence - sensor->newest - 1;
	sensor->newest = sequence;
	sensor->temperature_centi = temperature;
	++sensor->accepted;
	printf("[UDP dashboard] %s\n", sample);
	if (sensor->accepted % 3)
		return 1;
	(void)snprintf(feedback, sizeof(feedback), "LATEST device=%u seq=%u",
		device, sequence);
	return udp_send_text(dashboard, peer, feedback);
}

int main(int argc, char **argv)
{
	uint16_t port = argc > 1 ? net_demo_port(argv[1]) : 39002;
	hx_os_network *network = NULL;
	udp_endpoint dashboard = {0};
	hx_os_error error = {0};
	sensor_state sensors[2] = {{0}, {0}};
	hx_os_deadline deadline = hx_os_deadline_after_ms(10000);
	unsigned int index;
	int collecting = 0;
	int ok = net_demo_open(&network, &dashboard.socket, HX_OS_UDP, port);
	if (ok)
		ok = net_demo_check(hx_os_mutex_create(
					    NULL, 0, &dashboard.access, &error),
			     &error, "UDP mutex") &&
			net_demo_listening(dashboard.socket);
	while (ok) {
		char sample[UDP_TEXT_CAPACITY];
		hx_os_address peer;
		hx_os_status status = udp_receive_text(
			&dashboard, &peer, sample, deadline, &error);
		if (status == HX_OS_TIMEOUT && collecting)
			break;
		ok = net_demo_check(status, &error, "UDP sample");
		if (!ok)
			break;
		if (!collecting) {
			collecting = 1;
			deadline = hx_os_deadline_after_ms(1000);
		}
		ok = receive_sample(&dashboard, sample, &peer, sensors);
	}
	for (index = 0; index < 2 && ok; ++index) {
		const sensor_state *sensor = &sensors[index];
		char summary[UDP_TEXT_CAPACITY];
		(void)snprintf(summary, sizeof(summary),
			"SUMMARY device=%u accepted=%u newest=%u gaps=%u "
			"stale=%u",
			101 + index, sensor->accepted, sensor->newest,
			sensor->gaps, sensor->stale);
		printf("[UDP dashboard] %s temperature_centi=%d\n", summary,
			sensor->temperature_centi);
		if (sensor->accepted)
			ok = udp_send_text(&dashboard, &sensor->peer, summary);
	}
	hx_os_mutex_destroy(dashboard.access);
	if (!net_demo_close(&dashboard.socket, &network))
		ok = 0;
	if (ok)
		puts("UDP DASHBOARD PASS: collection window completed");
	return ok ? 0 : 1;
}
