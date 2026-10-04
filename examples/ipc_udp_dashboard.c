#include "ipc_demo.h"

typedef struct sensor_state {
	hx_ipc_peer peer;
	unsigned int accepted;
	unsigned int newest;
	unsigned int gaps;
	unsigned int stale;
	int temperature_centi;
} sensor_state;

static int receive_sample(hx_ipc *dashboard, const hx_ipc_message *message,
	sensor_state sensors[2], hx_os_error *error)
{
	unsigned int sequence;
	int temperature, consumed = -1;
	sensor_state *sensor;
	char feedback[64];
	if (message->channel < 101 || message->channel > 102 ||
		message->encoding != HX_IPC_UTF8 || message->bytes > 64)
		return 1;
	/* Widths bound numeric conversion; verify the entire message was used.
	 */
	if (sscanf(message->data, "SAMPLE seq=%9u temperature_centi=%9d%n",
		    &sequence, &temperature, &consumed) != 2 ||
		consumed < 0 || (size_t)consumed != message->bytes ||
		!sequence || sequence > 10 || temperature < -10000 ||
		temperature > 20000)
		return 1;
	sensor = &sensors[message->channel - 101];
	if (sensor->peer && sensor->peer != message->peer)
		return 1;
	sensor->peer = message->peer;
	if (sequence <= sensor->newest) {
		++sensor->stale;
		return 1;
	}
	/* Latest-state policy: gaps are observations, not a network loss rate.
	 */
	sensor->gaps += sequence - sensor->newest - 1;
	sensor->newest = sequence;
	sensor->temperature_centi = temperature;
	++sensor->accepted;
	printf("[dashboard receive] device=%llu seq=%u temperature_centi=%d\n",
		(unsigned long long)message->channel, sequence, temperature);
	if (sensor->accepted % 3)
		return 1;
	(void)snprintf(feedback, sizeof(feedback), "LATEST seq=%u", sequence);
	return demo_check(hx_ipc_send_text_until(dashboard, message->peer,
				  message->channel, feedback,
				  hx_os_deadline_after_ms(1000), error),
		error, "send UDP feedback");
}

int main(int argc, char **argv)
{
	const char *address = argc > 1 ? argv[1] : "udp://127.0.0.1:39002";
	hx_ipc *dashboard = NULL;
	hx_string actual = HX_STRING_INIT;
	hx_os_error error = {0};
	sensor_state sensors[2] = {{0}, {0}};
	hx_os_deadline deadline = hx_os_deadline_after_ms(10000);
	hx_os_status status;
	unsigned int index;
	int collecting = 0, ok;
	if (strncmp(address, "udp://", 6))
		return 2;
	status = hx_ipc_listen(address, &dashboard, &error);
	if (status == HX_OS_OK)
		status = hx_ipc_address(dashboard, &actual, &error);
	ok = demo_check(status, &error, "listen UDP");
	if (ok) {
		printf("Listening: %s\n", hx_string_c_str(&actual));
		fflush(stdout);
	}
	while (ok) {
		hx_ipc_message *message = NULL;
		status = hx_ipc_receive_until(
			dashboard, deadline, &message, &error);
		if (status == HX_OS_TIMEOUT && collecting)
			break;
		ok = demo_check(status, &error, "receive UDP sample");
		if (!ok)
			break;
		if (message->kind == HX_IPC_DATA) {
			if (!collecting) {
				collecting = 1;
				deadline = hx_os_deadline_after_ms(1000);
			}
			ok = receive_sample(
				dashboard, message, sensors, &error);
		}
		else if (message->kind == HX_IPC_OVERFLOW) {
			puts("[dashboard] event overflow; state needs "
			     "resynchronization");
			ok = 0;
		}
		hx_ipc_message_release(message);
	}
	for (index = 0; index < 2 && ok; ++index) {
		const sensor_state *sensor = &sensors[index];
		char summary[96];
		(void)snprintf(summary, sizeof(summary),
			"SUMMARY accepted=%u newest=%u gaps=%u stale=%u",
			sensor->accepted, sensor->newest, sensor->gaps,
			sensor->stale);
		printf("[dashboard summary] device=%u %s "
		       "temperature_centi=%d\n",
			101 + index, summary, sensor->temperature_centi);
		if (sensor->peer)
			ok = demo_check(
				hx_ipc_send_text_until(dashboard, sensor->peer,
					101 + index, summary,
					hx_os_deadline_after_ms(1000), &error),
				&error, "send UDP summary");
	}
	if (!demo_close(&dashboard))
		ok = 0;
	hx_string_destroy(&actual);
	if (ok)
		puts("UDP DASHBOARD PASS: collection window completed");
	return ok ? 0 : 1;
}
