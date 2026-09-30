#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)
static hx_string_view view(const char *text)
{
	return hx_string_view_from_cstr(text);
}
typedef struct allocation_state {
	size_t calls, fail, live;
	hx_settings *reenter;
	hx_settings *closing;
} allocation_state;
static void *HX_OS_CALL allocate(size_t bytes, void *user)
{
	allocation_state *state = user;
	void *memory;
	if (state->reenter) {
		int64_t value;
		hx_settings *settings = state->reenter;
		state->reenter = NULL;
		CHECK(hx_settings_get_i64(settings, "n", &value, NULL) ==
				HX_OS_OK &&
			value == 42);
	}
	if (++state->calls == state->fail)
		return NULL;
	memory = malloc(bytes);
	if (memory)
		++state->live;
	return memory;
}
static void HX_OS_CALL deallocate(void *memory, void *user)
{
	allocation_state *state = user;
	if (state->closing) {
		int64_t value;
		CHECK(hx_settings_get_i64(state->closing, "n", &value, NULL) ==
			HX_OS_CLOSED);
	}
	if (memory) {
		--state->live;
		free(memory);
	}
}
static void value_test(void)
{
	hx_settings_value *number = NULL, *text = NULL, *array = NULL;
	hx_settings_value *object = NULL, *child = NULL, *copy = NULL,
			  *bad = NULL;
	const hx_settings_value *items[2];
	hx_settings_pair pairs[2];
	hx_string output = HX_STRING_INIT;
	hx_string_view embedded = {"中文\0😀", sizeof("中文\0😀") - 1};
	hx_string_view key = {"a\0b", 3};
	int64_t integer = 99;
	double real;
	int boolean;
	void *pointer;
	size_t i;
	CHECK(hx_settings_value_i64(NULL, INT64_MIN, &number, NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_value_string(NULL, embedded, &text, NULL) ==
		HX_OS_OK);
	items[0] = number;
	items[1] = text;
	CHECK(hx_settings_value_array(NULL, items, 2, &array, NULL) ==
		HX_OS_OK);
	hx_settings_value_release(number);
	hx_settings_value_release(text);
	number = NULL;
	text = NULL;
	pairs[0].name = view("z");
	pairs[0].value = array;
	pairs[1].name = key;
	pairs[1].value = array;
	CHECK(hx_settings_value_object(NULL, pairs, 2, &object, NULL) ==
		HX_OS_OK);
	hx_settings_value_release(array);
	array = NULL;
	CHECK(hx_settings_value_name_at(object, 0, &output, NULL) == HX_OS_OK &&
		output.size == 3 &&
		!memcmp(hx_string_c_str(&output), key.data, 3));
	CHECK(hx_settings_value_member(object, key, &array, NULL) == HX_OS_OK);
	CHECK(hx_settings_value_at(array, 0, &number, NULL) == HX_OS_OK);
	CHECK(hx_settings_value_get_i64(number, &integer, NULL) == HX_OS_OK &&
		integer == INT64_MIN);
	CHECK(hx_settings_value_at(array, 1, &text, NULL) == HX_OS_OK);
	CHECK(hx_settings_value_get_string(text, &output, NULL) == HX_OS_OK &&
		output.size == embedded.size &&
		!memcmp(hx_string_c_str(&output), embedded.data,
			embedded.size));
	CHECK(hx_settings_value_get_i64(text, &integer, NULL) ==
			HX_OS_TYPE_MISMATCH &&
		integer == INT64_MIN);
	CHECK(hx_settings_value_copy(NULL, object, &copy, NULL) == HX_OS_OK);
	hx_settings_value_release(object);
	object = NULL;
	CHECK(hx_settings_value_count(copy) == 2);
	CHECK(hx_settings_value_at(copy, 7, &child, NULL) == HX_OS_RANGE &&
		!child);
	pairs[0].name = key;
	pairs[0].value = number;
	pairs[1].name = key;
	pairs[1].value = text;
	CHECK(hx_settings_value_object(NULL, pairs, 2, &bad, NULL) ==
			HX_OS_EXISTS &&
		!bad);
	CHECK(hx_settings_value_double(NULL, HUGE_VAL, &bad, NULL) ==
		HX_OS_UNREPRESENTABLE);
	CHECK(hx_settings_value_bool(NULL, 2, &bad, NULL) == HX_OS_INVALID);
	CHECK(hx_settings_value_string(NULL, view("\xff"), &bad, NULL) ==
		HX_OS_INVALID_ENCODING);
	CHECK(hx_settings_value_double(NULL, 0.25, &child, NULL) == HX_OS_OK);
	CHECK(hx_settings_value_get_double(child, &real, NULL) == HX_OS_OK &&
		real == 0.25);
	hx_settings_value_release(child);
	CHECK(hx_settings_value_bool(NULL, 1, &child, NULL) == HX_OS_OK);
	CHECK(hx_settings_value_get_bool(child, &boolean, NULL) == HX_OS_OK &&
		boolean);
	hx_settings_value_release(child);
	CHECK(hx_settings_value_pointer(NULL, &integer, &child, NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_value_get_pointer(child, &pointer, NULL) ==
			HX_OS_OK &&
		pointer == &integer);
	hx_settings_value_release(child);
	child = NULL;
	CHECK(hx_settings_value_null(NULL, &child, NULL) == HX_OS_OK);
	for (i = 1; i < 64; ++i) {
		hx_settings_value *next = NULL;
		items[0] = child;
		CHECK(hx_settings_value_array(NULL, items, 1, &next, NULL) ==
			HX_OS_OK);
		hx_settings_value_release(child);
		child = next;
	}
	items[0] = child;
	CHECK(hx_settings_value_array(NULL, items, 1, &bad, NULL) ==
		HX_OS_LIMIT);
	hx_settings_value_release(child);
	hx_settings_value_release(copy);
	hx_settings_value_release(array);
	hx_settings_value_release(number);
	hx_settings_value_release(text);
	hx_string_destroy(&output);
}
static void defaults_test(void)
{
	hx_settings *settings = NULL;
	hx_settings_value *snapshot = NULL, *value = NULL, *array = NULL;
	const hx_settings_value *items[1];
	hx_settings_pair member;
	hx_string output = HX_STRING_INIT;
	hx_string_view embedded_key = {"a\0b", 3},
		       embedded_value = {"v\0😀", 6};
	int64_t integer = 99;
	int boolean = 9;
	double real = 9;
	void *pointer = NULL;
	CHECK(hx_settings_create(NULL, &settings, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_default_i64(settings, "n", 7, NULL) == HX_OS_OK);
	CHECK(hx_settings_get_i64(settings, "n", &integer, NULL) == HX_OS_OK &&
		integer == 7);
	CHECK(hx_settings_set_string(settings, "n", "wrong type", NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_get_i64(settings, "n", &integer, NULL) ==
			HX_OS_TYPE_MISMATCH &&
		integer == 7);
	CHECK(hx_settings_unset(settings, view("n"), HX_SETTINGS_EXPLICIT,
		      NULL) == HX_OS_OK);
	CHECK(hx_settings_get_i64(settings, "n", &integer, NULL) == HX_OS_OK &&
		integer == 7);
	CHECK(hx_settings_unset(settings, view("n"), HX_SETTINGS_EXPLICIT,
		      NULL) == HX_OS_NOT_FOUND);
	CHECK(hx_settings_set_default_bool(settings, "b", 1, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_null(settings, "b", NULL) == HX_OS_OK);
	CHECK(hx_settings_get_bool(settings, "b", &boolean, NULL) ==
			HX_OS_TYPE_MISMATCH &&
		boolean == 9);
	CHECK(hx_settings_set_bool(settings, "b", 0, NULL) == HX_OS_OK);
	CHECK(hx_settings_get_bool(settings, "b", &boolean, NULL) == HX_OS_OK &&
		!boolean);
	CHECK(hx_settings_set_default_double(settings, "real", 0.5, NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_get_double(settings, "real", &real, NULL) ==
			HX_OS_OK &&
		real == 0.5);
	CHECK(hx_settings_set_double(settings, "real", 0.25, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_default_pointer(settings, "p", &integer, NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_get_pointer(settings, "p", &pointer, NULL) ==
			HX_OS_OK &&
		pointer == &integer);
	CHECK(hx_settings_set_pointer(settings, "p", NULL, NULL) == HX_OS_OK);
	CHECK(hx_settings_get_pointer(settings, "p", &pointer, NULL) ==
			HX_OS_OK &&
		!pointer);
	CHECK(hx_settings_set_default_null(settings, "null", NULL) == HX_OS_OK);
	CHECK(hx_settings_set_default_string(
		      settings, "label", "默认😀", NULL) == HX_OS_OK);
	CHECK(hx_settings_get_string(settings, "label", &output, NULL) ==
			HX_OS_OK &&
		!strcmp(hx_string_c_str(&output), "默认😀"));
	CHECK(hx_settings_set_default_string_view(settings, embedded_key,
		      embedded_value, NULL) == HX_OS_OK);
	CHECK(hx_settings_get_value(settings, embedded_key, &value, NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_value_get_string(value, &output, NULL) == HX_OS_OK &&
		output.size == embedded_value.size);
	hx_settings_value_release(value);
	value = NULL;
	CHECK(hx_settings_value_i64(NULL, 12, &value, NULL) == HX_OS_OK);
	items[0] = value;
	CHECK(hx_settings_set_default_array(
		      settings, "array", items, 1, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_array(settings, "array", items, 1, NULL) ==
		HX_OS_OK);
	member.name = view("x");
	member.value = value;
	CHECK(hx_settings_value_object(NULL, &member, 1, &array, NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_set_default_object(settings, "object", array, NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_set_object(settings, "object", array, NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_set_value(settings, view("generic"), value, NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_set_default_value(
		      settings, view("generic"), array, NULL) == HX_OS_OK);
	hx_settings_value_release(value);
	hx_settings_value_release(array);
	CHECK(hx_settings_snapshot(settings, HX_SETTINGS_EFFECTIVE, &snapshot,
		      NULL) == HX_OS_OK);
	CHECK(hx_settings_set_i64(settings, "n", 42, NULL) == HX_OS_OK);
	CHECK(hx_settings_value_member(snapshot, view("n"), &value, NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_value_get_i64(value, &integer, NULL) == HX_OS_OK &&
		integer == 7);
	hx_settings_value_release(value);
	CHECK(hx_settings_destroy(&settings, NULL) == HX_OS_OK);
	CHECK(hx_settings_value_count(snapshot) > 5);
	hx_settings_value_release(snapshot);
	hx_string_destroy(&output);
}
static void limits_test(void)
{
	hx_settings *settings = NULL;
	hx_settings_options options = HX_SETTINGS_OPTIONS_INIT;
	int64_t value = 0;
	CHECK(hx_settings_options_init(&options, sizeof(options) - 1) ==
		HX_OS_OUTPUT_FULL);
	CHECK(hx_settings_options_init(&options, sizeof(options)) == HX_OS_OK);
	options.max_nodes = 2;
	CHECK(hx_settings_create_with_options(
		      NULL, &options, &settings, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_i64(settings, "one", 1, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_i64(settings, "two", 2, NULL) == HX_OS_LIMIT);
	CHECK(hx_settings_get_i64(settings, "one", &value, NULL) == HX_OS_OK &&
		value == 1);
	CHECK(hx_settings_destroy(&settings, NULL) == HX_OS_OK);
	options.max_depth = 1;
	CHECK(hx_settings_create_with_options(
		      NULL, &options, &settings, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_i64(settings, "one", 1, NULL) == HX_OS_LIMIT);
	CHECK(hx_settings_destroy(&settings, NULL) == HX_OS_OK);
	options.max_memory_bytes = 1;
	CHECK(hx_settings_create_with_options(
		      NULL, &options, &settings, NULL) == HX_OS_LIMIT &&
		!settings);
}
static void configuration_allocation_test(
	hx_settings *settings, allocation_state *state)
{
	hx_os_config_document *replacement = NULL, *baseline = NULL;
	const char next[] =
		"{\"array\":[true,\"中文😀\"],\"object\":{\"v\":1}}";
	const char previous[] = "{\"n\":42,\"label\":\"old\",\"another\":3}";
	hx_string output = HX_STRING_INIT;
	size_t i;
	int64_t number;
	if (!(hx_os_capabilities() & HX_OS_CAP_CONFIG))
		return;
	CHECK(hx_os_config_parse(NULL, HX_OS_CONFIG_JSON, next,
		      sizeof(next) - 1, NULL, &replacement, NULL) == HX_OS_OK);
	CHECK(hx_os_config_parse(NULL, HX_OS_CONFIG_JSON, previous,
		      sizeof(previous) - 1, NULL, &baseline, NULL) == HX_OS_OK);
	for (i = 1; i <= 32; ++i) {
		hx_os_status status;
		size_t live = state->live;
		state->calls = 0;
		state->fail = i;
		status = hx_settings_import(settings, replacement, NULL);
		state->fail = 0;
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		if (status == HX_OS_NO_MEMORY) {
			CHECK(state->live == live);
			CHECK(hx_settings_get_i64(settings, "n", &number,
				      NULL) == HX_OS_OK &&
				number == 42);
		}
		CHECK(hx_settings_import(settings, baseline, NULL) == HX_OS_OK);
		CHECK(state->live == live);
	}
	if (hx_os_capabilities() & HX_OS_CAP_CONFIG_SERIALIZE)
		for (i = 1; i <= 32; ++i) {
			hx_os_status status;
			size_t live = state->live;
			CHECK(hx_string_assign_cstr(&output, "keep", NULL) ==
				HX_OS_OK);
			state->calls = 0;
			state->fail = i;
			status = hx_settings_to_json(settings, &output, NULL);
			state->fail = 0;
			CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
			CHECK(state->live == live);
			if (status == HX_OS_NO_MEMORY)
				CHECK(!strcmp(
					hx_string_c_str(&output), "keep"));
		}
	hx_string_destroy(&output);
	hx_os_config_release(baseline);
	hx_os_config_release(replacement);
}
static void allocation_test(void)
{
	allocation_state state = {0, 0, 0, NULL, NULL};
	hx_os_context_options options;
	hx_os_context *context = NULL;
	hx_settings *settings = NULL, *copy = NULL;
	hx_settings_value *snapshot = NULL;
	hx_string output = HX_STRING_INIT;
	size_t i;
	hx_os_context_options_init(&options);
	options.allocator.allocate = allocate;
	options.allocator.deallocate = deallocate;
	options.allocator.user = &state;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);
	for (i = 1; i < 7; ++i) {
		hx_os_status status;
		state.calls = 0;
		state.fail = i;
		status = hx_settings_create(context, &settings, NULL);
		state.fail = 0;
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		if (settings)
			CHECK(hx_settings_destroy(&settings, NULL) == HX_OS_OK);
		CHECK(hx_os_context_allocations(context) == 0 &&
			state.live == 1);
	}
	CHECK(hx_settings_create(context, &settings, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_i64(settings, "n", 42, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_string(settings, "label", "old", NULL) ==
		HX_OS_OK);
	for (i = 1; i <= 4; ++i) {
		hx_os_status status;
		size_t live = state.live;
		state.calls = 0;
		state.fail = i;
		status = hx_settings_set_string(settings, "label", "new", NULL);
		state.fail = 0;
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		if (status == HX_OS_NO_MEMORY) {
			CHECK(state.live == live);
			CHECK(hx_settings_get_string(settings, "label", &output,
				      NULL) == HX_OS_OK &&
				!strcmp(hx_string_c_str(&output), "old"));
		}
		CHECK(hx_settings_set_string(settings, "label", "old", NULL) ==
			HX_OS_OK);
	}
	state.reenter = settings;
	CHECK(hx_settings_set_i64(settings, "another", 3, NULL) == HX_OS_OK &&
		!state.reenter);
	for (i = 1; i < 16; ++i) {
		hx_os_status status;
		size_t live = state.live;
		state.calls = 0;
		state.fail = i;
		status = hx_settings_copy(context, settings, &copy, NULL);
		state.fail = 0;
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		if (copy)
			CHECK(hx_settings_destroy(&copy, NULL) == HX_OS_OK);
		CHECK(state.live == live);
	}
	configuration_allocation_test(settings, &state);
	CHECK(hx_settings_snapshot(settings, HX_SETTINGS_EXPLICIT, &snapshot,
		      NULL) == HX_OS_OK);
	CHECK(hx_settings_copy(NULL, settings, &copy, NULL) == HX_OS_OK);
	state.closing = settings;
	CHECK(hx_settings_destroy(&settings, NULL) == HX_OS_OK);
	state.closing = NULL;
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);
	hx_settings_value_release(snapshot);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK && !state.live);
	CHECK(hx_settings_get_string(copy, "label", &output, NULL) ==
			HX_OS_OK &&
		!strcmp(hx_string_c_str(&output), "old"));
	CHECK(hx_settings_destroy(&copy, NULL) == HX_OS_OK);
	hx_string_destroy(&output);
}
int main(void)
{
	hx_settings *settings = NULL;
	if (!(hx_os_capabilities() & HX_OS_CAP_SETTINGS)) {
		CHECK(hx_settings_create(NULL, &settings, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!settings);
		return 0;
	}
	value_test();
	defaults_test();
	limits_test();
	allocation_test();
	return 0;
}
