#include <hx_os.h>
#include <stdio.h>
#include <string.h>

typedef struct udp_state {
	hx_os_coro_stream *socket;
	hx_os_address address;
	hx_os_address peer;
	hx_os_deadline deadline;
	size_t completed;
	char received[64];
	int step;
} udp_state;

static const char g_udp_message[] = "{\"type\":\"status\",\"device_id\":7}";

// Persistent state keeps buffers alive while a C99 task is suspended.
static hx_os_status exchange(hx_os_coro *self, void *user, hx_os_error *error)
{
	udp_state *state = user;
	hx_os_status status = HX_OS_OK;

	(void)self;
	HX_OS_CORO_BEGIN(state->step);
	HX_OS_CORO_AWAIT(state->step, status,
		hx_os_coro_udp_bind(&state->address, 0, &state->socket,
			&state->address, error));
	if (status != HX_OS_OK)
		goto done;

	// Loop back to our own ephemeral port; no second process is required.
	HX_OS_CORO_AWAIT(state->step, status,
		hx_os_coro_send_to(state->socket, &state->address,
			g_udp_message, sizeof(g_udp_message), state->deadline,
			&state->completed, error));
	if (status != HX_OS_OK)
		goto done;

	HX_OS_CORO_AWAIT(state->step, status,
		hx_os_coro_receive_from(state->socket, &state->peer,
			state->received, sizeof(state->received),
			state->deadline, &state->completed, error));
	if (status != HX_OS_OK)
		goto done;

	if (state->completed != sizeof(g_udp_message) ||
		memcmp(state->received, g_udp_message, sizeof(g_udp_message))) {
		status = HX_OS_INVALID;
		goto done;
	}
	printf("UDP JSON: %s\n", state->received);
	HX_OS_CORO_END(state->step);

done:
	return hx_os_coro_complete(status, error);
}

static void cleanup(void *user)
{
	udp_state *state = user;

	hx_os_coro_stream_release(state->socket);
	state->socket = NULL;
}

int main(void)
{
	hx_os_coro_runtime *runtime = NULL;
	hx_os_coro *task = NULL;
	hx_os_error result = {HX_OS_OK, 0, 0, 0};
	hx_os_status status, closed;
	udp_state state;

	memset(&state, 0, sizeof(state));
	status = hx_os_coro_runtime_create(NULL, NULL, &runtime, NULL);
	if (status != HX_OS_OK) {
		fprintf(stderr, "Create runtime: %s\n",
			hx_os_status_name(status));
		if (runtime) {
			closed = hx_os_coro_runtime_close_until(runtime,
				hx_os_deadline_after_ms(2000), NULL);
			if (closed == HX_OS_OK)
				(void)hx_os_coro_runtime_destroy(
					&runtime, NULL);
			else
				fputs("Cleanup pending; keep owner to retry.\n",
					stderr);
		}

		return 1;
	}
	status = hx_os_address_parse("127.0.0.1", 0, &state.address, NULL);
	state.deadline = hx_os_deadline_after_ms(2000);
	if (status == HX_OS_OK)
		status = hx_os_coro_go(runtime, HX_OS_CORO_STACKLESS, exchange,
			&state, cleanup, &task, NULL);

	if (status == HX_OS_OK)
		status = hx_os_coro_wait_until(
			task, hx_os_deadline_after_ms(3000), &result, NULL);

	if (status != HX_OS_OK || result.status != HX_OS_OK)
		fprintf(stderr, "UDP task: %s\n",
			hx_os_status_name(
				status != HX_OS_OK ? status : result.status));

	hx_os_coro_release(task);
	closed = hx_os_coro_runtime_close_until(
		runtime, hx_os_deadline_after_ms(2000), NULL);
	if (closed != HX_OS_OK) {
		fprintf(stderr, "Close: %s; keep owner to retry\n",
			hx_os_status_name(closed));

		return 1;
	}
	(void)hx_os_coro_runtime_destroy(&runtime, NULL);

	return status == HX_OS_OK && result.status == HX_OS_OK ? 0 : 1;
}
