#include "utility_test.h"

typedef struct state {
	hx_os_signal_handler *signals;
	hx_os_proc_handler *procedures;
	hx_os_params *saved;
	hx_os_event *entered, *release;
	hx_os_atomic calls, cleanups;
	int trace, legacy_calls, fail, external;
} state;

static hx_os_status HX_OS_CALL lower_volume(
	const char *name, hx_os_params *params, void *user, hx_os_error *error)
{
	state *value = user;
	hx_os_params *borrowed = params;
	int64_t volume;

	CHECK(!strcmp(name, "volume"));
	CHECK(hx_os_params_get_i64(params, "volume", &volume, NULL) ==
		HX_OS_OK);
	CHECK(volume == 80);
	CHECK(hx_os_params_destroy(&borrowed, NULL) == HX_OS_BUSY &&
		borrowed == params);
	CHECK(hx_os_params_set_i64(params, "volume", 60, NULL) == HX_OS_OK);
	CHECK(hx_os_params_remove(params, "signal_name", NULL) == HX_OS_OK);
	value->trace = value->trace * 10 + 1;
	*error = (hx_os_error){HX_OS_IO_ERROR, HX_OS_ERROR_ERRNO, 5, 42};

	return HX_OS_IO_ERROR;
}

static hx_os_status HX_OS_CALL observe_volume(const char *name,
	const hx_os_signal_arguments *arguments, void *user, hx_os_error *error)
{
	state *value = user;
	int64_t volume;

	CHECK(!strcmp(name, "volume"));
	CHECK(hx_os_signal_arguments_get_i64(
		      arguments, "volume", &volume, NULL) == HX_OS_OK &&
		volume == 60);
	value->trace = value->trace * 10 + 2;

	return hx_os_signal_arguments_copy(
		arguments, NULL, &value->saved, error);
}

static hx_os_status HX_OS_CALL add_binary(
	const char *name, hx_os_params *params, void *user, hx_os_error *error)
{
	static const unsigned char payload[] = {0, 0xff, 0x80, 42};
	state *value = user;

	(void)error;
	CHECK(!strcmp(name, "volume"));
	CHECK(hx_os_params_set_bytes(params, "payload", payload,
		      sizeof(payload), NULL) == HX_OS_OK);
	CHECK(hx_os_params_set_u64(params, "sequence", UINT64_MAX, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_params_set_i64(params, "volume", 50, NULL) == HX_OS_OK);
	value->trace = value->trace * 10 + 3;

	return HX_OS_ACCESS_DENIED;
}

static hx_os_status HX_OS_CALL observe_binary(const char *name,
	const hx_os_signal_arguments *arguments, void *user, hx_os_error *error)
{
	state *value = user;
	hx_os_bytes_view bytes;
	uint64_t sequence;
	int64_t volume;

	(void)name;
	(void)error;
	CHECK(hx_os_signal_arguments_get_i64(
		      arguments, "volume", &volume, NULL) == HX_OS_OK &&
		volume == 50);
	CHECK(hx_os_signal_arguments_get_u64(
		      arguments, "sequence", &sequence, NULL) == HX_OS_OK &&
		sequence == UINT64_MAX);
	CHECK(hx_os_signal_arguments_get_bytes(
		      arguments, "payload", &bytes, NULL) == HX_OS_OK &&
		bytes.size == 4);
	CHECK(!memcmp(bytes.data, "\0\xff\x80*", 4));
	value->trace = value->trace * 10 + 4;

	return HX_OS_OK;
}

static hx_os_status HX_OS_CALL legacy(
	const hx_os_calldata *data, void *user, hx_os_error *error)
{
	state *value = user;
	int64_t volume;

	++value->legacy_calls;
	return hx_os_calldata_get_i64(data, "volume", &volume, error);
}

static void test_signal_values(void)
{
	test_memory memory = {0};
	hx_os_context *context = test_context(&memory);
	hx_os_params *params = NULL;
	hx_string_view name;
	hx_os_error error;
	state value = {0};
	size_t called;
	int64_t volume;

	CHECK(hx_os_signal_handler_create(context, &value.signals, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_params_create(NULL, &params, NULL) == HX_OS_OK);
	CHECK(hx_os_params_set_i64(params, "volume", 80, NULL) == HX_OS_OK);
	CHECK(hx_os_params_set_cstr(params, "signal_name", "volume", NULL) ==
		HX_OS_OK);
	CHECK(hx_os_signal_connect_mutable(value.signals, "volume",
		      lower_volume, &value, NULL, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect_all(value.signals, observe_volume, &value,
		      NULL, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect_all_mutable(value.signals, add_binary,
		      &value, NULL, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect_observer(value.signals, "volume",
		      observe_binary, &value, NULL, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect(value.signals, "volume", legacy, &value,
		      NULL, NULL, NULL) == HX_OS_OK);

	memory.fail = memory.calls + 1;
	CHECK(hx_os_signal_emit_params(value.signals, "volume", params, &called,
		      NULL) == HX_OS_NO_MEMORY &&
		called == 0);
	memory.fail = 0;
	CHECK(value.trace == 0);
	CHECK(hx_os_params_get_i64(params, "volume", &volume, NULL) ==
			HX_OS_OK &&
		volume == 80);
	CHECK(hx_os_params_get_string(params, "signal_name", &name, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_signal_emit_params(value.signals, name.data, params,
		      &called, &error) == HX_OS_IO_ERROR);
	CHECK(called == 4 && value.trace == 1234 && !value.legacy_calls);
	CHECK(error.domain == HX_OS_ERROR_ERRNO && error.native_code == 5 &&
		error.cause_code == 42);
	CHECK(hx_os_params_get_i64(params, "volume", &volume, NULL) ==
			HX_OS_OK &&
		volume == 50);
	CHECK(hx_os_params_get_i64(value.saved, "volume", &volume, NULL) ==
			HX_OS_OK &&
		volume == 60);
	CHECK(hx_os_params_destroy(&value.saved, NULL) == HX_OS_OK);
	CHECK(hx_os_params_destroy(&params, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_handler_destroy(&value.signals, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!memory.live);
}

static hx_os_status HX_OS_CALL increment(
	const char *name, hx_os_params *params, void *user, hx_os_error *error)
{
	state *value = user;
	int64_t volume;

	(void)name;
	++value->external;
	CHECK(hx_os_params_get_i64(params, "volume", &volume, NULL) ==
		HX_OS_OK);

	return hx_os_params_set_i64(params, "volume", volume + 1, error);
}

static void test_projection_and_readonly(void)
{
	state value = {0};
	hx_os_params *params = NULL;
	hx_os_calldata fixed = HX_OS_CALLDATA_INIT;
	size_t called, i;
	char name[16];

	CHECK(hx_os_signal_handler_create(NULL, &value.signals, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_params_create(NULL, &params, NULL) == HX_OS_OK);
	CHECK(hx_os_params_set_i64(params, "volume", 80, NULL) == HX_OS_OK);
	CHECK(hx_os_calldata_set_i64(&fixed, "volume", 80, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect_mutable(value.signals, "volume", increment,
		      &value, NULL, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect(value.signals, "volume", legacy, &value,
		      NULL, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_emit(value.signals, "volume", &fixed, &called,
		      NULL) == HX_OS_TYPE_MISMATCH);
	CHECK(called == 1 && !value.external && value.legacy_calls == 1);
	CHECK(hx_os_signal_emit_params(value.signals, "volume", params, &called,
		      NULL) == HX_OS_OK &&
		called == 2);
	CHECK(value.external == 1 && value.legacy_calls == 2);

	for (i = 0; i < 20; ++i) {
		(void)snprintf(name, sizeof(name), "item%zu", i);
		CHECK(hx_os_params_set_i64(params, name, (int64_t)i, NULL) ==
			HX_OS_OK);
	}
	CHECK(hx_os_signal_emit_params(value.signals, "volume", params, &called,
		      NULL) == HX_OS_LIMIT &&
		called == 1);
	CHECK(value.external == 2 && value.legacy_calls == 2);
	CHECK(hx_os_params_clear(params, NULL) == HX_OS_OK);
	CHECK(hx_os_params_set_i64(params, "volume", 80, NULL) == HX_OS_OK);
	CHECK(hx_os_params_set_bytes(params, "bytes", "a", 1, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_signal_emit_params(value.signals, "volume", params, &called,
		      NULL) == HX_OS_UNREPRESENTABLE &&
		called == 1);
	CHECK(value.external == 3 && value.legacy_calls == 2);
	CHECK(hx_os_params_destroy(&params, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_handler_destroy(&value.signals, NULL) == HX_OS_OK);
}

static hx_os_status HX_OS_CALL recursive(
	const char *name, hx_os_params *params, void *user, hx_os_error *error)
{
	state *value = user;
	hx_os_status status;

	++value->external;
	status = hx_os_params_set_i64(params, "depth", value->external, error);
	if (status != HX_OS_OK)
		return status;

	return hx_os_signal_emit_params(
		value->signals, name, params, NULL, error);
}

static void test_nested_borrow(void)
{
	state value = {0};
	hx_os_signal_options options = HX_OS_SIGNAL_OPTIONS_INIT;
	hx_os_params *params = NULL;
	int64_t depth;

	options.max_depth = 2;
	CHECK(hx_os_signal_handler_create_with_options(
		      NULL, &options, &value.signals, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect_all_mutable(value.signals, recursive, &value,
		      NULL, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_params_create(NULL, &params, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_emit_params(value.signals, "nested", params, NULL,
		      NULL) == HX_OS_LIMIT &&
		value.external == 2);
	CHECK(hx_os_params_get_i64(params, "depth", &depth, NULL) == HX_OS_OK &&
		depth == 2);
	CHECK(hx_os_params_destroy(&params, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_handler_destroy(&value.signals, NULL) == HX_OS_OK);
}

static hx_os_status HX_OS_CALL blocking(
	const char *name, hx_os_params *params, void *user, hx_os_error *error)
{
	state *value = user;

	(void)name;
	(void)error;
	if (hx_os_atomic_fetch_add(&value->calls, 1, HX_OS_ACQ_REL) == 3)
		hx_os_event_set(value->entered);
	CHECK(hx_os_event_wait_until(value->release,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);

	return hx_os_params_set_i64(params, "complete", 1, NULL);
}

static void HX_OS_CALL cleanup(void *user)
{
	state *value = user;

	(void)hx_os_atomic_fetch_add(&value->cleanups, 1, HX_OS_RELAXED);
}

static void *HX_OS_CALL worker(void *user)
{
	state *value = user;
	hx_os_params *params = NULL;
	int64_t complete;

	CHECK(hx_os_params_create(NULL, &params, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_emit_params(value->signals, "parallel", params, NULL,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_params_get_i64(params, "complete", &complete, NULL) ==
			HX_OS_OK &&
		complete == 1);
	CHECK(hx_os_params_destroy(&params, NULL) == HX_OS_OK);

	return NULL;
}

static void test_concurrent_close(void)
{
	state value = {0};
	hx_os_thread *threads[4];
	size_t i;

	CHECK(hx_os_signal_handler_create(NULL, &value.signals, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &value.entered, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &value.release, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect_all_mutable(value.signals, blocking, &value,
		      cleanup, NULL, NULL) == HX_OS_OK);
	for (i = 0; i < 4; ++i)
		CHECK(hx_os_thread_create(NULL, worker, &value, threads + i,
			      NULL) == HX_OS_OK);
	CHECK(hx_os_event_wait_until(value.entered,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(hx_os_signal_handler_close_until(&value.signals,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT &&
		value.signals);
	CHECK(!hx_os_atomic_load(&value.cleanups, HX_OS_ACQUIRE));
	hx_os_event_set(value.release);
	for (i = 0; i < 4; ++i) {
		CHECK(hx_os_thread_join_until(threads[i],
			      hx_os_deadline_after_ms(5000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(threads + i, NULL) == HX_OS_OK);
	}
	CHECK(hx_os_signal_handler_close_until(
		      &value.signals, HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
	CHECK(hx_os_atomic_load(&value.cleanups, HX_OS_ACQUIRE) == 1);
	hx_os_event_destroy(value.entered);
	hx_os_event_destroy(value.release);
}

static hx_os_status HX_OS_CALL procedure(
	hx_os_params *params, void *user, hx_os_error *error)
{
	state *value = user;
	hx_os_params *borrowed = params;
	hx_os_status status;

	CHECK(hx_os_params_destroy(&borrowed, NULL) == HX_OS_BUSY && borrowed);
	++value->external;
	status = hx_os_params_set_bytes(params, "result", "\0\xff", 2, error);
	if (status == HX_OS_OK && value->fail) {
		*error = (hx_os_error){HX_OS_IO_ERROR, HX_OS_ERROR_ERRNO, 5, 7};
		status = HX_OS_IO_ERROR;
	}

	return status;
}

static hx_os_status HX_OS_CALL fixed_procedure(
	hx_os_calldata *data, void *user, hx_os_error *error)
{
	(void)data;
	(void)user;
	(void)error;

	return HX_OS_OK;
}

static void test_procedure_values(void)
{
	state value = {0};
	test_memory memory = {0};
	hx_os_context *context = test_context(&memory);
	hx_os_params *input = NULL, *result = NULL;
	hx_os_bytes_view bytes;
	hx_os_error error;
	hx_os_status status = HX_OS_OK;
	size_t step, live;

	CHECK(hx_os_proc_create(context, &value.procedures, NULL) == HX_OS_OK);
	CHECK(hx_os_proc_add_params(value.procedures, "transform", procedure,
		      &value, NULL, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_proc_register(value.procedures, "transform",
		      fixed_procedure, NULL, NULL) == HX_OS_EXISTS);
	CHECK(hx_os_proc_register(value.procedures, "fixed", fixed_procedure,
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_params_create(NULL, &input, NULL) == HX_OS_OK);
	CHECK(hx_os_params_set_cstr(input, "name", "设备😀", NULL) == HX_OS_OK);
	CHECK(hx_os_proc_call(value.procedures, "transform", NULL, NULL) ==
			HX_OS_TYPE_MISMATCH &&
		!value.external);
	CHECK(hx_os_proc_call_params(value.procedures, "fixed", input, &result,
		      NULL) == HX_OS_TYPE_MISMATCH &&
		!result);
	CHECK(hx_os_proc_call_params(value.procedures, "missing", NULL, &result,
		      NULL) == HX_OS_NOT_FOUND &&
		!result);

	live = memory.live;
	for (step = 1; step < 32; ++step) {
		memory.fail = memory.calls + step;
		status = hx_os_proc_call_params(
			value.procedures, "transform", input, &result, &error);
		memory.fail = 0;
		if (status == HX_OS_OK)
			break;
		CHECK(status == HX_OS_NO_MEMORY && !result);
		CHECK(memory.live == live);
	}
	CHECK(step < 32 && result);
	CHECK(hx_os_params_get_bytes(result, "result", &bytes, NULL) ==
			HX_OS_OK &&
		bytes.size == 2 && !memcmp(bytes.data, "\0\xff", 2));
	CHECK(hx_os_params_get_bytes(input, "result", &bytes, NULL) ==
		HX_OS_NOT_FOUND);
	CHECK(hx_os_params_destroy(&result, NULL) == HX_OS_OK);
	CHECK(memory.live == live);
	value.fail = 1;
	CHECK(hx_os_proc_call_params(value.procedures, "transform", input,
		      &result, &error) == HX_OS_IO_ERROR &&
		!result);
	CHECK(error.domain == HX_OS_ERROR_ERRNO && error.native_code == 5 &&
		error.cause_code == 7 && memory.live == live);
	CHECK(hx_os_params_get_bytes(input, "result", &bytes, NULL) ==
		HX_OS_NOT_FOUND);
	CHECK(hx_os_params_destroy(&input, NULL) == HX_OS_OK);
	CHECK(hx_os_proc_destroy(&value.procedures, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!memory.live);
}

int main(void)
{
	int params = hx_os_params_available();
	uint64_t caps = hx_os_capabilities();

	if (params && (caps & HX_OS_CAP_SIGNALS)) {
		test_signal_values();
		test_projection_and_readonly();
		test_nested_borrow();
		test_concurrent_close();
	}
	else {
		size_t called = 9;
		hx_os_signal_connection id = 9;

		CHECK(hx_os_signal_connect_mutable(NULL, "x", lower_volume,
			      NULL, NULL, &id, NULL) == HX_OS_NOT_SUPPORTED &&
			!id);
		CHECK(hx_os_signal_emit_params(NULL, "x", NULL, &called,
			      NULL) == HX_OS_NOT_SUPPORTED &&
			!called);
	}
	if (params && (caps & HX_OS_CAP_PROCEDURES))
		test_procedure_values();
	else {
		hx_os_params *out = NULL;
		hx_os_proc_registration id = 9;

		CHECK(hx_os_proc_add_params(NULL, "x", procedure, NULL, NULL,
			      &id, NULL) == HX_OS_NOT_SUPPORTED &&
			!id);
		CHECK(hx_os_proc_call_params(NULL, "x", NULL, &out, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!out);
	}

	return 0;
}
