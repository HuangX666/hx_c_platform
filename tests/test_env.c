#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
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

typedef struct env_state {
	hx_os_atomic failed;
	char name[96];
} env_state;

static void *env_writer(void *user)
{
	env_state *state = user;
	int i;
	for (i = 0; i < 300; ++i)
		if (hx_os_env_set(state->name, i % 2 ? "中文值" : "value", 1,
			    NULL) != HX_OS_OK)
			hx_os_atomic_store(&state->failed, 1, HX_OS_RELAXED);
	return NULL;
}

static void *env_reader(void *user)
{
	env_state *state = user;
	hx_string value = HX_STRING_INIT;
	int i;
	for (i = 0; i < 300; ++i) {
		hx_os_status status =
			hx_os_env_get_string(state->name, &value, NULL);
		if (status != HX_OS_OK ||
			(strcmp(hx_string_c_str(&value), "中文值") &&
				strcmp(hx_string_c_str(&value), "value")))
			hx_os_atomic_store(&state->failed, 1, HX_OS_RELAXED);
	}
	hx_string_destroy(&value);
	return NULL;
}

static void *env_allocate(size_t bytes, void *user)
{
	char value[8];
	size_t required;
	CHECK(hx_os_env_get(user, value, sizeof(value), &required, NULL) ==
		HX_OS_OK);
	return malloc(bytes);
}

static void env_deallocate(void *memory, void *user)
{
	(void)user;
	free(memory);
}

int main(void)
{
	env_state state;
	hx_string value = HX_STRING_INIT;
	hx_os_thread *writer = NULL, *reader = NULL;
	hx_os_context *context = NULL;
	hx_os_context_options options;
	char buffer[64];
	size_t required;
	memset(&state, 0, sizeof(state));
	(void)snprintf(state.name, sizeof(state.name), "HX_OS_ENV_TEST_%llx",
		(unsigned long long)hx_os_monotonic_ns());
	if (!(hx_os_capabilities() & HX_OS_CAP_ENV)) {
		CHECK(hx_os_env_get(state.name, buffer, sizeof(buffer),
			      &required, NULL) == HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_env_get_string(state.name, &value, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_env_set(state.name, "x", 1, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_env_remove(state.name, NULL) ==
			HX_OS_NOT_SUPPORTED);
		return 0;
	}
	CHECK(hx_os_env_remove(state.name, NULL) == HX_OS_OK);
	CHECK(hx_os_env_get_string(state.name, &value, NULL) ==
		HX_OS_NOT_FOUND);
	CHECK(hx_os_env_set("bad=name", "x", 1, NULL) == HX_OS_INVALID);
	CHECK(hx_os_env_set(state.name, "\xff", 1, NULL) ==
		HX_OS_INVALID_ENCODING);
	CHECK(hx_os_env_set(state.name, "", 1, NULL) == HX_OS_OK);
	CHECK(hx_os_env_get(state.name, NULL, 0, &required, NULL) ==
		HX_OS_OUTPUT_FULL);
	CHECK(required == 1);
	CHECK(hx_os_env_get_string(state.name, &value, NULL) == HX_OS_OK &&
		!value.size);
	CHECK(hx_os_env_set(state.name, "must-not-replace-empty", 0, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_env_get_string(state.name, &value, NULL) == HX_OS_OK &&
		!value.size);
	CHECK(hx_os_env_set(state.name, "中文值", 1, NULL) == HX_OS_OK);
	CHECK(hx_os_env_get(state.name, buffer, 2, &required, NULL) ==
		HX_OS_OUTPUT_FULL);
	CHECK(required == strlen("中文值") + 1);
	CHECK(hx_os_env_get(state.name, buffer, sizeof(buffer), &required,
		      NULL) == HX_OS_OK);
	CHECK(!strcmp(buffer, "中文值"));
	CHECK(hx_os_env_set(state.name, "value", 1, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_create(NULL, env_writer, &state, &writer, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_thread_create(NULL, env_reader, &state, &reader, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_thread_join_until(writer, hx_os_deadline_after_ms(5000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_join_until(reader, hx_os_deadline_after_ms(5000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&writer, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&reader, NULL) == HX_OS_OK);
	CHECK(!hx_os_atomic_load(&state.failed, HX_OS_RELAXED));
	CHECK(hx_os_env_set(state.name, "short", 1, NULL) == HX_OS_OK);
	hx_os_context_options_init(&options);
	options.allocator.allocate = env_allocate;
	options.allocator.deallocate = env_deallocate;
	options.allocator.user = state.name;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);
	hx_string_destroy(&value);
	hx_string_init(&value, context);
	CHECK(hx_os_env_get_string(state.name, &value, NULL) == HX_OS_OK);
#if !defined(_WIN32)
	CHECK(setenv(state.name, "\xff", 1) == 0);
	CHECK(hx_os_env_get_string(state.name, &value, NULL) ==
		HX_OS_INVALID_ENCODING);
	CHECK(!strcmp(hx_string_c_str(&value), "short"));
#endif
	CHECK(hx_os_env_remove(state.name, NULL) == HX_OS_OK);
	CHECK(hx_os_env_remove(state.name, NULL) == HX_OS_OK);
	CHECK(hx_os_env_get_string(state.name, &value, NULL) ==
		HX_OS_NOT_FOUND);
	CHECK(!strcmp(hx_string_c_str(&value), "short"));
	hx_string_destroy(&value);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	puts("environment contracts passed");
	return 0;
}
