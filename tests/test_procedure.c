#include "utility_test.h"
typedef struct proc_state {
	hx_os_proc_handler *handler;
	hx_os_proc_registration registration;
	hx_os_event *entered, *release;
	size_t cleaned, calls;
} proc_state;
static hx_os_status HX_OS_CALL add_one(
	hx_os_calldata *data, void *user, hx_os_error *error)
{
	int64_t value;
	(void)user;
	hx_os_status status =
		hx_os_calldata_get_i64(data, "value", &value, error);
	if (status != HX_OS_OK)
		return status;
	return hx_os_calldata_set_i64(data, "value", value + 1, error);
}
static hx_os_status HX_OS_CALL fail_call(
	hx_os_calldata *data, void *user, hx_os_error *error)
{
	CHECK(add_one(data, user, error) == HX_OS_OK);
	error->status = HX_OS_IO_ERROR;
	error->domain = HX_OS_ERROR_ERRNO;
	error->native_code = 456;
	return HX_OS_IO_ERROR;
}
static void HX_OS_CALL cleanup(void *user)
{
	proc_state *state = user;
	++state->cleaned;
}
static hx_os_status HX_OS_CALL self_call(
	hx_os_calldata *data, void *user, hx_os_error *error)
{
	proc_state *state = user;
	(void)data;
	(void)error;
	CHECK(hx_os_proc_unregister_until(state->handler, state->registration,
		      HX_OS_DEADLINE_INFINITE, NULL) == HX_OS_DEADLOCK);
	CHECK(hx_os_proc_close_until(&state->handler, HX_OS_DEADLINE_INFINITE,
		      NULL) == HX_OS_DEADLOCK);
	return HX_OS_OK;
}
static hx_os_status HX_OS_CALL recursive_call(
	hx_os_calldata *data, void *user, hx_os_error *error)
{
	proc_state *state = user;
	++state->calls;
	return hx_os_proc_call(state->handler, "recurse", data, error);
}
static hx_os_status HX_OS_CALL blocked_call(
	hx_os_calldata *data, void *user, hx_os_error *error)
{
	proc_state *state = user;
	(void)data;
	(void)error;
	hx_os_event_set(state->entered);
	CHECK(hx_os_event_wait_until(state->release,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	return HX_OS_OK;
}
static void *HX_OS_CALL call_thread(void *user)
{
	proc_state *state = user;
	CHECK(hx_os_proc_call(state->handler, "blocked", NULL, NULL) ==
		HX_OS_OK);
	return NULL;
}
static void test_calls(void)
{
	proc_state state = {0};
	hx_os_proc_options options = HX_OS_PROC_OPTIONS_INIT;
	hx_os_calldata data = HX_OS_CALLDATA_INIT;
	hx_os_error error;
	int64_t value;
	CHECK(hx_os_proc_create(NULL, &state.handler, NULL) == HX_OS_OK);
	CHECK(hx_os_proc_register(state.handler, "add", add_one, NULL, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_proc_register(state.handler, "add", add_one, NULL, NULL) ==
		HX_OS_EXISTS);
	CHECK(hx_os_proc_call(state.handler, "absent", NULL, NULL) ==
		HX_OS_NOT_FOUND);
	CHECK(hx_os_calldata_set_i64(&data, "value", 41, NULL) == HX_OS_OK);
	CHECK(hx_os_proc_call(state.handler, "add", &data, NULL) == HX_OS_OK);
	CHECK(hx_os_calldata_get_i64(&data, "value", &value, NULL) ==
			HX_OS_OK &&
		value == 42);
	CHECK(hx_os_proc_register(state.handler, "fail", fail_call, NULL,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_proc_call(state.handler, "fail", &data, &error) ==
		HX_OS_IO_ERROR);
	CHECK(error.native_code == 456);
	CHECK(hx_os_calldata_get_i64(&data, "value", &value, NULL) ==
			HX_OS_OK &&
		value == 42);
	CHECK(hx_os_proc_add(state.handler, "self", self_call, &state, cleanup,
		      &state.registration, NULL) == HX_OS_OK);
	CHECK(hx_os_proc_call(state.handler, "self", NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_proc_close_until(
		      &state.handler, HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK &&
		!state.handler);
	CHECK(state.cleaned == 1);
	options.max_depth = 3;
	CHECK(hx_os_proc_create_with_options(
		      NULL, &options, &state.handler, NULL) == HX_OS_OK);
	CHECK(hx_os_proc_register(state.handler, "recurse", recursive_call,
		      &state, NULL) == HX_OS_OK);
	CHECK(hx_os_proc_call(state.handler, "recurse", NULL, NULL) ==
		HX_OS_LIMIT);
	CHECK(state.calls == 3);
	CHECK(hx_os_proc_destroy(&state.handler, NULL) == HX_OS_OK);
}
static void test_inflight(void)
{
	proc_state state = {0};
	hx_os_thread *thread = NULL;
	CHECK(hx_os_proc_create(NULL, &state.handler, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.entered, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.release, NULL) == HX_OS_OK);
	CHECK(hx_os_proc_add(state.handler, "blocked", blocked_call, &state,
		      cleanup, &state.registration, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_create(NULL, call_thread, &state, &thread, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.entered,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(hx_os_proc_unregister_until(state.handler, state.registration,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT);
	CHECK(hx_os_proc_call(state.handler, "blocked", NULL, NULL) ==
		HX_OS_NOT_FOUND);
	CHECK(hx_os_proc_close_until(&state.handler, HX_OS_DEADLINE_NOW,
		      NULL) == HX_OS_TIMEOUT &&
		state.handler && !state.cleaned);
	hx_os_event_set(state.release);
	CHECK(hx_os_thread_join_until(
		      thread, HX_OS_DEADLINE_INFINITE, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	CHECK(hx_os_proc_unregister_until(state.handler, state.registration,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
	CHECK(state.cleaned == 1);
	CHECK(hx_os_proc_destroy(&state.handler, NULL) == HX_OS_OK);
	hx_os_event_destroy(state.entered);
	hx_os_event_destroy(state.release);
}
typedef struct reentry_memory {
	test_memory memory;
	hx_os_proc_handler *handler;
	int armed;
	size_t attempts;
} reentry_memory;
static void reenter(reentry_memory *state)
{
	hx_os_proc_handler *same = state->handler;
	if (state->armed && same) {
		++state->attempts;
		CHECK(hx_os_proc_destroy(&same, NULL) == HX_OS_DEADLOCK);
		CHECK(same == state->handler);
	}
}
static void *HX_OS_CALL reentry_allocate(size_t bytes, void *user)
{
	reentry_memory *state = user;
	reenter(state);
	return test_allocate(bytes, &state->memory);
}
static void HX_OS_CALL reentry_free(void *memory, void *user)
{
	reentry_memory *state = user;
	reenter(state);
	test_deallocate(memory, &state->memory);
}
static void test_allocators(void)
{
	reentry_memory state = {0};
	hx_os_context_options options;
	hx_os_context *context = NULL;
	hx_os_proc_handler *owner = NULL;
	size_t fail, live;
	hx_os_status status;
	hx_os_context_options_init(&options);
	options.allocator.allocate = reentry_allocate;
	options.allocator.deallocate = reentry_free;
	options.allocator.user = &state;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);
	live = state.memory.live;
	for (fail = 1; fail < 20; ++fail) {
		state.memory.fail = state.memory.calls + fail;
		status = hx_os_proc_create(context, &owner, NULL);
		state.memory.fail = 0;
		if (status == HX_OS_OK)
			break;
		CHECK(status == HX_OS_NO_MEMORY && !owner &&
			state.memory.live == live);
	}
	CHECK(fail < 20);
	state.handler = owner;
	state.armed = 1;
	CHECK(hx_os_proc_register(owner, "add", add_one, NULL, NULL) ==
		HX_OS_OK);
	{
		hx_os_calldata data = HX_OS_CALLDATA_INIT;
		size_t allocations = state.memory.calls, n;
		CHECK(hx_os_calldata_set_i64(&data, "value", 0, NULL) ==
			HX_OS_OK);
		for (n = 0; n < 100; ++n)
			CHECK(hx_os_proc_call(owner, "add", &data, NULL) ==
				HX_OS_OK);
		CHECK(state.memory.calls == allocations);
	}
	CHECK(hx_os_proc_destroy(&owner, NULL) == HX_OS_OK && !owner);
	state.armed = 0;
	CHECK(state.attempts > 1);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!state.memory.live);
}
int main(void)
{
	if (!(hx_os_capabilities() & HX_OS_CAP_PROCEDURES)) {
		hx_os_proc_handler *handler = NULL;
		CHECK(hx_os_proc_create(NULL, &handler, NULL) ==
			HX_OS_NOT_SUPPORTED);
		return 0;
	}
	test_calls();
	test_inflight();
	test_allocators();
	return 0;
}
