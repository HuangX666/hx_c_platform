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

typedef hx_os_status(HX_OS_CALL *path_buffer_fn)(
	char *, size_t, size_t *, hx_os_error *);
typedef hx_os_status(HX_OS_CALL *path_string_fn)(hx_string *, hx_os_error *);

static void check_path(
	path_buffer_fn get, path_string_fn get_string, int optional)
{
	hx_string value = HX_STRING_INIT;
	char *buffer, tiny[1] = {1};
	size_t required = 0;
	hx_os_status status = get_string(&value, NULL);
	if (optional && status == HX_OS_NOT_SUPPORTED) {
		puts("module path unavailable: NOT_SUPPORTED");
		return;
	}
	CHECK(status == HX_OS_OK && value.size > 0);
	CHECK(get(NULL, 0, &required, NULL) == HX_OS_OUTPUT_FULL);
	CHECK(required == value.size + 1);
	CHECK(get(tiny, sizeof(tiny), &required, NULL) == HX_OS_OUTPUT_FULL);
	CHECK(!tiny[0]);
	buffer = malloc(required);
	CHECK(buffer);
	CHECK(get(buffer, required, &required, NULL) == HX_OS_OK);
	CHECK(!strcmp(buffer, value.data));
	free(buffer);
	hx_string_destroy(&value);
}

static void *fail_allocate(size_t bytes, void *user)
{
	return *(int *)user ? NULL : malloc(bytes);
}
static void fail_deallocate(void *memory, void *user)
{
	(void)user;
	free(memory);
}

int main(void)
{
	hx_string value = HX_STRING_INIT;
	hx_os_context *context = NULL;
	hx_os_context_options options;
	int fail = 0;
	if (!(hx_os_capabilities() & HX_OS_CAP_PATHS)) {
		size_t size;
		CHECK(hx_os_path_home_string(&value, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_path_executable(NULL, 0, &size, NULL) ==
			HX_OS_NOT_SUPPORTED);
		return 0;
	}
	check_path(hx_os_path_home, hx_os_path_home_string, 0);
	check_path(hx_os_path_config_home, hx_os_path_config_home_string, 0);
	check_path(hx_os_path_data_home, hx_os_path_data_home_string, 0);
	check_path(hx_os_path_program_data, hx_os_path_program_data_string, 0);
	check_path(hx_os_path_temp, hx_os_path_temp_string, 0);
	check_path(hx_os_path_executable, hx_os_path_executable_string, 0);
	check_path(hx_os_path_module, hx_os_path_module_string, 1);
#if !defined(_WIN32)
	/* This isolated test process has no concurrent environment users. */
	CHECK(setenv("HOME", "/hx-path-test/中文/", 1) == 0);
	CHECK(hx_os_path_home_string(&value, NULL) == HX_OS_OK);
	CHECK(!strcmp(value.data, "/hx-path-test/中文"));
#if !defined(__APPLE__)
	CHECK(setenv("XDG_CONFIG_HOME", "relative", 1) == 0);
	CHECK(hx_os_path_config_home_string(&value, NULL) == HX_OS_OK);
	CHECK(!strcmp(value.data, "/hx-path-test/中文/.config"));
	CHECK(setenv("XDG_DATA_HOME", "/hx-path-test/数据/", 1) == 0);
	CHECK(hx_os_path_data_home_string(&value, NULL) == HX_OS_OK);
	CHECK(!strcmp(value.data, "/hx-path-test/数据"));
	CHECK(setenv("XDG_DATA_HOME", "", 1) == 0);
	CHECK(hx_os_path_data_home_string(&value, NULL) == HX_OS_OK);
	CHECK(!strcmp(value.data, "/hx-path-test/中文/.local/share"));
	CHECK(setenv("XDG_DATA_HOME", "/\xff", 1) == 0);
	CHECK(hx_os_path_data_home_string(&value, NULL) ==
		HX_OS_INVALID_ENCODING);
	CHECK(!strcmp(value.data, "/hx-path-test/中文/.local/share"));
	CHECK(hx_os_path_program_data_string(&value, NULL) == HX_OS_OK);
	CHECK(!strcmp(value.data, "/var/lib"));
#endif
	CHECK(setenv("TMPDIR", "/hx-path-test/temp/", 1) == 0);
	CHECK(hx_os_path_temp_string(&value, NULL) == HX_OS_OK);
	CHECK(!strcmp(value.data, "/hx-path-test/temp"));
	{
		hx_os_file_info info;
		CHECK(hx_os_path_stat(value.data, 0, &info, NULL) ==
			HX_OS_NOT_FOUND);
	}
	CHECK(setenv("HOME", "/", 1) == 0);
	CHECK(hx_os_path_home_string(&value, NULL) == HX_OS_OK);
	CHECK(!strcmp(value.data, "/"));
#endif
	hx_string_destroy(&value);
	hx_os_context_options_init(&options);
	options.allocator.allocate = fail_allocate;
	options.allocator.deallocate = fail_deallocate;
	options.allocator.user = &fail;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);
	hx_string_init(&value, context);
	fail = 1;
	CHECK(hx_os_path_executable_string(&value, NULL) == HX_OS_NO_MEMORY);
	CHECK(!value.size && !value.data);
	hx_string_destroy(&value);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	puts("special path contracts passed");
	return 0;
}
