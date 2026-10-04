#include <hx/Format.hpp>
#include <cstdio>
#include <cstdlib>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			std::fprintf(                                          \
				stderr, "line %d: %s\n", __LINE__, #value);    \
			std::exit(1);                                          \
		}                                                              \
	} while (0)

struct Point {
	int x;
	int y;
};

template <> struct hx::Formatter<Point> {
	static hx_os_status format(const Point &value, hx::StringView spec,
		hx_string *out, size_t limit, hx_os_error *error) noexcept
	{
		if (!spec.empty())
			return HX_OS_SYNTAX;

		hx_format_options options = HX_FORMAT_OPTIONS_INIT;

		options.max_output_bytes = limit;

		hx_format_arg args[] = {
			hx_fmt_i64(value.x), hx_fmt_i64(value.y)};

		return hx_format_with_options(out,
			hx_string_view_from_cstr("({}, {})"), args, 2, &options,
			error);
	}
};

int main()
{
	if (!(hx_os_capabilities() & HX_OS_CAP_FORMAT))
		return 0;

	auto text = hx::format(
		"{} {} {:.2f} {} {}", "中文", 42, 1.25, true, Point{10, 20});
	CHECK(text && text.value() == "中文 42 1.25 true (10, 20)");

	auto named = hx::format("{name} {value:04x}",
		hx::named("name", std::string("demo")), hx::named("value", 15));
	CHECK(named && named.value() == "demo 000f");
	CHECK(hx::format("no arguments").value() == "no arguments");

	enum class Number : unsigned { value = 42 };
	int number = 0;
	volatile int *pointer = &number;

	CHECK(hx::format("{}", Number::value).value() == "42");
	CHECK(hx::format("{:p}", pointer));
	CHECK(hx::format("{:d}", "bad").status() == HX_OS_TYPE_MISMATCH);
	CHECK(hx::println("C++11: {}", 42));

	return 0;
}
