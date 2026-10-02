#include "cpp_variant_fixture.hpp"
hx::Result<void> SetPointFromOtherFile(hx::Variant &value) noexcept
{
	return value.set(VariantPoint{2.5, 4});
}
