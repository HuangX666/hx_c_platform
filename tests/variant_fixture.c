#include "variant_fixture.h"

HX_VARIANT_DEFINE_TRIVIAL_TYPE(test_point_type, test_point);

const hx_variant_type *test_variant_point_type(void)
{
	return &test_point_type;
}
