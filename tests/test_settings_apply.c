#include "utility_test.h"

static void test_overlay(void)
{
	hx_settings *source = NULL, *target = NULL;
	hx_settings_value *one = NULL, *nested = NULL, *old = NULL,
			  *current = NULL;
	hx_settings_pair pair;
	size_t applied;
	int64_t integer;
	int payload = 7;
	void *pointer = NULL;

	CHECK(hx_settings_create(NULL, &source, NULL) == HX_OS_OK);
	CHECK(hx_settings_create(NULL, &target, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_i64(target, "keep", 42, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_default_i64(target, "default", 11, NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_set_default_i64(source, "default", 99, NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_set_i64(source, "port", 8080, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_pointer(source, "user", &payload, NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_value_i64(NULL, 1, &one, NULL) == HX_OS_OK);
	pair.name = hx_string_view_from_cstr("only");
	pair.value = one;
	CHECK(hx_settings_value_object(NULL, &pair, 1, &nested, NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_set_value(source, hx_string_view_from_cstr("nested"),
		      nested, NULL) == HX_OS_OK);
	pair.name = hx_string_view_from_cstr("old");
	CHECK(hx_settings_value_object(NULL, &pair, 1, &old, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_value(target, hx_string_view_from_cstr("nested"),
		      old, NULL) == HX_OS_OK);

	CHECK(hx_settings_apply(target, source, &applied, NULL) == HX_OS_OK);
	CHECK(applied == 3);
	CHECK(hx_settings_get_i64(target, "keep", &integer, NULL) == HX_OS_OK &&
		integer == 42);
	CHECK(hx_settings_get_i64(target, "default", &integer, NULL) ==
			HX_OS_OK &&
		integer == 11);
	CHECK(hx_settings_get_i64(target, "port", &integer, NULL) == HX_OS_OK &&
		integer == 8080);
	CHECK(hx_settings_get_pointer(target, "user", &pointer, NULL) ==
			HX_OS_OK &&
		pointer == &payload);
	CHECK(hx_settings_get_value(target, hx_string_view_from_cstr("nested"),
		      &current, NULL) == HX_OS_OK);
	CHECK(hx_settings_value_count(current) == 1);
	CHECK(hx_settings_value_member(current, hx_string_view_from_cstr("old"),
		      &one, NULL) == HX_OS_NOT_FOUND);
	CHECK(hx_settings_apply_value(target, nested, &applied, NULL) ==
			HX_OS_OK &&
		applied == 1);
	CHECK(hx_settings_apply(target, target, &applied, NULL) == HX_OS_OK &&
		applied == 5);
	CHECK(hx_settings_apply_value(target, one, &applied, NULL) ==
			HX_OS_TYPE_MISMATCH &&
		applied == 0);

	CHECK(hx_settings_destroy(&source, NULL) == HX_OS_OK);
	CHECK(hx_settings_get_i64(target, "port", &integer, NULL) == HX_OS_OK &&
		integer == 8080);
	CHECK(hx_settings_destroy(&target, NULL) == HX_OS_OK);
	hx_settings_value_release(one);
	hx_settings_value_release(nested);
	hx_settings_value_release(old);
	hx_settings_value_release(current);
}

static void test_limit(void)
{
	hx_settings *source = NULL, *target = NULL;
	hx_settings_options options = HX_SETTINGS_OPTIONS_INIT;
	size_t applied = 999;
	int64_t value;

	options.max_nodes = 2;
	options.max_active_operations = 1;
	CHECK(hx_settings_create(NULL, &source, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_i64(source, "b", 2, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_i64(source, "a", 1, NULL) == HX_OS_OK);
	CHECK(hx_settings_create_with_options(NULL, &options, &target, NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_apply(target, source, &applied, NULL) == HX_OS_LIMIT);
	CHECK(applied == 1);
	CHECK(hx_settings_get_i64(target, "a", &value, NULL) == HX_OS_OK &&
		value == 1);
	CHECK(hx_settings_get_i64(target, "b", &value, NULL) ==
		HX_OS_NOT_FOUND);
	CHECK(hx_settings_apply(target, target, &applied, NULL) == HX_OS_OK &&
		applied == 1);
	CHECK(hx_settings_destroy(&source, NULL) == HX_OS_OK);
	CHECK(hx_settings_destroy(&target, NULL) == HX_OS_OK);
}

typedef struct observer_state {
	test_memory memory;
	size_t notifications;
} observer_state;

static void HX_OS_CALL observe(
	hx_settings *settings, const hx_settings_change *change, void *user)
{
	observer_state *state = user;
	int64_t current;

	CHECK(change->kind == HX_SETTINGS_SET && change->key.size == 1);
	CHECK(change->key.data[0] == 'a');
	CHECK(hx_settings_get_i64(settings, "a", &current, NULL) == HX_OS_OK &&
		current == 1);
	CHECK(hx_settings_destroy(&settings, NULL) == HX_OS_DEADLOCK);
	++state->notifications;
	state->memory.fail = state->memory.calls + 1;
}

static void test_failed_allocation(void)
{
	observer_state state = {{0, 0, 0}, 0};
	hx_os_context *context = test_context(&state.memory);
	hx_settings *source = NULL, *target = NULL;
	hx_settings_options options = HX_SETTINGS_OPTIONS_INIT;
	size_t applied = 99;
	int64_t value;

	options.on_change = observe;
	options.user = &state;
	CHECK(hx_settings_create(NULL, &source, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_i64(source, "a", 1, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_i64(source, "b", 2, NULL) == HX_OS_OK);
	CHECK(hx_settings_create_with_options(
		      context, &options, &target, NULL) == HX_OS_OK);
	CHECK(hx_settings_apply(target, source, &applied, NULL) ==
		HX_OS_NO_MEMORY);
	state.memory.fail = 0;
	CHECK(applied == 1 && state.notifications == 1);
	CHECK(hx_settings_get_i64(target, "a", &value, NULL) == HX_OS_OK &&
		value == 1);
	CHECK(hx_settings_get_i64(target, "b", &value, NULL) ==
		HX_OS_NOT_FOUND);
	CHECK(hx_settings_destroy(&source, NULL) == HX_OS_OK);
	CHECK(hx_settings_destroy(&target, NULL) == HX_OS_OK);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(state.memory.live == 0);
}

int main(void)
{
	if (!(hx_os_capabilities() & HX_OS_CAP_SETTINGS)) {
		size_t applied = 123;

		CHECK(hx_settings_apply(NULL, NULL, &applied, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			applied == 0);
		return 0;
	}

	test_overlay();
	test_limit();
	test_failed_allocation();
	puts("settings overlay contracts passed");

	return 0;
}
