#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <locale.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

static void check_time(int64_t ms, const char *expected)
{
	hx_string text = HX_STRING_INIT;

	CHECK(hx_os_wall_time_format_utc_string(ms, NULL, &text, NULL) ==
		HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), expected));

	hx_string_destroy(&text);
}

static void test_more_fields(void)
{
	hx_string text = HX_STRING_INIT;
	const char *pattern = "%Y %y %YY %m %d %H %I %M %S %p %a %A "
			      "%b %B %j %u %w %L %s %z %:z %Z";

	CHECK(hx_os_wall_time_format_utc_string(0, pattern, &text, NULL) ==
		HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text),
		"1970 70 70 01 01 00 12 00 00 AM Thu Thursday "
		"Jan January 001 4 4 000 0 +0000 +00:00 UTC"));
	CHECK(hx_os_wall_time_format_utc_string(
		      -1, "%s.%L %j %a", &text, NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "-1.999 365 Wed"));
	CHECK(hx_os_wall_time_format_utc_string(INT64_C(951825784005),
		      "%H %I %p %j %u %w %L", &text, NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "12 12 PM 060 2 2 005"));
	CHECK(hx_os_wall_time_format_utc_string(-INT64_C(62135596800000),
		      "%Y %j %u", &text, NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "0001 001 1"));
	hx_string_destroy(&text);
}

static void test_local(const char *expected_minutes)
{
	hx_string text = HX_STRING_INIT;
	char local[80], expected[80];
	size_t required;
	int seconds = 0, dst = 0;
	hx_os_status status;

	CHECK(hx_os_wall_time_local_offset_at(0, &seconds, &dst, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_wall_time_format_local(0, "%Y-%m-%d %H:%M:%S", local,
		      sizeof(local), &required, NULL) == HX_OS_OK);
	CHECK(hx_os_wall_time_format_utc((int64_t)seconds * 1000,
		      "%Y-%m-%d %H:%M:%S", expected, sizeof(expected),
		      &required, NULL) == HX_OS_OK);
	CHECK(!strcmp(local, expected));
	if (expected_minutes) {
		const char *expected_local = seconds == 0
			? "1970-01-01T00:00:00+00:00"
			: seconds == 32400  ? "1970-01-01T09:00:00+09:00"
			: seconds == -18000 ? "1969-12-31T19:00:00-05:00"
					    : "1970-01-01T05:45:00+05:45";

		CHECK(seconds == atoi(expected_minutes) * 60 && dst == 0);
		CHECK(hx_os_wall_time_format_local_string(
			      0, NULL, &text, NULL) == HX_OS_OK);
		CHECK(!strcmp(hx_string_c_str(&text), expected_local));
	}
	CHECK(hx_os_wall_time_format_local_string(
		      0, "%CCYY/%MM/%DD_%hh", &text, NULL) == HX_OS_OK);
	CHECK(hx_os_filename_format_local_string(
		      0, "%CCYY/%MM/%DD_%hh", "txt", &text, NULL) == HX_OS_OK);
	CHECK(!strchr(hx_string_c_str(&text), '/'));
	status = hx_os_wall_time_local_offset_at(-1, &seconds, &dst, NULL);
#if defined(_WIN32)
	CHECK(status == HX_OS_RANGE);
#else
	CHECK(status == HX_OS_OK);
#endif
	hx_string_destroy(&text);
}

int main(int argc, char **argv)
{
	char text[80] = "unchanged";
	size_t required = 999;
	hx_string output = HX_STRING_INIT;
	int offset, dst, year, month, day;
	int64_t ms;

	/* Only the test executable selects a locale, never the library. */
	if (argc >= 3)
		CHECK(setlocale(LC_ALL, argv[2]) != NULL);

	if (!(hx_os_capabilities() & HX_OS_CAP_CALENDAR)) {
		CHECK(hx_os_wall_time_format_utc(0, NULL, NULL, 0, &required,
			      NULL) == HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_wall_time_local_offset_minutes(
			      &offset, &dst, NULL) == HX_OS_NOT_SUPPORTED);

		return 0;
	}

	test_local(argc >= 2 ? argv[1] : NULL);
	CHECK(hx_os_wall_time_local_offset_minutes(&offset, &dst, NULL) ==
		HX_OS_OK);
	CHECK(offset >= -1440 && offset <= 1440 && dst >= -1 && dst <= 1);
	if (argc >= 2)
		CHECK(offset == atoi(argv[1]) && dst == 0);
	/* Offset-only runs leave the exhaustive UTC walk to the main test. */
	if (argc == 2)
		return 0;

	test_more_fields();

	check_time(0, "1970-01-01T00:00:00Z");
	check_time(-1, "1969-12-31T23:59:59Z");
	check_time(-1000, "1969-12-31T23:59:59Z");
	check_time(-INT64_C(62135596800000), "0001-01-01T00:00:00Z");
	check_time(INT64_C(253402300799999), "9999-12-31T23:59:59Z");
	check_time(INT64_C(951782400000), "2000-02-29T00:00:00Z");
	check_time(-INT64_C(2203891200000), "1900-03-01T00:00:00Z");
	CHECK(hx_os_wall_time_format_utc(0, "%Q", text, sizeof(text), &required,
		      NULL) == HX_OS_SYNTAX &&
		!strcmp(text, "unchanged"));
	CHECK(hx_os_wall_time_format_utc(INT64_MAX, NULL, text, sizeof(text),
		      &required, NULL) == HX_OS_RANGE &&
		!strcmp(text, "unchanged"));
	CHECK(hx_os_wall_time_format_utc(0, NULL, text, 1, &required, NULL) ==
			HX_OS_OUTPUT_FULL &&
		required == 21 && !strcmp(text, "unchanged"));
	CHECK(hx_os_wall_time_format_utc(0, "%% %CCYY 年😀", text, sizeof(text),
		      &required, NULL) == HX_OS_OK &&
		!strcmp(text, "% 1970 年😀"));
	CHECK(hx_string_assign_cstr(&output, "%CCYY/%MM/%DD", NULL) ==
		HX_OS_OK);
	CHECK(hx_os_wall_time_format_utc_string(
		      0, hx_string_c_str(&output), &output, NULL) == HX_OS_OK &&
		!strcmp(hx_string_c_str(&output), "1970/01/01"));
	CHECK(hx_os_wall_time_format_utc_string(0, "%", &output, NULL) ==
			HX_OS_SYNTAX &&
		!strcmp(hx_string_c_str(&output), "1970/01/01"));

	hx_string_destroy(&output);

	/* Walk every civil day across two leap-year century boundaries using
	 * an independent calendar progression, not the implementation search.
	 */
	ms = -INT64_C(2208988800000);

	for (year = 1900; year < 2102; ++year)
		for (month = 1; month <= 12; ++month) {
			static const int lengths[] = {
				31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

			int days = lengths[month - 1];

			if (month == 2 && year % 4 == 0 &&
				(year % 100 != 0 || year % 400 == 0))
				++days;
			for (day = 1; day <= days; ++day) {
				char expected[32];

				(void)snprintf(expected, sizeof(expected),
					"%04d-%02d-%02dT00:00:00Z", year, month,
					day);
				CHECK(hx_os_wall_time_format_utc(ms, NULL, text,
					      sizeof(text), &required,
					      NULL) == HX_OS_OK);
				CHECK(!strcmp(text, expected));

				ms += INT64_C(86400000);
			}
		}

	return 0;
}
