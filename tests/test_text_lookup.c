#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)
typedef struct allocation_state {
	size_t calls, fail, live;
} allocation_state;

static void *HX_OS_CALL allocate(size_t bytes, void *user)
{
	allocation_state *state = user;
	void *memory;

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

	if (memory) {
		--state->live;

		free(memory);
	}
}

static void *read_table(void *user)
{
	hx_os_text_lookup *table = user;
	hx_string text = HX_STRING_INIT;
	int i;

	for (i = 0; i < 300; ++i) {
		CHECK(hx_os_text_lookup_get_string(
			      table, "hello", &text, NULL) == HX_OS_OK &&
			!strcmp(hx_string_c_str(&text), "你好😀"));
		CHECK(hx_os_text_lookup_get_string(
			      table, "absent", &text, NULL) == HX_OS_OK &&
			!strcmp(hx_string_c_str(&text), "absent"));
	}

	hx_string_destroy(&text);

	return NULL;
}

int main(void)
{
	hx_os_text_lookup *table = NULL;
	hx_os_text_lookup_options options = HX_OS_TEXT_LOOKUP_OPTIONS_INIT;
	hx_os_context_options context_options;
	hx_os_context *context = NULL;
	hx_os_thread *threads[4];
	allocation_state state = {0, 0, 0};
	hx_string text = HX_STRING_INIT;
	size_t i, required;
	char buffer[32] = "keep";

	if (!(hx_os_capabilities() & HX_OS_CAP_TEXT_LOOKUP)) {
		CHECK(hx_os_text_lookup_create(NULL, &table, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!table);

		return 0;
	}

	CHECK(hx_os_text_lookup_options_init(&options, sizeof(options) - 1) ==
		HX_OS_OUTPUT_FULL);
	CHECK(hx_os_text_lookup_options_init(&options, sizeof(options)) ==
		HX_OS_OK);

	options.max_entries = 2;
	options.max_text_bytes = 64;
	hx_os_context_options_init(&context_options);
	context_options.allocator.allocate = allocate;
	context_options.allocator.deallocate = deallocate;
	context_options.allocator.user = &state;
	CHECK(hx_os_context_create(&context_options, &context, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_text_lookup_create_with_options(
		      context, &options, &table, NULL) == HX_OS_OK);

	for (i = 1; i <= 2; ++i) {
		size_t live = state.live;

		state.calls = 0;
		state.fail = i;
		CHECK(hx_os_text_lookup_add(table, "hello", "你好😀", NULL) ==
			HX_OS_NO_MEMORY);

		state.fail = 0;
		CHECK(state.live == live);
		CHECK(hx_os_text_lookup_get_string(
			      table, "hello", &text, NULL) == HX_OS_OK &&
			!strcmp(hx_string_c_str(&text), "hello"));
	}

	CHECK(hx_os_text_lookup_add(table, "hello", "你好😀", NULL) ==
		HX_OS_OK);
	CHECK(hx_os_text_lookup_add(table, "empty", "", NULL) == HX_OS_OK);
	CHECK(hx_os_text_lookup_add(table, "third", "x", NULL) == HX_OS_LIMIT);

	state.calls = 0;
	state.fail = 1;
	CHECK(hx_os_text_lookup_add(table, "hello", "bad replacement", NULL) ==
		HX_OS_NO_MEMORY);

	state.fail = 0;
	CHECK(hx_os_text_lookup_get(table, "hello", buffer, 1, &required,
		      NULL) == HX_OS_OUTPUT_FULL &&
		!strcmp(buffer, "keep"));
	CHECK(required == strlen("你好😀") + 1);
	CHECK(hx_os_text_lookup_get(table, "hello", buffer, sizeof(buffer),
		      &required, NULL) == HX_OS_OK &&
		!strcmp(buffer, "你好😀"));
	CHECK(hx_os_text_lookup_get_string(table, "empty", &text, NULL) ==
			HX_OS_OK &&
		!text.size);
	CHECK(hx_os_text_lookup_add(table, "hello", "\xff", NULL) ==
		HX_OS_INVALID_ENCODING);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);

	{
		hx_string owned;

		hx_string_init(&owned, context);
		state.calls = 0;
		state.fail = 1;
		CHECK(hx_os_text_lookup_get_string(table, "hello", &owned,
			      NULL) == HX_OS_NO_MEMORY &&
			!owned.size);

		state.fail = 0;

		hx_string_destroy(&owned);
	}

	for (i = 0; i < 4; ++i)
		CHECK(hx_os_thread_create(NULL, read_table, table, &threads[i],
			      NULL) == HX_OS_OK);

	for (i = 0; i < 4; ++i) {
		CHECK(hx_os_thread_join_until(threads[i],
			      hx_os_deadline_after_ms(3000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&threads[i], NULL) == HX_OS_OK);
	}

	CHECK(hx_os_text_lookup_add(table, "hello", "replacement", NULL) ==
		HX_OS_OK);
	CHECK(hx_os_text_lookup_get_string(table, "hello", &text, NULL) ==
			HX_OS_OK &&
		!strcmp(hx_string_c_str(&text), "replacement"));

	hx_os_text_lookup_destroy(&table);
	CHECK(!table && hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!state.live);

	hx_string_destroy(&text);

	return 0;
}
