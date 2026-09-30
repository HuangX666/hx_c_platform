#include "internal.h"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if defined(_MSC_VER) && defined(_M_IX86)
static int64_t hx_os_atomic_msvc32_update(
	hx_os_atomic *value, int64_t operand, int add)
{
	volatile __int64 *storage = (volatile __int64 *)&value->value;
	int64_t previous = _InterlockedCompareExchange64(storage, 0, 0);
	for (;;) {
		int64_t desired = add
			? (int64_t)((uint64_t)previous + (uint64_t)operand)
			: operand;
		int64_t observed = _InterlockedCompareExchange64(
			storage, desired, previous);
		if (observed == previous)
			return previous;
		previous = observed;
	}
}
#endif

int64_t hx_os_atomic_load(const hx_os_atomic *value, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;
	return _InterlockedCompareExchange64(
		(volatile __int64 *)&value->value, 0, 0);
#else
	switch (order) {
	case HX_OS_RELAXED:
		return __atomic_load_n(&value->value, __ATOMIC_RELAXED);
	case HX_OS_ACQUIRE:
		return __atomic_load_n(&value->value, __ATOMIC_ACQUIRE);
	default:
		return __atomic_load_n(&value->value, __ATOMIC_SEQ_CST);
	}
#endif
}

void hx_os_atomic_store(
	hx_os_atomic *value, int64_t desired, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;
#if defined(_M_IX86)
	(void)hx_os_atomic_msvc32_update(value, desired, 0);
#else
	(void)_InterlockedExchange64(
		(volatile __int64 *)&value->value, desired);
#endif
#else
	switch (order) {
	case HX_OS_RELAXED:
		__atomic_store_n(&value->value, desired, __ATOMIC_RELAXED);
		break;
	case HX_OS_RELEASE:
		__atomic_store_n(&value->value, desired, __ATOMIC_RELEASE);
		break;
	default:
		__atomic_store_n(&value->value, desired, __ATOMIC_SEQ_CST);
		break;
	}
#endif
}

int64_t hx_os_atomic_fetch_add(
	hx_os_atomic *value, int64_t amount, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;
#if defined(_M_IX86)
	return hx_os_atomic_msvc32_update(value, amount, 1);
#else
	return _InterlockedExchangeAdd64(
		(volatile __int64 *)&value->value, amount);
#endif
#else
	switch (order) {
	case HX_OS_RELAXED:
		return __atomic_fetch_add(
			&value->value, amount, __ATOMIC_RELAXED);
	case HX_OS_ACQUIRE:
		return __atomic_fetch_add(
			&value->value, amount, __ATOMIC_ACQUIRE);
	case HX_OS_RELEASE:
		return __atomic_fetch_add(
			&value->value, amount, __ATOMIC_RELEASE);
	case HX_OS_ACQ_REL:
		return __atomic_fetch_add(
			&value->value, amount, __ATOMIC_ACQ_REL);
	default:
		return __atomic_fetch_add(
			&value->value, amount, __ATOMIC_SEQ_CST);
	}
#endif
}

int hx_os_atomic_compare_exchange(hx_os_atomic *value, int64_t *expected,
	int64_t desired, hx_os_memory_order success, hx_os_memory_order failure)
{
	(void)success;
	(void)failure;
#if defined(_MSC_VER)
	{
		int64_t old = _InterlockedCompareExchange64(
			(volatile __int64 *)&value->value, desired, *expected);
		if (old == *expected)
			return 1;
		*expected = old;
		return 0;
	}
#else
	return __atomic_compare_exchange_n(&value->value, expected, desired, 0,
		__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#endif
}

void *hx_os_atomic_ptr_load(
	const hx_os_atomic_ptr *value, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;
	return _InterlockedCompareExchangePointer(
		(void *volatile *)&value->value, NULL, NULL);
#else
	if (order == HX_OS_RELAXED)
		return __atomic_load_n(&value->value, __ATOMIC_RELAXED);
	if (order == HX_OS_ACQUIRE)
		return __atomic_load_n(&value->value, __ATOMIC_ACQUIRE);
	return __atomic_load_n(&value->value, __ATOMIC_SEQ_CST);
#endif
}

void *hx_os_atomic_ptr_exchange(
	hx_os_atomic_ptr *value, void *desired, hx_os_memory_order order)
{
	(void)order;
#if defined(_MSC_VER)
	return _InterlockedExchangePointer(
		(void *volatile *)&value->value, desired);
#else
	return __atomic_exchange_n(&value->value, desired, __ATOMIC_SEQ_CST);
#endif
}

int hx_os_atomic_is_lock_free(void)
{
#if defined(_MSC_VER)
	return 1;
#else
	return __atomic_always_lock_free(8, 0);
#endif
}

void hx_os_cpu_relax(void)
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
	_mm_pause();
#elif defined(_MSC_VER) && defined(_M_ARM64)
	__yield();
#elif defined(__i386__) || defined(__x86_64__)
	__asm__ __volatile__("pause");
#elif defined(__aarch64__) || (defined(__arm__) && __ARM_ARCH >= 7)
	__asm__ __volatile__("yield");
#endif
}
