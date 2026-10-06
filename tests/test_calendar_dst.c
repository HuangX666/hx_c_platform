#include "utility_test.h"

static void check(int64_t ms, const char *expected, int seconds, int dst)
{
	hx_string text = HX_STRING_INIT;
	int offset = 123, daylight = 123;

	CHECK(hx_os_wall_time_format_local_string(ms, NULL, &text, NULL) ==
		HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), expected));
	CHECK(hx_os_wall_time_local_offset_at(ms, &offset, &daylight, NULL) ==
		HX_OS_OK);
	CHECK(offset == seconds && daylight == dst);
	hx_string_destroy(&text);
}

int main(int argc, char **argv)
{
	if (!(hx_os_capabilities() & HX_OS_CAP_CALENDAR))
		return 0;

	if (argc == 2 && !strcmp(argv[1], "seconds")) {
		hx_string text = HX_STRING_INIT;
		int minutes, dst;

		check(0, "1970-01-01T00:00:30+00:00:30", 30, 0);
		CHECK(hx_os_wall_time_format_local_string(
			      0, "%z %Z", &text, NULL) == HX_OS_OK);
		CHECK(!strcmp(hx_string_c_str(&text), "+000030 UTC+00:00:30"));
		CHECK(hx_os_wall_time_local_offset_minutes(
			      &minutes, &dst, NULL) == HX_OS_NOT_SUPPORTED);
		hx_string_destroy(&text);
		return 0;
	}

	check(INT64_C(1710053999000), "2024-03-10T01:59:59-05:00", -18000, 0);
	check(INT64_C(1710054000000), "2024-03-10T03:00:00-04:00", -14400, 1);
	check(INT64_C(1730613599000), "2024-11-03T01:59:59-04:00", -14400, 1);
	check(INT64_C(1730613600000), "2024-11-03T01:00:00-05:00", -18000, 0);
	puts("local daylight transitions passed");

	return 0;
}
