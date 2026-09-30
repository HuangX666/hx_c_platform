#ifndef HX_C_PLATFORM_ATOMIC_H
#define HX_C_PLATFORM_ATOMIC_H

#include "hx_os_base.h"

#if defined(_MSC_VER)
#define HX_OS_ALIGN_8 __declspec(align(8))
#elif defined(__GNUC__)
#define HX_OS_ALIGN_8 __attribute__((aligned(8)))
#else
#error Unsupported compiler atomic storage
#endif

typedef struct HX_OS_ALIGN_8 hx_os_atomic {
	int64_t value;
} hx_os_atomic;

typedef struct hx_os_atomic_ptr {
	void *value;
} hx_os_atomic_ptr;

#define HX_OS_ATOMIC_INIT(value)                                               \
	{                                                                      \
		value                                                          \
	}
#define HX_OS_ATOMIC_PTR_INIT(value)                                           \
	{                                                                      \
		value                                                          \
	}

typedef enum hx_os_memory_order {
	HX_OS_RELAXED,
	HX_OS_ACQUIRE,
	HX_OS_RELEASE,
	HX_OS_ACQ_REL,
	HX_OS_SEQ_CST
} hx_os_memory_order;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Storage must be writable, including for native RMW-based loads.
 * Initialize before publication; never access storage members concurrently.
 * Valid load orders: relaxed/acquire/seq_cst. Valid store orders:
 * relaxed/release/seq_cst. CAS failure excludes release/acq_rel and cannot
 * exceed success. Implementations may use a stronger memory order.
 */
HX_OS_API int64_t HX_OS_CALL hx_os_atomic_load(
	const hx_os_atomic *value, hx_os_memory_order order);
HX_OS_API void HX_OS_CALL hx_os_atomic_store(
	hx_os_atomic *value, int64_t desired, hx_os_memory_order order);
HX_OS_API int64_t HX_OS_CALL hx_os_atomic_fetch_add(
	hx_os_atomic *value, int64_t amount, hx_os_memory_order order);
HX_OS_API int HX_OS_CALL hx_os_atomic_compare_exchange(hx_os_atomic *value,
	int64_t *expected, int64_t desired, hx_os_memory_order success,
	hx_os_memory_order failure);
HX_OS_API void *HX_OS_CALL hx_os_atomic_ptr_load(
	const hx_os_atomic_ptr *value, hx_os_memory_order order);
HX_OS_API void *HX_OS_CALL hx_os_atomic_ptr_exchange(
	hx_os_atomic_ptr *value, void *desired, hx_os_memory_order order);
HX_OS_API int HX_OS_CALL hx_os_atomic_is_lock_free(void);

#ifdef __cplusplus
}
#endif
#endif
