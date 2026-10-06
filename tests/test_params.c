#include "utility_test.h"

static void test_values(void)
{
	test_memory memory = {0};
	hx_os_context *context = test_context(&memory);
	hx_os_params *params = NULL, *copy = NULL;
	hx_os_param_value value;
	hx_string_view name, text;
	hx_os_bytes_view bytes;
	unsigned char input[] = {0, 0xff, 0x80, 1, 2};
	size_t i, calls;
	uint64_t unsigned_value;
	int64_t integer;
	int boolean, pointed = 10;
	void *pointer;
	double real;
	char key[32], previous[32] = "";

	CHECK(hx_os_params_create(context, &params, NULL) == HX_OS_OK);
	for (i = 0; i < 150; ++i) {
		size_t id = i * 71 % 150;

		(void)snprintf(key, sizeof(key), "key_%03zu", id);
		CHECK(hx_os_params_set_i64(params, key, (int64_t)id, NULL) ==
			HX_OS_OK);
	}
	CHECK(hx_os_params_count(params) == 150);
	for (i = 0; i < 150; ++i) {
		CHECK(hx_os_params_at(params, i, &name, &value, NULL) ==
			HX_OS_OK);
		CHECK(strcmp(previous, name.data) < 0);
		CHECK(value.kind == HX_OS_PARAM_I64 &&
			value.data.integer == (int64_t)i);
		memcpy(previous, name.data, name.size + 1);
	}
	CHECK(hx_os_params_set_bool(params, "ready", 1, NULL) == HX_OS_OK);
	CHECK(hx_os_params_set_bool(params, "ready", 2, NULL) == HX_OS_INVALID);
	CHECK(hx_os_params_get_bool(params, "ready", &boolean, NULL) ==
			HX_OS_OK &&
		boolean);
	CHECK(hx_os_params_set_u64(params, "sequence", UINT64_MAX, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_params_get_u64(params, "sequence", &unsigned_value, NULL) ==
		HX_OS_OK);
	CHECK(unsigned_value == UINT64_MAX);
	CHECK(hx_os_params_set_double(params, "temperature", 21.5, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_params_get_double(params, "temperature", &real, NULL) ==
			HX_OS_OK &&
		real == 21.5);
	CHECK(hx_os_params_set_pointer(params, "user", &pointed, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_params_get_pointer(params, "user", &pointer, NULL) ==
			HX_OS_OK &&
		pointer == &pointed);
	CHECK(hx_os_params_set_cstr(params, "label", "设备😀", NULL) ==
		HX_OS_OK);
	CHECK(hx_os_params_set_string(params, "embedded",
		      (hx_string_view){"x\0y", 3}, NULL) == HX_OS_OK);
	CHECK(hx_os_params_get_string(params, "embedded", &text, NULL) ==
		HX_OS_OK);
	CHECK(text.size == 3 && !memcmp(text.data, "x\0y\0", 4));
	CHECK(hx_os_params_set_bytes(params, "binary", input, sizeof(input),
		      NULL) == HX_OS_OK);
	memset(input, 42, sizeof(input));
	CHECK(hx_os_params_get_bytes(params, "binary", &bytes, NULL) ==
		HX_OS_OK);
	CHECK(bytes.size == 5 &&
		((const unsigned char *)bytes.data)[1] == 0xff);
	CHECK(hx_os_params_set_bytes(params, "binary",
		      (const unsigned char *)bytes.data + 1, 4,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_params_get_bytes(params, "binary", &bytes, NULL) ==
		HX_OS_OK);
	CHECK(bytes.size == 4 &&
		((const unsigned char *)bytes.data)[0] == 0xff);
	CHECK(hx_os_params_set_string(params, "binary",
		      (hx_string_view){bytes.data, bytes.size},
		      NULL) == HX_OS_INVALID_ENCODING);
	CHECK(hx_os_params_set_bytes(params, "empty", NULL, 0, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_params_set_bytes(params, "empty", NULL, 1, NULL) ==
		HX_OS_INVALID);
	CHECK(hx_os_params_set_bytes(params, "empty", input, SIZE_MAX, NULL) ==
		HX_OS_LIMIT);

	calls = memory.calls;
	for (i = 0; i < 1000; ++i) {
		CHECK(hx_os_params_set_i64(
			      params, "key_000", (int64_t)i, NULL) == HX_OS_OK);
		CHECK(hx_os_params_set_cstr(params, "label", "short", NULL) ==
			HX_OS_OK);
		CHECK(hx_os_params_get_i64(params, "key_000", &integer, NULL) ==
				HX_OS_OK &&
			integer == (int64_t)i);
	}
	CHECK(memory.calls == calls);
	integer = 777;
	CHECK(hx_os_params_get_i64(params, "sequence", &integer, NULL) ==
			HX_OS_TYPE_MISMATCH &&
		integer == 777);
	CHECK(hx_os_params_get_i64(params, "missing", &integer, NULL) ==
			HX_OS_NOT_FOUND &&
		integer == 777);
	CHECK(hx_os_params_remove(params, "key_075", NULL) == HX_OS_OK);
	CHECK(hx_os_params_remove(params, "key_075", NULL) == HX_OS_NOT_FOUND);
	CHECK(hx_os_params_get_i64(params, "key_076", &integer, NULL) ==
			HX_OS_OK &&
		integer == 76);
	CHECK(hx_os_params_clone(params, NULL, &copy, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);
	CHECK(hx_os_params_destroy(&params, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!memory.live);
	CHECK(hx_os_params_get_bytes(copy, "binary", &bytes, NULL) == HX_OS_OK);
	CHECK(bytes.size == 4 &&
		((const unsigned char *)bytes.data)[0] == 0xff);
	CHECK(hx_os_params_get_pointer(copy, "user", &pointer, NULL) ==
			HX_OS_OK &&
		pointer == &pointed);
	CHECK(hx_os_params_destroy(&copy, NULL) == HX_OS_OK);
}

static void test_limits(void)
{
	hx_os_params *params = NULL;
	hx_os_params_options options = HX_OS_PARAMS_OPTIONS_INIT;
	hx_string_view name = {"unchanged", 9};
	hx_os_param_value value;
	char long_name[130];

	options.max_entries = 2;
	options.max_bytes = 10;
	CHECK(hx_os_params_create_with_options(NULL, &options, &params, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_params_set_cstr(params, "a", "12345", NULL) == HX_OS_OK);
	CHECK(hx_os_params_storage_bytes(params) == 8);
	CHECK(hx_os_params_set_cstr(params, "b", "x", NULL) == HX_OS_LIMIT);
	CHECK(hx_os_params_set_cstr(params, "a", "", NULL) == HX_OS_OK);
	CHECK(hx_os_params_storage_bytes(params) == 8);
	CHECK(hx_os_params_set_cstr(params, "b", "x", NULL) == HX_OS_LIMIT);
	CHECK(hx_os_params_remove(params, "a", NULL) == HX_OS_OK);
	CHECK(hx_os_params_set_cstr(params, "b", "x", NULL) == HX_OS_OK);
	CHECK(hx_os_params_set_cstr(params, "a", "123", NULL) == HX_OS_OK);
	CHECK(hx_os_params_storage_bytes(params) == 10);
	CHECK(hx_os_params_set_i64(params, "c", 0, NULL) == HX_OS_LIMIT);
	CHECK(hx_os_params_at(params, 2, &name, &value, NULL) == HX_OS_RANGE);
	CHECK(!strcmp(name.data, "unchanged"));
	CHECK(hx_os_params_set_i64(params, "", 0, NULL) == HX_OS_INVALID);
	CHECK(hx_os_params_set_i64(params, "\xff", 0, NULL) ==
		HX_OS_INVALID_ENCODING);
	memset(long_name, 'a', sizeof(long_name));
	long_name[129] = 0;
	CHECK(hx_os_params_set_i64(params, long_name, 0, NULL) == HX_OS_LIMIT);
	CHECK(hx_os_params_clear(params, NULL) == HX_OS_OK);
	CHECK(!hx_os_params_count(params) &&
		!hx_os_params_storage_bytes(params));
	CHECK(hx_os_params_set_bool(params, "new", 0, NULL) == HX_OS_OK);
	CHECK(hx_os_params_clear(params, NULL) == HX_OS_OK);
	{
		hx_os_params_edit edit = hx_os_params_edit_begin(params);
		int64_t integer;

		hx_os_params_edit_set_i64(&edit, "one", 1);
		hx_os_params_edit_set_i64(&edit, "two", 2);
		hx_os_params_edit_set_i64(&edit, "third", 3);
		hx_os_params_edit_set_i64(&edit, "one", 99);
		CHECK(hx_os_params_edit_finish(&edit, NULL) == HX_OS_LIMIT);
		CHECK(hx_os_params_get_i64(params, "one", &integer, NULL) ==
				HX_OS_OK &&
			integer == 1);
		CHECK(hx_os_params_count(params) == 2);
	}
	CHECK(hx_os_params_destroy(&params, NULL) == HX_OS_OK);
}

typedef struct reentry_state {
	test_memory memory;
	hx_os_params *params;
	int active;
} reentry_state;

static void test_reentry(reentry_state *state)
{
	hx_os_param_value value;
	hx_os_params *owner = state->params;

	if (!state->active || !owner)
		return;
	CHECK(hx_os_params_set_i64(owner, "nested", 1, NULL) == HX_OS_DEADLOCK);
	CHECK(hx_os_params_get(owner, "existing", &value, NULL) ==
		HX_OS_DEADLOCK);
	CHECK(hx_os_params_clear(owner, NULL) == HX_OS_DEADLOCK);
	CHECK(hx_os_params_destroy(&owner, NULL) == HX_OS_DEADLOCK);
	CHECK(owner == state->params);
}

static void *HX_OS_CALL allocate(size_t bytes, void *user)
{
	reentry_state *state = user;

	test_reentry(state);

	return test_allocate(bytes, &state->memory);
}

static void HX_OS_CALL deallocate(void *memory, void *user)
{
	reentry_state *state = user;

	test_reentry(state);
	test_deallocate(memory, &state->memory);
}

static void test_allocation(void)
{
	reentry_state state = {{0}, NULL, 0};
	hx_os_context_options options;
	hx_os_context *context = NULL;
	hx_string_view text;
	size_t failure, live;
	int succeeded = 0;

	hx_os_context_options_init(&options);
	options.allocator.allocate = allocate;
	options.allocator.deallocate = deallocate;
	options.allocator.user = &state;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);
	CHECK(hx_os_params_create(context, &state.params, NULL) == HX_OS_OK);
	CHECK(hx_os_params_set_cstr(state.params, "existing", "keep", NULL) ==
		HX_OS_OK);
	live = state.memory.live;
	state.active = 1;
	state.memory.fail = state.memory.calls + 1;
	CHECK(hx_os_params_set_cstr(state.params, "existing",
		      "a longer replacement", NULL) == HX_OS_NO_MEMORY);
	state.memory.fail = 0;
	CHECK(state.memory.live == live);
	CHECK(hx_os_params_get_string(state.params, "existing", &text, NULL) ==
		HX_OS_OK);
	CHECK(!strcmp(text.data, "keep"));
	CHECK(hx_os_params_set_cstr(state.params, "second", "copy", NULL) ==
		HX_OS_OK);
	live = state.memory.live;

	for (failure = 1; failure < 32; ++failure) {
		hx_os_params *copy = NULL;
		hx_os_status status;

		state.memory.fail = state.memory.calls + failure;
		status = hx_os_params_clone(state.params, context, &copy, NULL);
		state.memory.fail = 0;
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		if (status == HX_OS_OK) {
			state.active = 0;
			CHECK(hx_os_params_destroy(&copy, NULL) == HX_OS_OK);
			state.active = 1;
			succeeded = 1;
		}
		else
			CHECK(!copy);
		CHECK(state.memory.live == live);
		if (succeeded)
			break;
	}
	CHECK(succeeded);
	CHECK(hx_os_params_destroy(&state.params, NULL) == HX_OS_OK);
	state.active = 0;
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!state.memory.live);

	/* A first insertion needs both payload and entry-array allocations. */
	for (failure = 1; failure <= 2; ++failure) {
		test_memory memory = {0};
		hx_os_params *params = NULL;

		context = test_context(&memory);
		CHECK(hx_os_params_create(context, &params, NULL) == HX_OS_OK);
		memory.fail = memory.calls + failure;
		CHECK(hx_os_params_set_cstr(params, "key", "value", NULL) ==
			HX_OS_NO_MEMORY);
		CHECK(!hx_os_params_count(params) &&
			!hx_os_params_storage_bytes(params));
		memory.fail = 0;
		CHECK(hx_os_params_destroy(&params, NULL) == HX_OS_OK);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
			!memory.live);
	}
}

int main(void)
{
	hx_os_params *params = NULL;

	CHECK(!hx_os_params_count(NULL));
	CHECK(hx_os_params_destroy(&params, NULL) == HX_OS_OK);
	if (!hx_os_params_available()) {
		CHECK(hx_os_params_create(NULL, &params, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!params);
		CHECK(hx_os_params_set_i64(NULL, "disabled", 0, NULL) ==
			HX_OS_NOT_SUPPORTED);

		return 0;
	}
	test_values();
	test_limits();
	test_allocation();

	return 0;
}
