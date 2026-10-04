#ifndef HX_C_PLATFORM_ATOMIC_H
#define HX_C_PLATFORM_ATOMIC_H

#include "hx_os_base.h"

#if defined(_MSC_VER)
#define HX_OS_ALIGN_8 __declspec(align(8))
#define HX_OS_ALIGN_4 __declspec(align(4))
#elif defined(__GNUC__)
#define HX_OS_ALIGN_8 __attribute__((aligned(8)))
#define HX_OS_ALIGN_4 __attribute__((aligned(4)))
#else
#error Unsupported compiler atomic storage
#endif

typedef struct HX_OS_ALIGN_8 hx_os_atomic {
	int64_t value;
} hx_os_atomic;

typedef struct HX_OS_ALIGN_4 hx_os_atomic32 {
	int32_t value;
} hx_os_atomic32;

typedef struct hx_os_atomic_ptr {
	void *value;
} hx_os_atomic_ptr;

#define HX_OS_ATOMIC_INIT(value)                                               \
	{                                                                      \
		value                                                          \
	}
#define HX_OS_ATOMIC32_INIT(value)                                             \
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
 * Caller-owned writable storage, including native RMW-based loads. Initialize
 * before publication. Never concurrently read/write the raw member, pack the
 * types, copy live storage, or alias it with C++ std::atomic. Alignment is 8
 * for the existing 64-bit type, 4 for atomic32, pointer alignment for ptr.
 *
 * Load: relaxed/acquire/seq_cst. Store: relaxed/release/seq_cst.
 * RMW: all five orders. CAS legal success/failure pairs:
 * relaxed/relaxed; acquire/{relaxed,acquire}; release/relaxed;
 * acq_rel/{relaxed,acquire}; seq_cst/{relaxed,acquire,seq_cst}.
 * Unsupported enum values/illegal combinations conservatively use seq_cst
 * (both orders for CAS). No assertion, global error or process termination.
 * GCC/Clang dispatch to constant native orders; MSVC is never weaker than
 * requested and uses full-barrier Interlocked operations.
 *
 * Fetch/exchange return the prior value; arithmetic wraps modulo 2^width.
 * CAS returns 1 when stored, preserving *expected; otherwise 0 and copies
 * the observed value to *expected. expected is private writable storage,
 * must not overlap value, and is not concurrently accessed by other threads.
 * Weak CAS may fail spuriously; MSVC uses strong CAS, as does native x86.
 * These operations do not allocate through context or call user callbacks.
 * Non-lock-free targets may use compiler runtime locks (libatomic); these
 * are not universally async-signal-safe or wait-free. No ownership transfer
 * or reclamation/ABA protection is provided for stored pointers.
 */
HX_OS_API int64_t HX_OS_CALL hx_os_atomic_load(
	const hx_os_atomic *value, hx_os_memory_order order);

HX_OS_API void HX_OS_CALL hx_os_atomic_store(
	hx_os_atomic *value, int64_t desired, hx_os_memory_order order);

HX_OS_API int64_t HX_OS_CALL hx_os_atomic_fetch_add(
	hx_os_atomic *value, int64_t operand, hx_os_memory_order order);

HX_OS_API int64_t HX_OS_CALL hx_os_atomic_fetch_sub(
	hx_os_atomic *value, int64_t operand, hx_os_memory_order order);

HX_OS_API int64_t HX_OS_CALL hx_os_atomic_fetch_or(
	hx_os_atomic *value, int64_t operand, hx_os_memory_order order);

HX_OS_API int64_t HX_OS_CALL hx_os_atomic_fetch_and(
	hx_os_atomic *value, int64_t operand, hx_os_memory_order order);

HX_OS_API int64_t HX_OS_CALL hx_os_atomic_fetch_xor(
	hx_os_atomic *value, int64_t operand, hx_os_memory_order order);

HX_OS_API int64_t HX_OS_CALL hx_os_atomic_exchange(
	hx_os_atomic *value, int64_t operand, hx_os_memory_order order);

HX_OS_API int HX_OS_CALL hx_os_atomic_compare_exchange(hx_os_atomic *value,
	int64_t *expected, int64_t desired, hx_os_memory_order success,
	hx_os_memory_order failure);

HX_OS_API int HX_OS_CALL hx_os_atomic_compare_exchange_weak(hx_os_atomic *value,
	int64_t *expected, int64_t desired, hx_os_memory_order success,
	hx_os_memory_order failure);

HX_OS_API int HX_OS_CALL hx_os_atomic_is_lock_free(void);

HX_OS_API int32_t HX_OS_CALL hx_os_atomic32_load(
	const hx_os_atomic32 *value, hx_os_memory_order order);

HX_OS_API void HX_OS_CALL hx_os_atomic32_store(
	hx_os_atomic32 *value, int32_t desired, hx_os_memory_order order);

HX_OS_API int32_t HX_OS_CALL hx_os_atomic32_fetch_add(
	hx_os_atomic32 *value, int32_t operand, hx_os_memory_order order);

HX_OS_API int32_t HX_OS_CALL hx_os_atomic32_fetch_sub(
	hx_os_atomic32 *value, int32_t operand, hx_os_memory_order order);

HX_OS_API int32_t HX_OS_CALL hx_os_atomic32_fetch_or(
	hx_os_atomic32 *value, int32_t operand, hx_os_memory_order order);

HX_OS_API int32_t HX_OS_CALL hx_os_atomic32_fetch_and(
	hx_os_atomic32 *value, int32_t operand, hx_os_memory_order order);

HX_OS_API int32_t HX_OS_CALL hx_os_atomic32_fetch_xor(
	hx_os_atomic32 *value, int32_t operand, hx_os_memory_order order);

HX_OS_API int32_t HX_OS_CALL hx_os_atomic32_exchange(
	hx_os_atomic32 *value, int32_t operand, hx_os_memory_order order);

HX_OS_API int HX_OS_CALL hx_os_atomic32_compare_exchange(hx_os_atomic32 *value,
	int32_t *expected, int32_t desired, hx_os_memory_order success,
	hx_os_memory_order failure);

HX_OS_API int HX_OS_CALL hx_os_atomic32_compare_exchange_weak(
	hx_os_atomic32 *value, int32_t *expected, int32_t desired,
	hx_os_memory_order success, hx_os_memory_order failure);

HX_OS_API int HX_OS_CALL hx_os_atomic32_is_lock_free(void);

HX_OS_API void *HX_OS_CALL hx_os_atomic_ptr_load(
	const hx_os_atomic_ptr *value, hx_os_memory_order order);

HX_OS_API void HX_OS_CALL hx_os_atomic_ptr_store(
	hx_os_atomic_ptr *value, void *desired, hx_os_memory_order order);

HX_OS_API void *HX_OS_CALL hx_os_atomic_ptr_exchange(
	hx_os_atomic_ptr *value, void *desired, hx_os_memory_order order);

HX_OS_API int HX_OS_CALL hx_os_atomic_ptr_compare_exchange(
	hx_os_atomic_ptr *value, void **expected, void *desired,
	hx_os_memory_order success, hx_os_memory_order failure);

HX_OS_API int HX_OS_CALL hx_os_atomic_ptr_compare_exchange_weak(
	hx_os_atomic_ptr *value, void **expected, void *desired,
	hx_os_memory_order success, hx_os_memory_order failure);

HX_OS_API int HX_OS_CALL hx_os_atomic_ptr_is_lock_free(void);

/* Fences synchronize only in conjunction with atomic communication.
 * Relaxed is a compiler barrier only; other orders have the named fence
 * semantics (MSVC uses a full barrier). Unknown values use seq_cst.
 */
HX_OS_API void HX_OS_CALL hx_os_atomic_fence(hx_os_memory_order order);

#ifdef __cplusplus
}
#endif
#endif
