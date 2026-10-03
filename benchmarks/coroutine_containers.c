#include <hx_os.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif

static uint64_t bench_now(void)
{
#if defined(_WIN32)
	LARGE_INTEGER ticks, frequency;
	if (!QueryPerformanceFrequency(&frequency) ||
		!QueryPerformanceCounter(&ticks) || frequency.QuadPart <= 0)
		exit(1);
	return (uint64_t)ticks.QuadPart / (uint64_t)frequency.QuadPart *
		UINT64_C(1000000000) +
		(uint64_t)((double)((uint64_t)ticks.QuadPart %
				   (uint64_t)frequency.QuadPart) *
			1000000000.0 / (double)frequency.QuadPart);
#else
	uint64_t value;
	if (hx_os_monotonic_read(&value, NULL) != HX_OS_OK)
		exit(1);
	return value;
#endif
}
static int bench_compare(
	const void *a, size_t a_size, const void *b, size_t b_size, void *user)
{
	uint32_t left, right;
	memcpy(&left, a, sizeof(left));
	memcpy(&right, b, sizeof(right));
	(void)a_size;
	(void)b_size;
	(void)user;
	return (left > right) - (left < right);
}
static void bench_tree(int kind, int random)
{
	hx_os_container_options options;
	hx_os_map *map;
	uint64_t start, inserted, found, erased;
	uint32_t i;
	size_t memory;
	hx_os_container_options_init(&options, 0);
	if (hx_os_map_create(NULL, kind, &options, bench_compare, &map, NULL) !=
		HX_OS_OK)
		exit(1);
	start = bench_now();
	for (i = 0; i < 20000; ++i) {
		uint32_t key = random ? (i * 7919) % 20000 : i;
		if (hx_os_map_insert(map, &key, sizeof(key), NULL, 0, NULL) !=
			HX_OS_OK)
			exit(1);
	}
	inserted = bench_now();
	memory = hx_os_map_memory(map);
	for (i = 0; i < 20000; ++i) {
		const void *value;
		if (hx_os_map_find(map, &i, sizeof(i), &value) != HX_OS_OK)
			exit(1);
	}
	found = bench_now();
	for (i = 0; i < 20000; ++i)
		if (hx_os_map_erase(map, &i, sizeof(i), NULL, NULL) != HX_OS_OK)
			exit(1);
	erased = bench_now();
	printf("tree=%s input=%s count=20000 insert_ns=%.1f find_ns=%.1f "
	       "erase_ns=%.1f bytes=%zu\n",
		kind == HX_OS_MAP_RED_BLACK ? "rb" : "avl",
		random ? "permuted" : "sorted",
		(double)(inserted - start) / 20000,
		(double)(found - inserted) / 20000,
		(double)(erased - found) / 20000, memory);
	(void)hx_os_map_destroy(&map, NULL);
}
typedef struct bench_state {
	size_t count;
	int step;
} bench_state;
static hx_os_status bench_stackful(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	bench_state *state = user;
	(void)self;
	for (state->count = 0; state->count < 100000; ++state->count)
		if (hx_os_coro_yield(error) != HX_OS_OK)
			return error->status;
	return HX_OS_OK;
}
static hx_os_status bench_stackless(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	bench_state *state = user;
	hx_os_status status;
	(void)self;
	HX_OS_CORO_BEGIN(state->step);
	for (state->count = 0; state->count < 100000; ++state->count) {
		HX_OS_CORO_AWAIT(state->step, status, hx_os_coro_yield(error));
		if (status != HX_OS_OK)
			return hx_os_coro_complete(status, error);
	}
	HX_OS_CORO_END(state->step);
	return HX_OS_OK;
}
static void bench_coroutine(int kind)
{
	hx_os_coro_runtime *runtime;
	hx_os_coro *task;
	hx_os_coro_options options = HX_OS_CORO_OPTIONS_INIT;
	hx_os_error result;
	bench_state state = {0, 0};
	uint64_t start, elapsed;
	hx_os_deadline close;
	options.workers = 1;
	if (hx_os_coro_runtime_create(NULL, &options, &runtime, NULL) !=
		HX_OS_OK)
		exit(1);
	start = bench_now();
	if (hx_os_coro_go(runtime, kind,
		    kind == HX_OS_CORO_STACKFUL ? bench_stackful
						: bench_stackless,
		    &state, NULL, &task, NULL) != HX_OS_OK)
		exit(1);
	if (hx_os_coro_wait_until(task, hx_os_deadline_after_ms(20000), &result,
		    NULL) != HX_OS_OK ||
		result.status != HX_OS_OK)
		exit(1);
	elapsed = bench_now() - start;
	printf("coroutine=%s count=%zu yield_round_ns=%.1f "
	       "configured_stack=%zu\n",
		kind == HX_OS_CORO_STACKFUL ? "stackful" : "stackless",
		state.count, (double)elapsed / (double)state.count,
		kind == HX_OS_CORO_STACKFUL ? options.stack_bytes : 0);
	hx_os_coro_release(task);
	close = hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN
		? hx_os_deadline_after_ms(5000)
		: HX_OS_DEADLINE_INFINITE;
	if (hx_os_coro_runtime_drain_until(runtime, close, NULL) != HX_OS_OK ||
		hx_os_coro_runtime_destroy(&runtime, NULL) != HX_OS_OK)
		exit(1);
}
int main(void)
{
	printf("platform=%s cpu=%s pointer_bits=%u\n", hx_os_platform_name(),
		hx_os_cpu_name(), (unsigned)(sizeof(void *) * 8));
	if (hx_os_capabilities() & HX_OS_CAP_CONTAINERS) {
		bench_tree(HX_OS_MAP_ORDERED, 0);
		bench_tree(HX_OS_MAP_RED_BLACK, 0);
		bench_tree(HX_OS_MAP_ORDERED, 1);
		bench_tree(HX_OS_MAP_RED_BLACK, 1);
	}
	if (hx_os_capabilities() & HX_OS_CAP_COROUTINES)
		bench_coroutine(HX_OS_CORO_STACKLESS);
	if (hx_os_capabilities() & HX_OS_CAP_STACKFUL_COROUTINES)
		bench_coroutine(HX_OS_CORO_STACKFUL);
	return 0;
}
