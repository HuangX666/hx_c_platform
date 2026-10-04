#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <time.h>
#endif
#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)
static hx_os_status HX_OS_CALL visit(
	const char *name, const char *value, void *user)
{
	int *found = user;

	if (!strcmp(name, "HX_SYSTEM_SNAPSHOT")) {
		CHECK(!strcmp(value, "before 中文"));

		++*found;
		CHECK(hx_os_env_set(name, "after", 1, NULL) == HX_OS_OK);
	}

	return HX_OS_OK;
}

static hx_os_status HX_OS_CALL stop_visit(
	const char *name, const char *value, void *user)
{
	(void)name;
	(void)value;
	++*(int *)user;

	return HX_OS_CANCELLED;
}

int main(void)
{
	hx_os_string_list *arguments = NULL;
	hx_string text = HX_STRING_INIT;
	uint64_t identifier = 9, features = 9, quotient = 9;
	uint32_t remainder = 9;
	size_t required = 0;
	char buffer[512];
	int emulated = 9, found = 0;
	hx_os_status status;

	CHECK(hx_os_bswap16(UINT16_C(0x1234)) == UINT16_C(0x3412));
	CHECK(hx_os_bswap32(UINT32_C(0x12345678)) == UINT32_C(0x78563412));
	CHECK(hx_os_bswap64(UINT64_C(0x0123456789abcdef)) ==
		UINT64_C(0xefcdab8967452301));
	CHECK(hx_os_byte_order() == HX_OS_LITTLE_ENDIAN ||
		hx_os_byte_order() == HX_OS_BIG_ENDIAN);

	CHECK(hx_os_udiv64(UINT64_MAX, 0, &quotient, &remainder) ==
			HX_OS_INVALID &&
		quotient == 9 && remainder == 9);
	CHECK(hx_os_udiv64(UINT64_MAX, UINT32_C(123456789), &quotient,
		      &remainder) == HX_OS_OK);
	CHECK(quotient == UINT64_MAX / UINT32_C(123456789) &&
		remainder == UINT64_MAX % UINT32_C(123456789));

	if (hx_os_capabilities() & HX_OS_CAP_SYSTEM_UTILS) {
		CHECK(hx_os_process_arguments(NULL, &arguments, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_string_list_count(arguments) > 0);
		CHECK(hx_os_string_list_data(
			      arguments)[hx_os_string_list_count(arguments)] ==
			NULL);
		CHECK(!hx_os_string_list_at(arguments, SIZE_MAX));

		CHECK(hx_os_process_id(&identifier) == HX_OS_OK && identifier);

		CHECK(hx_os_host_name_string(&text, NULL) == HX_OS_OK &&
			text.size);
		CHECK(hx_os_host_name(NULL, 0, &required, NULL) ==
				HX_OS_OUTPUT_FULL &&
			required == text.size + 1);
		CHECK(hx_os_host_name(buffer, sizeof(buffer), &required,
			      NULL) == HX_OS_OK &&
			!strcmp(buffer, hx_string_c_str(&text)));

		CHECK(hx_os_cpu_features_query(&features, NULL) == HX_OS_OK);
		CHECK(!(features & HX_OS_CPU_AVX2) ||
			(features & HX_OS_CPU_AVX));
		CHECK(!(features & HX_OS_CPU_AVX) ||
			(features & HX_OS_CPU_SSE2));

		status = hx_os_sys_emulated(&emulated, NULL);
		CHECK((status == HX_OS_OK &&
			      (emulated == 0 || emulated == 1)) ||
			(status == HX_OS_NOT_SUPPORTED && emulated == 9));
	}
	else {
		CHECK(hx_os_process_arguments(NULL, &arguments, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!arguments);
		CHECK(hx_os_process_id(&identifier) == HX_OS_NOT_SUPPORTED &&
			identifier == 9);
		CHECK(hx_os_cpu_features_query(&features, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			features == 9);
		CHECK(!hx_os_cpu_features());
	}

	if (hx_os_capabilities() & HX_OS_CAP_ENV) {
		CHECK(hx_os_env_set("HX_SYSTEM_SNAPSHOT", "before 中文", 1,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_env_enumerate(NULL, visit, &found, NULL) ==
				HX_OS_OK &&
			found == 1);
		CHECK(hx_os_env_get_string("HX_SYSTEM_SNAPSHOT", &text, NULL) ==
				HX_OS_OK &&
			!strcmp(hx_string_c_str(&text), "after"));

		found = 0;
		CHECK(hx_os_env_enumerate(NULL, stop_visit, &found, NULL) ==
				HX_OS_CANCELLED &&
			found == 1);
		CHECK(hx_os_env_remove("HX_SYSTEM_SNAPSHOT", NULL) == HX_OS_OK);
	}
	else
		CHECK(hx_os_env_enumerate(NULL, visit, &found, NULL) ==
			HX_OS_NOT_SUPPORTED);
#if !defined(_WIN32)
	{
		struct timespec converted, now;

		CHECK(hx_os_posix_duration_to_timespec(UINT64_C(1234567890),
			      &converted, NULL) == HX_OS_OK);
		CHECK(converted.tv_sec == 1 && converted.tv_nsec == 234567890);
		CHECK(hx_os_posix_deadline_to_timespec(HX_OS_DEADLINE_INFINITE,
			      CLOCK_MONOTONIC, &converted,
			      NULL) == HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_posix_deadline_to_timespec(
			      hx_os_deadline_after_ms(1000), CLOCK_MONOTONIC,
			      &converted, NULL) == HX_OS_OK);
		CHECK(!clock_gettime(CLOCK_MONOTONIC, &now));
		CHECK(converted.tv_sec >= now.tv_sec &&
			converted.tv_sec - now.tv_sec <= 2);
	}
#else
	CHECK(hx_os_posix_duration_to_timespec(0, NULL, NULL) ==
		HX_OS_NOT_SUPPORTED);
#endif
	hx_os_string_list_destroy(&arguments);
	CHECK(!arguments);

	hx_string_destroy(&text);

	return 0;
}
