#ifndef HX_TEST_VARIANT_FIXTURE_H
#define HX_TEST_VARIANT_FIXTURE_H

#include <hx_variant.h>

typedef struct test_point {
	double x;
	double y;
} test_point;

extern const hx_variant_type test_point_type;

const hx_variant_type *test_variant_point_type(void);
#endif
