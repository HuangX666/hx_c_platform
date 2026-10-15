#include <hx_os.h>
#include "network_demo.h"

typedef struct lookup_state {
	hx_os_resolver_options options;
	hx_os_address_list *addresses;
	const char *host;
	hx_os_deadline deadline;
	hx_os_error error;
	int step;
} lookup_state;

static void resolved(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	lookup_state *state = user;

	state->error = completion->error;
	state->addresses = hx_os_request_take_addresses(request);
}

static hx_os_status resolve_in_coroutine(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	lookup_state *state = user;
	hx_os_status status = HX_OS_OK;

	(void)self;
	HX_OS_CORO_BEGIN(state->step);
	HX_OS_CORO_AWAIT(state->step, status,
		hx_os_coro_resolve_cancellable(state->host, 443,
			&state->options, state->deadline, &state->addresses,
			error));
	HX_OS_CORO_END(state->step);
	return hx_os_coro_complete(status, error);
}

int main(int argc, char **argv)
{
	lookup_state state = {0};
	hx_os_resolver *resolver = NULL;
	hx_os_loop *loop = NULL;
	hx_os_request *request = NULL;
	hx_os_coro_runtime *runtime = NULL;
	hx_os_coro *task = NULL;
	hx_os_status status, closed;
	size_t i;
	int first = 1, mode = 0, ok;

	if (argc > 1 && !strcmp(argv[1], "--async")) {
		mode = 1;
		++first;
	}
	else if (argc > 1 && !strcmp(argv[1], "--coro")) {
		mode = 2;
		++first;
	}
	if (argc < first + 1 || argc > first + 2) {
		fputs("Usage: dns_lookup [--async|--coro] HOST [DNS_IP:PORT]\n",
			stderr);
		return 2;
	}
	(void)hx_os_resolver_options_init(
		&state.options, sizeof(state.options));
	state.host = argv[first];
	if (argc == first + 2)
		state.options.servers = argv[first + 1];
	state.deadline = hx_os_deadline_after_ms(2000);

	if (mode == 1) {
		status = hx_os_loop_create_local(
			NULL, NULL, &loop, &state.error);
		if (status == HX_OS_OK)
			status = hx_os_loop_resolve(loop, state.host, 443,
				HX_OS_TCP, &state.options, state.deadline,
				resolved, &state, &request, &state.error);
		if (status == HX_OS_OK)
			status = hx_os_loop_run_until(
				loop, hx_os_deadline_after_ms(3000), NULL);
	}
	else if (mode == 2) {
		status = hx_os_coro_runtime_create(
			NULL, NULL, &runtime, &state.error);
		if (status == HX_OS_OK)
			status = hx_os_coro_go(runtime, HX_OS_CORO_STACKLESS,
				resolve_in_coroutine, &state, NULL, &task,
				&state.error);
		if (status == HX_OS_OK)
			status = hx_os_coro_wait_until(task,
				hx_os_deadline_after_ms(3000), &state.error,
				NULL);
	}
	else {
		status = hx_os_resolver_create(
			NULL, &state.options, &resolver, &state.error);
		if (status == HX_OS_OK)
			status = hx_os_resolver_resolve_until(resolver,
				state.host, 443, HX_OS_TCP, state.deadline,
				NULL, &state.addresses, &state.error);
	}
	if (status == HX_OS_OK)
		status = state.error.status;
	ok = net_demo_check(status, &state.error, "resolve host");

	// Retire callbacks before releasing their state, including on timeout.
	if (loop) {
		closed = hx_os_loop_close_until(
			loop, hx_os_deadline_after_ms(2000), NULL);
		if (closed != HX_OS_OK) {
			fputs("Loop close pending; keep owners to retry.\n",
				stderr);
			return 1;
		}
		hx_os_request_release(request);
		(void)hx_os_loop_destroy(&loop, NULL);
	}
	if (runtime) {
		hx_os_coro_cancel(task);
		closed = hx_os_coro_runtime_close_until(
			runtime, hx_os_deadline_after_ms(2000), NULL);
		if (closed != HX_OS_OK) {
			fputs("Runtime close pending; keep owners to retry.\n",
				stderr);
			return 1;
		}
		hx_os_coro_release(task);
		(void)hx_os_coro_runtime_destroy(&runtime, NULL);
	}
	for (i = 0; ok && i < hx_os_address_list_count(state.addresses); ++i) {
		const hx_os_address *address =
			hx_os_address_list_get(state.addresses, i);
		char text[64];

		status = hx_os_address_format(
			address, text, sizeof(text), &state.error);
		ok = net_demo_check(status, &state.error, "format address");
		if (ok)
			printf("IPv%u %s port=%u\n",
				(unsigned int)address->family, text,
				(unsigned int)address->port);
	}
	hx_os_address_list_destroy(state.addresses);
	if (resolver) {
		status = hx_os_resolver_destroy(&resolver, &state.error);
		if (!net_demo_check(status, &state.error, "close resolver"))
			ok = 0;
	}

	return ok ? 0 : 1;
}
