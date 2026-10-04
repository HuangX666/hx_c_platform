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
	hx_os_config_document *document = json_decode(sample, strlen(sample));

	const hx_os_config_node *root = hx_os_config_root(document);

	int64_t device, sequence, temperature;
	sensor_state *sensor;

	int valid = document && json_has_text(root, "type", "sample") &&
		json_get_int(root, "device_id", &device) && device >= 101 &&
		device <= 102 && json_get_int(root, "seq", &sequence) &&
		sequence > 0 && sequence <= 10 &&
		json_get_int(root, "temperature_centi", &temperature) &&
		temperature >= -10000 && temperature <= 20000;

	hx_os_config_release(document);
	if (!valid)
		return 1;

	sensor = &sensors[device - 101];
	if (sensor->accepted && !net_demo_same_peer(peer, &sensor->peer))
		return 1;

	sensor->peer = *peer;
	if ((unsigned int)sequence <= sensor->newest) {
		++sensor->stale;

		return 1;
	}

	sensor->gaps += (unsigned int)sequence - sensor->newest - 1;
	sensor->newest = (unsigned int)sequence;
	sensor->temperature_centi = (int)temperature;
	++sensor->accepted;
	printf("[UDP dashboard] %s\n", sample);
	if (sensor->accepted % 3)
		return 1;

	{
		json_field fields[] = {json_text("type", "latest"),
			json_int("device_id", device),
			json_int("seq", sequence)};

		hx_string wire = HX_STRING_INIT;

		int ok = json_encode(fields, 3, &wire) &&
			udp_send_text(dashboard, peer, hx_string_c_str(&wire));

		hx_string_destroy(&wire);

		return ok;
	}
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

		json_field fields[] = {json_text("type", "summary"),
			json_int("device_id", 101 + index),
			json_int("accepted", sensor->accepted),
			json_int("newest", sensor->newest),
			json_int("gaps", sensor->gaps),
			json_int("stale", sensor->stale),
			json_int("temperature_centi",
				sensor->temperature_centi)};

		hx_string wire = HX_STRING_INIT;

		ok = json_encode(fields, 7, &wire);
		if (ok) {
			printf("[UDP summary] %s\n", hx_string_c_str(&wire));
			if (sensor->accepted)
				ok = udp_send_text(&dashboard, &sensor->peer,
					hx_string_c_str(&wire));
		}

		hx_string_destroy(&wire);
	}

	hx_os_mutex_destroy(dashboard.access);
	if (!net_demo_close(&dashboard.socket, &network))
		ok = 0;

	if (ok)
		puts("UDP DASHBOARD PASS: collection window completed");

	return ok ? 0 : 1;
}
