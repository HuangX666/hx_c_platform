#include "hx_os.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "atomic check at %d: %s\n", __LINE__,  \
				#value);                                       \
			exit(1);                                               \
		}                                                              \
	} while (0)

typedef struct atomic_state {
	hx_os_atomic counter;
	hx_os_atomic32 small;
	hx_os_atomic_ptr published;
	int payload;
} atomic_state;

static void orders_test(void)
{
	hx_os_atomic wide = HX_OS_ATOMIC_INIT(0);
	hx_os_atomic32 small = HX_OS_ATOMIC32_INIT(0);
	hx_os_atomic_ptr pointer = HX_OS_ATOMIC_PTR_INIT(NULL);

	int object, replacement, s, f;

	for (s = -1; s <= HX_OS_SEQ_CST + 1; ++s) {
		hx_os_memory_order order = (hx_os_memory_order)s;

		hx_os_atomic_store(&wide, INT64_MAX, order);
		CHECK(hx_os_atomic_fetch_add(&wide, 1, order) == INT64_MAX);
		CHECK(hx_os_atomic_load(&wide, order) == INT64_MIN);
		CHECK(hx_os_atomic_fetch_sub(&wide, INT64_MIN, order) ==
			INT64_MIN);
		CHECK(hx_os_atomic_fetch_or(&wide, 15, order) == 0);
		CHECK(hx_os_atomic_fetch_and(&wide, 6, order) == 15);
		CHECK(hx_os_atomic_fetch_xor(&wide, 3, order) == 6);
		CHECK(hx_os_atomic_exchange(&wide, 0, order) == 5);

		hx_os_atomic32_store(&small, INT32_MAX, order);
		CHECK(hx_os_atomic32_fetch_add(&small, 1, order) == INT32_MAX);
		CHECK(hx_os_atomic32_load(&small, order) == INT32_MIN);
		CHECK(hx_os_atomic32_fetch_sub(&small, INT32_MIN, order) ==
			INT32_MIN);
		CHECK(hx_os_atomic32_fetch_or(&small, 15, order) == 0);
		CHECK(hx_os_atomic32_fetch_and(&small, 6, order) == 15);
		CHECK(hx_os_atomic32_fetch_xor(&small, 3, order) == 6);
		CHECK(hx_os_atomic32_exchange(&small, 0, order) == 5);

		hx_os_atomic_ptr_store(&pointer, &object, order);
		CHECK(hx_os_atomic_ptr_exchange(
			      &pointer, &replacement, order) == &object);
		CHECK(hx_os_atomic_ptr_load(&pointer, order) == &replacement);

		hx_os_atomic_fence(order);

		for (f = -1; f <= HX_OS_SEQ_CST + 1; ++f) {
			hx_os_memory_order failure = (hx_os_memory_order)f;
			int64_t expected = 0;
			int32_t expected32 = 0;
			void *expected_ptr = NULL;

			hx_os_atomic_store(&wide, 9, HX_OS_RELAXED);
			hx_os_atomic32_store(&small, 9, HX_OS_RELAXED);
			hx_os_atomic_ptr_store(
				&pointer, &object, HX_OS_RELAXED);
			CHECK(!hx_os_atomic_compare_exchange(
				&wide, &expected, 17, order, failure));
			CHECK(expected == 9);
			CHECK(hx_os_atomic_compare_exchange(
				&wide, &expected, 17, order, failure));
			CHECK(expected == 9);
			CHECK(!hx_os_atomic32_compare_exchange(
				&small, &expected32, 17, order, failure));
			CHECK(expected32 == 9);
			CHECK(hx_os_atomic32_compare_exchange(
				&small, &expected32, 17, order, failure));
			CHECK(!hx_os_atomic_ptr_compare_exchange(&pointer,
				&expected_ptr, &replacement, order, failure));
			CHECK(expected_ptr == &object);
			CHECK(hx_os_atomic_ptr_compare_exchange(&pointer,
				&expected_ptr, &replacement, order, failure));

			expected = 17;
			expected32 = 17;
			expected_ptr = &replacement;

			while (!hx_os_atomic_compare_exchange_weak(
				&wide, &expected, 19, order, failure))
				CHECK(expected == 17);

			while (!hx_os_atomic32_compare_exchange_weak(
				&small, &expected32, 19, order, failure))
				CHECK(expected32 == 17);

			while (!hx_os_atomic_ptr_compare_exchange_weak(
				&pointer, &expected_ptr, NULL, order, failure))
				CHECK(expected_ptr == &replacement);
			CHECK(hx_os_atomic_load(&wide, HX_OS_RELAXED) == 19);
			CHECK(hx_os_atomic32_load(&small, HX_OS_RELAXED) == 19);
			CHECK(!hx_os_atomic_ptr_load(&pointer, HX_OS_RELAXED));
		}
	}
}

static void *count_worker(void *user)
{
	atomic_state *state = user;
	int i;

	for (i = 0; i < 20000; ++i) {
		int64_t previous =
			hx_os_atomic_load(&state->counter, HX_OS_RELAXED);

		while (!hx_os_atomic_compare_exchange_weak(&state->counter,
			&previous, previous + 1, HX_OS_RELAXED, HX_OS_RELAXED))
			hx_os_cpu_relax();
		(void)hx_os_atomic32_fetch_add(&state->small, 1, HX_OS_RELAXED);
	}

	return NULL;
}

static void *publish_worker(void *user)
{
	atomic_state *state = user;
	int i;

	for (i = 1; i <= 10000; ++i) {
		while (hx_os_atomic_ptr_load(&state->published, HX_OS_ACQUIRE))
			hx_os_cpu_relax();

		state->payload = i;
		CHECK(!hx_os_atomic_ptr_exchange(
			&state->published, &state->payload, HX_OS_RELEASE));
	}

	return NULL;
}

int main(void)
{
	atomic_state state = {HX_OS_ATOMIC_INIT(0), HX_OS_ATOMIC32_INIT(0),
		HX_OS_ATOMIC_PTR_INIT(NULL), 0};

	hx_os_thread *threads[4] = {NULL};
	size_t i;

	CHECK(sizeof(hx_os_atomic) == 8);
	CHECK(sizeof(hx_os_atomic32) == 4);
	CHECK((uintptr_t)&state.counter % 8 == 0);
	CHECK((uintptr_t)&state.small % 4 == 0);
	CHECK(hx_os_atomic_is_lock_free() == 0 ||
		hx_os_atomic_is_lock_free() == 1);
	CHECK(hx_os_atomic32_is_lock_free() == 0 ||
		hx_os_atomic32_is_lock_free() == 1);
	CHECK(hx_os_atomic_ptr_is_lock_free() == 0 ||
		hx_os_atomic_ptr_is_lock_free() == 1);

	orders_test();

	for (i = 0; i < 4; ++i)
		CHECK(hx_os_thread_create(NULL, count_worker, &state,
			      &threads[i], NULL) == HX_OS_OK);

	for (i = 0; i < 4; ++i) {
		CHECK(hx_os_thread_join_until(threads[i],
			      (hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN)
				      ? hx_os_deadline_after_ms(10000)
				      : HX_OS_DEADLINE_INFINITE,
			      NULL, NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&threads[i], NULL) == HX_OS_OK);
	}

	CHECK(hx_os_atomic_load(&state.counter, HX_OS_RELAXED) == 80000);
	CHECK(hx_os_atomic32_load(&state.small, HX_OS_RELAXED) == 80000);
	CHECK(hx_os_thread_create(NULL, publish_worker, &state, &threads[0],
		      NULL) == HX_OS_OK);

	for (i = 1; i <= 10000; ++i) {
		void *observed = NULL;

		/* A failed acquire CAS must acquire the published payload. */
		while (hx_os_atomic_ptr_compare_exchange(&state.published,
			&observed, NULL, HX_OS_ACQUIRE, HX_OS_ACQUIRE))
			hx_os_cpu_relax();
		CHECK(observed == &state.payload);
		CHECK(*(int *)observed == (int)i);

		hx_os_atomic_ptr_store(&state.published, NULL, HX_OS_RELEASE);
	}

	CHECK(hx_os_thread_join_until(threads[0],
		      (hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN)
			      ? hx_os_deadline_after_ms(10000)
			      : HX_OS_DEADLINE_INFINITE,
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&threads[0], NULL) == HX_OS_OK);

	return 0;
}
