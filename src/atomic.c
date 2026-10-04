#include "internal.h"

#if defined(_MSC_VER)
#include <intrin.h>
#else
/* Memory-order arguments must be constants: runtime GCC orders may silently
 * become seq_cst. Keep the dispatch shared across integer and pointer types.
 */
#define HX_OS_ATOMIC_RMW(builtin)                                              \
	switch (order) {                                                       \
	case HX_OS_RELAXED:                                                    \
		return builtin(&value->value, operand, __ATOMIC_RELAXED);      \
	case HX_OS_ACQUIRE:                                                    \
		return builtin(&value->value, operand, __ATOMIC_ACQUIRE);      \
	case HX_OS_RELEASE:                                                    \
		return builtin(&value->value, operand, __ATOMIC_RELEASE);      \
	case HX_OS_ACQ_REL:                                                    \
		return builtin(&value->value, operand, __ATOMIC_ACQ_REL);      \
	default:                                                               \
		return builtin(&value->value, operand, __ATOMIC_SEQ_CST);      \
	}

static int hx_os_atomic_orders_valid(
	hx_os_memory_order success, hx_os_memory_order failure)
{
	switch (success) {
	case HX_OS_RELAXED:
	case HX_OS_RELEASE:
		return failure == HX_OS_RELAXED;
	case HX_OS_ACQUIRE:
	case HX_OS_ACQ_REL:
		return failure == HX_OS_RELAXED || failure == HX_OS_ACQUIRE;
	case HX_OS_SEQ_CST:
		return failure == HX_OS_RELAXED || failure == HX_OS_ACQUIRE ||
			failure == HX_OS_SEQ_CST;
	default:
		return 0;
	}
}
#define HX_OS_ATOMIC_CAS_CALL(s, f)                                            \
	__atomic_compare_exchange_n(                                           \
		&value->value, expected, desired, weak, s, f)
#define HX_OS_ATOMIC_CAS()                                                     \
	do {                                                                   \
		if (!hx_os_atomic_orders_valid(success, failure)) {            \
			success = HX_OS_SEQ_CST;                               \
			failure = HX_OS_SEQ_CST;                               \
		}                                                              \
		switch (success) {                                             \
		case HX_OS_RELAXED:                                            \
			return HX_OS_ATOMIC_CAS_CALL(                          \
				__ATOMIC_RELAXED, __ATOMIC_RELAXED);           \
		case HX_OS_RELEASE:                                            \
			return HX_OS_ATOMIC_CAS_CALL(                          \
				__ATOMIC_RELEASE, __ATOMIC_RELAXED);           \
		case HX_OS_ACQUIRE:                                            \
			if (failure == HX_OS_ACQUIRE)                          \
				return HX_OS_ATOMIC_CAS_CALL(                  \
					__ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);   \
			return HX_OS_ATOMIC_CAS_CALL(                          \
				__ATOMIC_ACQUIRE, __ATOMIC_RELAXED);           \
		case HX_OS_ACQ_REL:                                            \
			if (failure == HX_OS_ACQUIRE)                          \
				return HX_OS_ATOMIC_CAS_CALL(                  \
					__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);   \
			return HX_OS_ATOMIC_CAS_CALL(                          \
				__ATOMIC_ACQ_REL, __ATOMIC_RELAXED);           \
		default:                                                       \
			if (failure == HX_OS_ACQUIRE)                          \
				return HX_OS_ATOMIC_CAS_CALL(                  \
					__ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE);   \
			if (failure == HX_OS_RELAXED)                          \
				return HX_OS_ATOMIC_CAS_CALL(                  \
					__ATOMIC_SEQ_CST, __ATOMIC_RELAXED);   \
			return HX_OS_ATOMIC_CAS_CALL(                          \
				__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);           \
		}                                                              \
	} while (0)
#endif

#if defined(_MSC_VER) && defined(_M_IX86)
enum {
	HX_OS_ATOMIC_ADD,
	HX_OS_ATOMIC_OR,
	HX_OS_ATOMIC_AND,
	HX_OS_ATOMIC_XOR,
	HX_OS_ATOMIC_EXCHANGE
};

static int64_t hx_os_atomic_msvc32_update(
	hx_os_atomic *value, int64_t operand, int operation)
{
	volatile __int64 *storage = (volatile __int64 *)&value->value;

	int64_t previous = _InterlockedCompareExchange64(storage, 0, 0);

	for (;;) {
		int64_t desired, observed;

		switch (operation) {
		case HX_OS_ATOMIC_ADD:
			desired = (int64_t)((uint64_t)previous +
				(uint64_t)operand);
			break;
		case HX_OS_ATOMIC_OR:
			desired = previous | operand;
			break;
		case HX_OS_ATOMIC_AND:
			desired = previous & operand;
			break;
		case HX_OS_ATOMIC_XOR:
			desired = previous ^ operand;
			break;

		default:
			desired = operand;
			break;
		}

		observed = _InterlockedCompareExchange64(
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
	(void)hx_os_atomic_exchange(value, desired, HX_OS_SEQ_CST);
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
	hx_os_atomic *value, int64_t operand, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;
#if defined(_M_IX86)
	return hx_os_atomic_msvc32_update(value, operand, HX_OS_ATOMIC_ADD);
#else
	return _InterlockedExchangeAdd64(
		(volatile __int64 *)&value->value, operand);
#endif
#else
	HX_OS_ATOMIC_RMW(__atomic_fetch_add);
#endif
}

int64_t hx_os_atomic_fetch_sub(
	hx_os_atomic *value, int64_t operand, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;
#if defined(_M_IX86)
	return hx_os_atomic_msvc32_update(value,
		(int64_t)(UINT64_C(0) - (uint64_t)operand), HX_OS_ATOMIC_ADD);
#else
	return _InterlockedExchangeAdd64((volatile __int64 *)&value->value,
		(int64_t)(UINT64_C(0) - (uint64_t)operand));
#endif
#else
	HX_OS_ATOMIC_RMW(__atomic_fetch_sub);
#endif
}

int64_t hx_os_atomic_fetch_or(
	hx_os_atomic *value, int64_t operand, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;
#if defined(_M_IX86)
	return hx_os_atomic_msvc32_update(value, operand, HX_OS_ATOMIC_OR);
#else
	return _InterlockedOr64((volatile __int64 *)&value->value, operand);
#endif
#else
	HX_OS_ATOMIC_RMW(__atomic_fetch_or);
#endif
}

int64_t hx_os_atomic_fetch_and(
	hx_os_atomic *value, int64_t operand, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;
#if defined(_M_IX86)
	return hx_os_atomic_msvc32_update(value, operand, HX_OS_ATOMIC_AND);
#else
	return _InterlockedAnd64((volatile __int64 *)&value->value, operand);
#endif
#else
	HX_OS_ATOMIC_RMW(__atomic_fetch_and);
#endif
}

int64_t hx_os_atomic_fetch_xor(
	hx_os_atomic *value, int64_t operand, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;
#if defined(_M_IX86)
	return hx_os_atomic_msvc32_update(value, operand, HX_OS_ATOMIC_XOR);
#else
	return _InterlockedXor64((volatile __int64 *)&value->value, operand);
#endif
#else
	HX_OS_ATOMIC_RMW(__atomic_fetch_xor);
#endif
}

int64_t hx_os_atomic_exchange(
	hx_os_atomic *value, int64_t operand, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;
#if defined(_M_IX86)
	return hx_os_atomic_msvc32_update(
		value, operand, HX_OS_ATOMIC_EXCHANGE);
#else
	return _InterlockedExchange64(
		(volatile __int64 *)&value->value, operand);
#endif
#else
	HX_OS_ATOMIC_RMW(__atomic_exchange_n);
#endif
}

static int hx_os_atomic_cas(hx_os_atomic *value, int64_t *expected,
	int64_t desired, int weak, hx_os_memory_order success,
	hx_os_memory_order failure)
{
#if defined(_MSC_VER)
	int64_t old = _InterlockedCompareExchange64(
		(volatile __int64 *)&value->value, desired, *expected);
	(void)weak;
	(void)success;
	(void)failure;
	if (old == *expected)
		return 1;

	*expected = old;

	return 0;
#else
	HX_OS_ATOMIC_CAS();
#endif
}

int hx_os_atomic_compare_exchange(hx_os_atomic *value, int64_t *expected,
	int64_t desired, hx_os_memory_order success, hx_os_memory_order failure)
{
	return hx_os_atomic_cas(value, expected, desired, 0, success, failure);
}

int hx_os_atomic_compare_exchange_weak(hx_os_atomic *value, int64_t *expected,
	int64_t desired, hx_os_memory_order success, hx_os_memory_order failure)
{
	return hx_os_atomic_cas(value, expected, desired, 1, success, failure);
}

int hx_os_atomic_is_lock_free(void)
{
#if defined(_MSC_VER)
	return 1;
#else
	return __atomic_always_lock_free(8, 0);
#endif
}

int32_t hx_os_atomic32_load(
	const hx_os_atomic32 *value, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;

	return _InterlockedCompareExchange(
		(volatile long *)&value->value, 0, 0);
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

void hx_os_atomic32_store(
	hx_os_atomic32 *value, int32_t desired, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;
	(void)hx_os_atomic32_exchange(value, desired, HX_OS_SEQ_CST);
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

int32_t hx_os_atomic32_fetch_add(
	hx_os_atomic32 *value, int32_t operand, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;

	return _InterlockedExchangeAdd((volatile long *)&value->value, operand);
#else
	HX_OS_ATOMIC_RMW(__atomic_fetch_add);
#endif
}

int32_t hx_os_atomic32_fetch_sub(
	hx_os_atomic32 *value, int32_t operand, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;

	return _InterlockedExchangeAdd((volatile long *)&value->value,
		(int32_t)(UINT32_C(0) - (uint32_t)operand));
#else
	HX_OS_ATOMIC_RMW(__atomic_fetch_sub);
#endif
}

int32_t hx_os_atomic32_fetch_or(
	hx_os_atomic32 *value, int32_t operand, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;

	return _InterlockedOr((volatile long *)&value->value, operand);
#else
	HX_OS_ATOMIC_RMW(__atomic_fetch_or);
#endif
}

int32_t hx_os_atomic32_fetch_and(
	hx_os_atomic32 *value, int32_t operand, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;

	return _InterlockedAnd((volatile long *)&value->value, operand);
#else
	HX_OS_ATOMIC_RMW(__atomic_fetch_and);
#endif
}

int32_t hx_os_atomic32_fetch_xor(
	hx_os_atomic32 *value, int32_t operand, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;

	return _InterlockedXor((volatile long *)&value->value, operand);
#else
	HX_OS_ATOMIC_RMW(__atomic_fetch_xor);
#endif
}

int32_t hx_os_atomic32_exchange(
	hx_os_atomic32 *value, int32_t operand, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;

	return _InterlockedExchange((volatile long *)&value->value, operand);
#else
	HX_OS_ATOMIC_RMW(__atomic_exchange_n);
#endif
}

static int hx_os_atomic32_cas(hx_os_atomic32 *value, int32_t *expected,
	int32_t desired, int weak, hx_os_memory_order success,
	hx_os_memory_order failure)
{
#if defined(_MSC_VER)
	int32_t old = _InterlockedCompareExchange(
		(volatile long *)&value->value, desired, *expected);
	(void)weak;
	(void)success;
	(void)failure;
	if (old == *expected)
		return 1;

	*expected = old;

	return 0;
#else
	HX_OS_ATOMIC_CAS();
#endif
}

int hx_os_atomic32_compare_exchange(hx_os_atomic32 *value, int32_t *expected,
	int32_t desired, hx_os_memory_order success, hx_os_memory_order failure)
{
	return hx_os_atomic32_cas(
		value, expected, desired, 0, success, failure);
}

int hx_os_atomic32_compare_exchange_weak(hx_os_atomic32 *value,
	int32_t *expected, int32_t desired, hx_os_memory_order success,
	hx_os_memory_order failure)
{
	return hx_os_atomic32_cas(
		value, expected, desired, 1, success, failure);
}

int hx_os_atomic32_is_lock_free(void)
{
#if defined(_MSC_VER)
	return 1;
#else
	return __atomic_always_lock_free(4, 0);
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

void hx_os_atomic_ptr_store(
	hx_os_atomic_ptr *value, void *desired, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;
	(void)hx_os_atomic_ptr_exchange(value, desired, HX_OS_SEQ_CST);
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

void *hx_os_atomic_ptr_exchange(
	hx_os_atomic_ptr *value, void *operand, hx_os_memory_order order)
{
#if defined(_MSC_VER)
	(void)order;

	return _InterlockedExchangePointer(
		(void *volatile *)&value->value, operand);
#else
	HX_OS_ATOMIC_RMW(__atomic_exchange_n);
#endif
}

static int hx_os_atomic_ptr_cas(hx_os_atomic_ptr *value, void **expected,
	void *desired, int weak, hx_os_memory_order success,
	hx_os_memory_order failure)
{
#if defined(_MSC_VER)
	void *old = _InterlockedCompareExchangePointer(
		(void *volatile *)&value->value, desired, *expected);
	(void)weak;
	(void)success;
	(void)failure;
	if (old == *expected)
		return 1;

	*expected = old;

	return 0;
#else
	HX_OS_ATOMIC_CAS();
#endif
}

int hx_os_atomic_ptr_compare_exchange(hx_os_atomic_ptr *value, void **expected,
	void *desired, hx_os_memory_order success, hx_os_memory_order failure)
{
	return hx_os_atomic_ptr_cas(
		value, expected, desired, 0, success, failure);
}

int hx_os_atomic_ptr_compare_exchange_weak(hx_os_atomic_ptr *value,
	void **expected, void *desired, hx_os_memory_order success,
	hx_os_memory_order failure)
{
	return hx_os_atomic_ptr_cas(
		value, expected, desired, 1, success, failure);
}

int hx_os_atomic_ptr_is_lock_free(void)
{
#if defined(_MSC_VER)
	return 1;
#else
	return __atomic_always_lock_free(sizeof(void *), 0);
#endif
}

void hx_os_atomic_fence(hx_os_memory_order order)
{
#if defined(_MSC_VER)
	if (order == HX_OS_RELAXED)
		_ReadWriteBarrier();
	else {
		volatile long barrier = 0;

		(void)_InterlockedExchange(&barrier, 1);
	}
#else
	switch (order) {
	case HX_OS_RELAXED:
		__asm__ __volatile__("" ::: "memory");
		break;
	case HX_OS_ACQUIRE:
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		break;
	case HX_OS_RELEASE:
		__atomic_thread_fence(__ATOMIC_RELEASE);
		break;
	case HX_OS_ACQ_REL:
		__atomic_thread_fence(__ATOMIC_ACQ_REL);
		break;

	default:
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		break;
	}
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
