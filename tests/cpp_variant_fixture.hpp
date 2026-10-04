#ifndef HX_TEST_CPP_VARIANT_FIXTURE_HPP
#define HX_TEST_CPP_VARIANT_FIXTURE_HPP

#include <hx/Variant.hpp>

struct VariantPoint {
	double x;
	double y;
};

hx::Result<void> SetPointFromOtherFile(hx::Variant &value) noexcept;
#endif
