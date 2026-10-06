#include "utility_test.h"

typedef struct all_state {
	hx_os_signal_handler *handler;
	hx_os_signal_connection late;
	test_memory *memory;
	hx_os_context *copy_context;
	hx_os_params *copy;
	hx_os_event *entered, *release;
	hx_os_atomic calls, cleanups;
	int trace, add_late, pointed;
} all_state;

static void HX_OS_CALL cleanup(void *user)
{
	all_state *state = user;

	(void)hx_os_atomic_fetch_add(&state->cleanups, 1, HX_OS_RELAXED);
}

static hx_os_status HX_OS_CALL late(const char *name,
	const hx_os_signal_arguments *arguments, void *user, hx_os_error *error)
{
	all_state *state = user;

	(void)name;
	(void)arguments;
	(void)error;
	state->trace = state->trace * 10 + 4;

	return HX_OS_OK;
}

static void inspect_arguments(
	const hx_os_signal_arguments *arguments, all_state *state)
{
	size_t calls = state->memory->calls, index;
	int64_t integer = 90;
	int boolean;
	double real = -1;
	void *pointer;
	hx_string_view text, name;
	hx_os_param_value value;

	CHECK(hx_os_signal_arguments_count(arguments) == 5);
	CHECK(hx_os_signal_arguments_get_i64(
		      arguments, "number", &integer, NULL) == HX_OS_OK &&
		integer == 42);
	CHECK(hx_os_signal_arguments_get_bool(
		      arguments, "ready", &boolean, NULL) == HX_OS_OK &&
		boolean == 1);
	CHECK(hx_os_signal_arguments_get_double(
		      arguments, "ratio", &real, NULL) == HX_OS_OK &&
		real == 2.5);
	CHECK(hx_os_signal_arguments_get_pointer(
		      arguments, "pointer", &pointer, NULL) == HX_OS_OK &&
		pointer == &state->pointed);
	CHECK(hx_os_signal_arguments_get_string(
		      arguments, "文字", &text, NULL) == HX_OS_OK);
	CHECK(text.size == 7 && !memcmp(text.data, "中\0文", 7));
	CHECK(hx_os_signal_arguments_get_i64(arguments, "missing", &integer,
		      NULL) == HX_OS_NOT_FOUND &&
		integer == 42);
	CHECK(hx_os_signal_arguments_get_double(arguments, "number", &real,
		      NULL) == HX_OS_TYPE_MISMATCH &&
		real == 2.5);

	for (index = 0; index < 5; ++index) {
		hx_os_param_value by_name;

		CHECK(hx_os_signal_arguments_at(arguments, index, &name, &value,
			      NULL) == HX_OS_OK);
		CHECK(name.size > 0 && name.data[name.size] == 0);
		CHECK(hx_os_signal_arguments_get(arguments, name.data, &by_name,
			      NULL) == HX_OS_OK &&
			value.kind == by_name.kind);
	}
	name.data = "keep";
	name.size = 4;
	value.kind = 42;
	CHECK(hx_os_signal_arguments_at(
		      arguments, SIZE_MAX, &name, &value, NULL) == HX_OS_RANGE);
	CHECK(name.size == 4 && !strcmp(name.data, "keep") && value.kind == 42);
	CHECK(state->memory->calls == calls);
}

static hx_os_status HX_OS_CALL first(const char *name,
	const hx_os_signal_arguments *arguments, void *user, hx_os_error *error)
{
	all_state *state = user;

	CHECK(error && error->status == HX_OS_OK);
	if (!strcmp(name, "ready"))
		inspect_arguments(arguments, state);
	else
		CHECK(!hx_os_signal_arguments_count(arguments));
	state->trace = state->trace * 10 + 1;
	if (state->add_late) {
		state->add_late = 0;
		CHECK(hx_os_signal_connect_all(state->handler, late, state,
			      cleanup, &state->late, NULL) == HX_OS_OK);
	}

	error->status = HX_OS_IO_ERROR;
	error->domain = HX_OS_ERROR_ERRNO;
	error->native_code = 5;
	error->cause_code = 7;

	return HX_OS_IO_ERROR;
}

static hx_os_status HX_OS_CALL named(
	const hx_os_calldata *data, void *user, hx_os_error *error)
{
	all_state *state = user;

	(void)data;
	(void)error;
	state->trace = state->trace * 10 + 2;

	return HX_OS_OK;
}

static hx_os_status HX_OS_CALL third(const char *name,
	const hx_os_signal_arguments *arguments, void *user, hx_os_error *error)
{
	all_state *state = user;

	(void)name;
	(void)arguments;
	(void)error;
	state->trace = state->trace * 10 + 3;

	return HX_OS_ACCESS_DENIED;
}

static void test_order_and_bounds(void)
{
	test_memory memory = {0};
	hx_os_context *context = test_context(&memory);
	hx_os_signal_options options = HX_OS_SIGNAL_OPTIONS_INIT;
	hx_os_signal_connection one, two, three, rejected = 99;
	hx_os_calldata data = HX_OS_CALLDATA_INIT;
	all_state state = {0};
	hx_os_error error;
	size_t called, before;

	options.max_connections = 4;
	state.memory = &memory;
	state.add_late = 1;
	CHECK(hx_os_signal_handler_create_with_options(
		      context, &options, &state.handler, NULL) == HX_OS_OK);
	CHECK(hx_os_calldata_set_i64(&data, "number", 42, NULL) == HX_OS_OK);
	CHECK(hx_os_calldata_set_bool(&data, "ready", 1, NULL) == HX_OS_OK);
	CHECK(hx_os_calldata_set_double(&data, "ratio", 2.5, NULL) == HX_OS_OK);
	CHECK(hx_os_calldata_set_pointer(
		      &data, "pointer", &state.pointed, NULL) == HX_OS_OK);
	CHECK(hx_os_calldata_set_string(&data, "文字",
		      (hx_string_view){"中\0文", 7}, NULL) == HX_OS_OK);
	before = memory.calls;
	CHECK(hx_os_signal_connect_all(state.handler, first, &state, cleanup,
		      &one, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect(state.handler, "ready", named, &state,
		      cleanup, &two, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect_all(state.handler, third, &state, cleanup,
		      &three, NULL) == HX_OS_OK);
	CHECK(memory.calls == before);
	CHECK(hx_os_signal_emit(state.handler, "ready", &data, &called,
		      &error) == HX_OS_IO_ERROR);
	CHECK(state.trace == 123 && called == 3);
	CHECK(memory.calls == before + 1);
	CHECK(error.domain == HX_OS_ERROR_ERRNO && error.native_code == 5 &&
		error.cause_code == 7);
	CHECK(hx_os_signal_connect_all(state.handler, late, &state, cleanup,
		      &rejected, NULL) == HX_OS_LIMIT &&
		!rejected);
	CHECK(!hx_os_atomic_load(&state.cleanups, HX_OS_RELAXED));

	state.trace = 0;
	CHECK(hx_os_signal_emit(state.handler, "future😀", NULL, &called,
		      NULL) == HX_OS_IO_ERROR);
	CHECK(called == 3 && state.trace == 134);
	CHECK(hx_os_signal_disconnect_until(state.handler, two,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect(state.handler, "*", named, &state, cleanup,
		      &two, NULL) == HX_OS_OK);
	state.trace = 0;
	CHECK(hx_os_signal_emit(state.handler, "future", NULL, &called, NULL) ==
			HX_OS_IO_ERROR &&
		called == 3 && state.trace == 134);
	state.trace = 0;
	CHECK(hx_os_signal_emit(state.handler, "*", NULL, &called, NULL) ==
			HX_OS_IO_ERROR &&
		called == 4 && state.trace == 1342);
	CHECK(hx_os_signal_connect(state.handler, NULL, named, &state, NULL,
		      NULL, NULL) == HX_OS_INVALID);
	CHECK(hx_os_signal_connect_all(state.handler, NULL, &state, NULL, NULL,
		      NULL) == HX_OS_INVALID);
	CHECK(hx_os_signal_emit(state.handler, "", NULL, &called, NULL) ==
			HX_OS_INVALID &&
		!called);
	CHECK(hx_os_signal_handler_close_until(
		      &state.handler, HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_RELAXED) == 5);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(!memory.live);
}

static hx_os_status HX_OS_CALL copy_arguments(const char *name,
	const hx_os_signal_arguments *arguments, void *user, hx_os_error *error)
{
	all_state *state = user;

	(void)name;
	return hx_os_signal_arguments_copy(
		arguments, state->copy_context, &state->copy, error);
}

static void test_copy(void)
{
	test_memory memory = {0};
	hx_os_context *context = test_context(&memory);
	hx_os_calldata data = HX_OS_CALLDATA_INIT;
	all_state state = {0};
	hx_string_view text;
	hx_os_status status;
	size_t step;
	void *pointer;

	state.copy_context = context;
	CHECK(hx_os_signal_handler_create(NULL, &state.handler, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_signal_connect_all(state.handler, copy_arguments, &state,
		      NULL, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_calldata_set_string(&data, "text",
		      (hx_string_view){"中\0文", 7}, NULL) == HX_OS_OK);
	CHECK(hx_os_calldata_set_pointer(
		      &data, "pointer", &state.pointed, NULL) == HX_OS_OK);
	if (hx_os_params_available()) {
		for (step = 1; step < 32; ++step) {
			memory.fail = memory.calls + step;
			status = hx_os_signal_emit(
				state.handler, "copy", &data, NULL, NULL);
			memory.fail = 0;
			CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
			if (status == HX_OS_OK)
				break;
			CHECK(!state.copy &&
				hx_os_context_allocations(context) == 0);
		}
		CHECK(step < 32 && state.copy);
		CHECK(hx_os_calldata_set_string(&data, "text",
			      (hx_string_view){"changed", 7},
			      NULL) == HX_OS_OK);
		CHECK(hx_os_signal_handler_destroy(&state.handler, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_params_get_string(
			      state.copy, "text", &text, NULL) == HX_OS_OK &&
			text.size == 7 && !memcmp(text.data, "中\0文", 7));
		CHECK(hx_os_params_get_pointer(state.copy, "pointer", &pointer,
			      NULL) == HX_OS_OK &&
			pointer == &state.pointed);
		CHECK(hx_os_params_destroy(&state.copy, NULL) == HX_OS_OK);
	}
	else {
		CHECK(hx_os_signal_emit(state.handler, "copy", &data, NULL,
			      NULL) == HX_OS_NOT_SUPPORTED &&
			!state.copy);
		CHECK(hx_os_signal_handler_destroy(&state.handler, NULL) ==
			HX_OS_OK);
	}
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(!memory.live);
}

static hx_os_status HX_OS_CALL blocking(const char *name,
	const hx_os_signal_arguments *arguments, void *user, hx_os_error *error)
{
	all_state *state = user;

	(void)error;
	CHECK(!strcmp(name, "parallel"));
	CHECK(!hx_os_signal_arguments_count(arguments));
	if (hx_os_atomic_fetch_add(&state->calls, 1, HX_OS_ACQ_REL) == 3)
		hx_os_event_set(state->entered);
	CHECK(hx_os_event_wait_until(state->release,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(hx_os_signal_disconnect_until(state->handler, state->late,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_DEADLOCK);

	return HX_OS_OK;
}

static void *HX_OS_CALL emit_worker(void *user)
{
	all_state *state = user;
	size_t called;

	CHECK(hx_os_signal_emit(state->handler, "parallel", NULL, &called,
		      NULL) == HX_OS_OK &&
		called == 1);

	return NULL;
}

static void test_concurrent_close(void)
{
	all_state state = {0};
	hx_os_signal_connection connection;
	hx_os_thread *threads[4];
	size_t index;

	CHECK(hx_os_signal_handler_create(NULL, &state.handler, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.entered, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.release, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect_all(state.handler, blocking, &state, cleanup,
		      &connection, NULL) == HX_OS_OK);
	state.late = connection;
	for (index = 0; index < 4; ++index)
		CHECK(hx_os_thread_create(NULL, emit_worker, &state,
			      threads + index, NULL) == HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.entered,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(hx_os_signal_disconnect_until(state.handler, connection,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT);
	CHECK(hx_os_signal_handler_close_until(&state.handler,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT &&
		state.handler);
	CHECK(!hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE));
	hx_os_event_set(state.release);
	for (index = 0; index < 4; ++index) {
		CHECK(hx_os_thread_join_until(threads[index],
			      hx_os_deadline_after_ms(5000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(threads + index, NULL) == HX_OS_OK);
	}
	CHECK(hx_os_signal_handler_close_until(
		      &state.handler, HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 1);
	hx_os_event_destroy(state.entered);
	hx_os_event_destroy(state.release);
}

int main(void)
{
	if (!(hx_os_capabilities() & HX_OS_CAP_SIGNALS)) {
		hx_os_signal_connection id = 99;
		hx_os_param_value value;
		hx_os_params *copy = NULL;

		CHECK(hx_os_signal_connect_all(NULL, first, NULL, NULL, &id,
			      NULL) == HX_OS_NOT_SUPPORTED &&
			!id);
		CHECK(hx_os_signal_arguments_get(NULL, "x", &value, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_signal_arguments_copy(NULL, NULL, &copy, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!copy);

		return 0;
	}

	test_order_and_bounds();
	test_copy();
	test_concurrent_close();

	return 0;
}
