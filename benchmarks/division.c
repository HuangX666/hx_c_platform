#include <hx_os.h>
#include <stdio.h>
#include <inttypes.h>
static volatile uint32_t divisor = 123456789;
static volatile uint64_t sink;
int main(void)
{
	uint64_t input = UINT64_MAX, quotient, started, helper, native, i;
	uint32_t remainder;
	started = hx_os_monotonic_ns();
	for (i = 0; i < 1000000; ++i) {
		(void)hx_os_udiv64(input - i, divisor, &quotient, &remainder);
		sink = quotient + remainder;
	}
	helper = hx_os_monotonic_ns() - started;
	started = hx_os_monotonic_ns();
	for (i = 0; i < 1000000; ++i) {
		uint32_t by = divisor;
		sink = (input - i) / by + (input - i) % by;
	}
	native = hx_os_monotonic_ns() - started;
	printf("udiv64_ns=%" PRIu64 " native_ns=%" PRIu64 " sink=%" PRIu64 "\n",
		helper, native, sink);
	return 0;
}
