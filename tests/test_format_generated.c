#include <hx_format_syntax.h>
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

typedef struct user_info {
	const char *name;
	int age;
} user_info;

static hx_os_status test_user(const void *object, hx_string_view spec,
	hx_string *out, size_t limit, hx_os_error *error)
{
	const user_info *user = object;
	hx_format_arg args[] = {hx_fmt_str(user->name), hx_fmt_i64(user->age)};
	hx_format_options options = HX_FORMAT_OPTIONS_INIT;
	(void)spec;
	options.max_output_bytes = limit;
	return hx_format_with_options(out, hx_string_view_from_cstr("{} ({})"),
		args, 2, &options, error);
}
HX_FORMATTER(user_info, test_user);

int main(void)
{
	user_info user = {"张三", 18};
	hx_string text = HX_STRING_INIT;
	double a = 1.25, b = 2.5;
	HX_TEST_FORMAT_NUMBER number = 1.5;
	int width = 6, counter = 0;
	if (!(hx_os_capabilities() & HX_OS_CAP_FORMAT))
		return 0;
	CHECK(HX_FFORMAT(&text, "姓名={user.name} 合计={a + b:.2f}") ==
		HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "姓名=张三 合计=3.75"));
	CHECK(HX_FORMAT(&text, "{} {}", ++counter, ++counter) == HX_OS_OK);
	CHECK(counter == 2 && !strcmp(hx_string_c_str(&text), "1 2"));
	CHECK(HX_FORMAT(&text, "{:.1f}", number) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "1.5"));
#if defined(_DEBUG)
	CHECK(HX_FORMAT(&text, "Debug {}", 42) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "Debug 42"));
#endif
	CHECK(HX_FORMAT(&text, "{person}", HX_NAMED("person", user)) ==
		HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "张三 (18)"));
	CHECK(HX_FFORMAT(&text, "{user.name:>{width}}") == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "    张三"));
	CHECK(HX_PRINTLN("generated: {} {:.2f}", user.name, a + b) == HX_OS_OK);
	hx_string_destroy(&text);
	return 0;
}
