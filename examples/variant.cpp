#include <hx/Variant.hpp>
#include <cstdio>

struct Point {
	double x;
	double y;
};

int main()
{
	if (!(hx_os_capabilities() & HX_OS_CAP_VARIANT)) {
		std::puts("Variant support is disabled");
		return 0;
	}
	hx::TypedVariant<int32_t, double, hx::StringView, Point> value;
	if (!value.set(int32_t(42)))
		return 1;
	auto count = value.get<int32_t>();
	if (!count)
		return 1;
	std::printf("Integer: %d\n", static_cast<int>(count.value()));

	if (!value.set("hello"))
		return 1;
	auto text = value.get<hx::StringView>();
	if (!text)
		return 1;
	std::printf("String: %.*s\n", static_cast<int>(text.value().size()),
		text.value().data());

	if (!value.set(Point{2, 5}))
		return 1;
	auto position = value.get<Point>();
	if (!position)
		return 1;
	std::printf(
		"Point: %.1f, %.1f\n", position.value().x, position.value().y);
}
