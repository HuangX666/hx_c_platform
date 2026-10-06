#include "utility_test.h"
#include <signal.h>

static volatile sig_atomic_t caught;

static void catch_trap(int number)
{
	if (number == SIGTRAP)
		caught = 1;
}

int main(void)
{
	void (*previous)(int) = signal(SIGTRAP, catch_trap);

	CHECK(previous != SIG_ERR);
	hx_os_debug_break();
	CHECK(caught);
	CHECK(signal(SIGTRAP, previous) != SIG_ERR);
	puts("explicit breakpoint reached the application handler");

	return 0;
}
