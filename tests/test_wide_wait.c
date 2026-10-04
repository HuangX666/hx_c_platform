#include "utility_test.h"
#include "wide_vectors.h"

static void test_wide(void)
{
	hx_os_uint128 product = {7, 9}, quotient;
	uint64_t remainder = 55, result = 42;
	hx_os_error error;
	size_t i;

	if (!(hx_os_capabilities() & HX_OS_CAP_WIDE_MATH)) {
		CHECK(hx_os_u128_multiply(1, 2, &product) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(product.low == 7 && product.high == 9);

		return;
	}

	for (i = 0; i < sizeof(wide_cases) / sizeof(wide_cases[0]); ++i) {
		const struct wide_case *c = &wide_cases[i];

		CHECK(hx_os_u128_multiply(c->a, c->b, &product) == HX_OS_OK);
		CHECK(product.low == c->low && product.high == c->high);
		CHECK(hx_os_u128_divide(product, c->divisor, &quotient,
			      &remainder) == HX_OS_OK);
		CHECK(quotient.low == c->qlow && quotient.high == c->qhigh);
		CHECK(remainder == c->remainder);
		if (!c->qhigh) {
			CHECK(hx_os_muldiv_u64(c->a, c->b, c->divisor,
				      HX_OS_ROUND_DOWN, &result,
				      &error) == HX_OS_OK);
			CHECK(result == c->qlow && error.status == HX_OS_OK);
		}
	}

	CHECK(hx_os_muldiv_u64(1001, 1000000000, 30000, HX_OS_ROUND_DOWN,
		      &result, NULL) == HX_OS_OK &&
		result == 33366666);
	CHECK(hx_os_muldiv_u64(1001, 1000000000, 30000, HX_OS_ROUND_NEAREST,
		      &result, NULL) == HX_OS_OK &&
		result == 33366667);
	CHECK(hx_os_muldiv_u64(3, 1, 2, HX_OS_ROUND_NEAREST, &result, NULL) ==
			HX_OS_OK &&
		result == 2);
	CHECK(hx_os_muldiv_u64(1, 1, 3, HX_OS_ROUND_UP, &result, NULL) ==
			HX_OS_OK &&
		result == 1);

	result = 42;
	CHECK(hx_os_muldiv_u64(UINT64_MAX, UINT64_MAX, 1, HX_OS_ROUND_DOWN,
		      &result, &error) == HX_OS_RANGE &&
		result == 42);
	CHECK(error.status == HX_OS_RANGE);
	CHECK(hx_os_muldiv_u64(1, 1, 0, HX_OS_ROUND_DOWN, &result, NULL) ==
			HX_OS_INVALID &&
		result == 42);

	quotient.low = 7;
	CHECK(hx_os_u128_divide(product, 0, &quotient, &remainder) ==
			HX_OS_INVALID &&
		quotient.low == 7);
}

static hx_os_status HX_OS_CALL tick(void *user, hx_os_error *error)
{
	unsigned *count = user;

	CHECK(error && error->status == HX_OS_OK);

	++*count;

	return *count == 3 ? HX_OS_EOF : HX_OS_OK;
}

static hx_os_status HX_OS_CALL fail_tick(void *user, hx_os_error *error)
{
	(void)user;
	error->status = HX_OS_ACCESS_DENIED;
	error->domain = HX_OS_ERROR_ERRNO;
	error->native_code = 123;

	return HX_OS_ACCESS_DENIED;
}

static void test_wait(void)
{
	unsigned count = 0;
	size_t completed = 99;
	hx_os_deadline due;
	uint64_t after;
	hx_os_error error;

	if (hx_os_capabilities() & HX_OS_CAP_PRECISE_WAIT) {
		CHECK(hx_os_sleep_until_precise(HX_OS_DEADLINE_NOW, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_sleep_until_precise(
			      HX_OS_DEADLINE_INFINITE, NULL) == HX_OS_INVALID);
		CHECK(hx_os_sleep_until_precise_ex(HX_OS_DEADLINE_NOW, 10000001,
			      NULL) == HX_OS_INVALID);

		due = hx_os_deadline_after_ms(3);
		CHECK(hx_os_sleep_until_precise_ex(due, 100000, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_monotonic_read(&after, NULL) == HX_OS_OK &&
			after >= due);
		CHECK(hx_os_sleep_until_precise_ex(
			      HX_OS_DEADLINE_NOW, 0, NULL) == HX_OS_OK);

		due = hx_os_monotonic_ns() + 100000;
		CHECK(hx_os_sleep_until_precise_ex(due, 200000, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_monotonic_ns() >= due);
	}
	else {
		CHECK(hx_os_sleep_until_precise(HX_OS_DEADLINE_NOW, NULL) ==
			HX_OS_NOT_SUPPORTED);
	}

	CHECK(hx_os_repeat_until(1, 10, tick, &count, HX_OS_DEADLINE_NOW,
		      &completed, NULL) == HX_OS_TIMEOUT &&
		!count && !completed);
	CHECK(hx_os_repeat(1, 10, tick, &count, NULL) == HX_OS_OK &&
		count == 3);
	CHECK(hx_os_repeat_until(1, 1, fail_tick, NULL, HX_OS_DEADLINE_INFINITE,
		      &completed, &error) == HX_OS_ACCESS_DENIED);
	CHECK(completed == 1 && error.native_code == 123);
}

int main(void)
{
	test_wide();
	test_wait();

	return 0;
}
