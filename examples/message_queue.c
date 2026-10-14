#include <hx_os_queue.h>
#include <hx_os_thread.h>
#include <stdio.h>

typedef struct device_sample {
	uint32_t device_id;
	uint32_t sequence;
	double value;
} device_sample;

typedef struct producer_state {
	hx_os_message_queue *queue;
	hx_os_deadline deadline;
	size_t sent;
	hx_os_status send_status;
	hx_os_status close_status;
} producer_state;

static int report_failure(const char *operation, hx_os_status status)
{
	if (status == HX_OS_OK)
		return 0;

	fprintf(stderr, "%s failed: %s\n", operation,
		hx_os_status_name(status));

	return 1;
}

static void *HX_OS_CALL produce_samples(void *user)
{
	producer_state *state = user;
	device_sample samples[6];
	size_t i;

	for (i = 0; i < 6; ++i) {
		samples[i].device_id = 17;
		samples[i].sequence = (uint32_t)(i + 1);
		samples[i].value = 20.0 + (double)(i + 1) * 0.5;
	}

	state->send_status = hx_os_message_queue_send_many_until(state->queue,
		samples, 6, state->deadline, &state->sent, NULL);

	state->close_status = hx_os_message_queue_close(state->queue, NULL);

	return NULL;
}

int main(void)
{
	// Static state remains valid even if a bounded join cannot finish.
	static producer_state state;
	static hx_os_thread *producer;
	hx_os_status status, close_status;
	int failed = 0;

	status = hx_os_message_queue_create(
		NULL, sizeof(device_sample), 2, &state.queue, NULL);
	if (report_failure("Create queue", status))
		return 1;

	state.deadline = hx_os_deadline_after_ms(5000);
	status = hx_os_thread_create(
		NULL, produce_samples, &state, &producer, NULL);
	failed |= report_failure("Create producer", status);

	if (status == HX_OS_OK) {
		for (;;) {
			device_sample samples[3];
			size_t received = 0, i;

			status = hx_os_message_queue_receive_many_until(
				state.queue, samples, 3, state.deadline,
				&received, NULL);
			for (i = 0; i < received; ++i)
				printf("Device %lu, sample %lu: %.1f\n",
					(unsigned long)samples[i].device_id,
					(unsigned long)samples[i].sequence,
					samples[i].value);

			if (status == HX_OS_EOF)
				break;
			if (report_failure("Receive samples", status)) {
				failed = 1;
				break;
			}
		}
	}

	// Also wake a blocked sender if receiving failed.
	close_status = hx_os_message_queue_close(state.queue, NULL);
	failed |= report_failure("Close queue", close_status);

	if (producer) {
		status = hx_os_thread_join_until(producer,
			hx_os_deadline_after_ms(2000), NULL, NULL);
		if (report_failure("Join producer", status)) {
			fprintf(stderr, "Thread and queue retained; retry join "
					"before releasing them.\n");

			return 1;
		}

		failed |= report_failure("Send samples", state.send_status);
		if (state.sent != 6)
			fprintf(stderr, "Accepted %zu of 6 samples\n",
				state.sent);
		failed |= report_failure("Producer close", state.close_status);
		status = hx_os_thread_destroy(&producer, NULL);
		failed |= report_failure("Destroy producer", status);
	}

	status = hx_os_message_queue_destroy(&state.queue, NULL);
	failed |= report_failure("Destroy queue", status);

	return failed ? 1 : 0;
}
