#ifndef HX_C_PLATFORM_BASE_H
#define HX_C_PLATFORM_BASE_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define HX_OS_CALL __cdecl
#if defined(HX_OS_SHARED)
#if defined(HX_OS_BUILD)
#define HX_OS_API __declspec(dllexport)
#else
#define HX_OS_API __declspec(dllimport)
#endif
#else
#define HX_OS_API
#endif
#else
#define HX_OS_CALL
#if defined(__GNUC__)
#define HX_OS_API __attribute__((visibility("default")))
#else
#define HX_OS_API
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define HX_OS_VERSION_MAJOR 0
#define HX_OS_VERSION_MINOR 1
#define HX_OS_VERSION_PATCH 0
#define HX_OS_ABI_VERSION 1

typedef int32_t hx_os_status;

enum {
	HX_OS_OK = 0,
	HX_OS_EOF = 1,
	HX_OS_NEED_INPUT = 2,
	HX_OS_OUTPUT_FULL = 3,
	HX_OS_INVALID = -1,
	HX_OS_NO_MEMORY = -2,
	HX_OS_IO_ERROR = -3,
	HX_OS_NOT_FOUND = -4,
	HX_OS_EXISTS = -5,
	HX_OS_ACCESS_DENIED = -6,
	HX_OS_TIMEOUT = -7,
	HX_OS_CANCELLED = -8,
	HX_OS_BUSY = -9,
	HX_OS_CLOSED = -10,
	HX_OS_NOT_SUPPORTED = -11,
	HX_OS_LIMIT = -12,
	HX_OS_WOULD_BLOCK = -13,
	HX_OS_DEADLOCK = -14,
	HX_OS_INVALID_ENCODING = -15,
	HX_OS_UNREPRESENTABLE = -16,
	HX_OS_TRUNCATED_INPUT = -17,
	HX_OS_CROSS_DEVICE = -18,
	HX_OS_RANGE = -19,
	HX_OS_SYNTAX = -20,
	HX_OS_TYPE_MISMATCH = -21,
	HX_OS_AUTH_FAILED = -22
};

enum {
	HX_OS_ERROR_NONE,
	HX_OS_ERROR_ERRNO,
	HX_OS_ERROR_WIN32,
	HX_OS_ERROR_WINSOCK,
	HX_OS_ERROR_GAI,
	HX_OS_ERROR_MACH,
	HX_OS_ERROR_CONFIG,
	HX_OS_ERROR_LOADER,
	HX_OS_ERROR_HRESULT,
	HX_OS_ERROR_NTSTATUS,
	HX_OS_ERROR_OPENSSL,
	HX_OS_ERROR_ARCHIVE
};

typedef struct hx_os_error {
	hx_os_status status;
	uint32_t domain;
	int64_t native_code;
	int64_t cause_code;
} hx_os_error;

typedef struct hx_os_context hx_os_context;
typedef void *(HX_OS_CALL *hx_os_allocate_fn)(size_t bytes, void *user);
typedef void(HX_OS_CALL *hx_os_deallocate_fn)(void *memory, void *user);

/*
 * Callbacks must have malloc-equivalent fundamental alignment, be thread-safe
 * and support cross-thread release. The copied table and borrowed user state
 * remain valid until context destruction succeeds. Alignment above the
 * fundamental alignment is supplied by the library's allocation wrapper.
 */
typedef struct hx_os_allocator {
	hx_os_allocate_fn allocate;
	hx_os_deallocate_fn deallocate;
	void *user;
} hx_os_allocator;

typedef struct hx_os_context_options {
	uint32_t size;
	uint32_t version;
	hx_os_allocator allocator;
} hx_os_context_options;

HX_OS_API const char *HX_OS_CALL hx_os_status_name(hx_os_status status);

HX_OS_API void HX_OS_CALL hx_os_error_clear(hx_os_error *error);

/* required includes NUL; a small buffer is NUL-terminated when capacity > 0.
 * Native GAI text and Apple Mach text are appended only when valid UTF-8.
 * Other targets retain numeric Mach diagnostics. No network initialization.
 * Locale-dependent text is diagnostic, not a stable machine-readable key.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_error_message(const hx_os_error *error,
	char *buffer, size_t capacity, size_t *required);

HX_OS_API void HX_OS_CALL hx_os_context_options_init(
	hx_os_context_options *options);

HX_OS_API hx_os_status HX_OS_CALL hx_os_context_create(
	const hx_os_context_options *options, hx_os_context **out,
	hx_os_error *error);

/* NULL context selects an immutable default allocator, not global settings. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_context_destroy(
	hx_os_context **context, hx_os_error *error);

HX_OS_API size_t HX_OS_CALL hx_os_context_allocations(
	const hx_os_context *context);

/* Zero-byte allocation is valid. alignment=0 selects fundamental alignment. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_memory_alloc(hx_os_context *context,
	size_t bytes, size_t alignment, void **out, hx_os_error *error);

HX_OS_API void HX_OS_CALL hx_os_memory_free(void *memory);

typedef uint64_t hx_os_deadline;
#define HX_OS_DEADLINE_NOW UINT64_C(0)
#define HX_OS_DEADLINE_INFINITE UINT64_MAX

/* Read variants retain native failures. Units do not imply resolution. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_monotonic_read(
	uint64_t *nanoseconds, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_wall_time_read(
	int64_t *milliseconds, hx_os_error *error);

/*
 * Monotonic time excludes suspend. Convenience reads return UINT64_MAX
 * (monotonic) or INT64_MIN (wall) on failure. Deadline creation then returns
 * NOW; use the status-returning read when clock failures need diagnosis.
 */
HX_OS_API uint64_t HX_OS_CALL hx_os_monotonic_ns(void);

HX_OS_API int64_t HX_OS_CALL hx_os_wall_time_ms(void);

/* Overflow saturates to the largest finite deadline. */
HX_OS_API hx_os_deadline HX_OS_CALL hx_os_deadline_after_ms(uint64_t ms);

HX_OS_API int HX_OS_CALL hx_os_deadline_expired(hx_os_deadline deadline);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sleep_until(
	hx_os_deadline deadline, hx_os_error *error);

HX_OS_API void HX_OS_CALL hx_os_cpu_relax(void);

enum {
	HX_OS_CAP_THREADS = UINT64_C(1) << 0,
	HX_OS_CAP_TIMED_JOIN = UINT64_C(1) << 1,
	HX_OS_CAP_NETWORK = UINT64_C(1) << 2,
	HX_OS_CAP_LOOP = UINT64_C(1) << 3,
	HX_OS_CAP_THREAD_POOL = UINT64_C(1) << 4,
	HX_OS_CAP_MEMORY_POOLS = UINT64_C(1) << 5,
	HX_OS_CAP_LEGACY_TEXT = UINT64_C(1) << 6,
	HX_OS_CAP_ATOMIC64_LOCK_FREE = UINT64_C(1) << 7,
	HX_OS_CAP_CONFIG = UINT64_C(1) << 8,
	HX_OS_CAP_CONFIG_DOUBLE = UINT64_C(1) << 9,
	HX_OS_CAP_CONTAINERS = UINT64_C(1) << 10
};

HX_OS_API uint64_t HX_OS_CALL hx_os_capabilities(void);

HX_OS_API const char *HX_OS_CALL hx_os_platform_name(void);

HX_OS_API const char *HX_OS_CALL hx_os_cpu_name(void);
#define HX_OS_CAP_SYSINFO (UINT64_C(1) << 11)
#define HX_OS_CAP_LIBRARY (UINT64_C(1) << 12)
#define HX_OS_CAP_FILE_UTILS (UINT64_C(1) << 13)
#define HX_OS_CAP_GLOB (UINT64_C(1) << 14)
#define HX_OS_CAP_ENV (UINT64_C(1) << 15)
#define HX_OS_CAP_PATHS (UINT64_C(1) << 16)
#define HX_OS_CAP_THREAD_EX (UINT64_C(1) << 17)
#define HX_OS_CAP_PIPE (UINT64_C(1) << 18)
#define HX_OS_CAP_ASYNC_PIPE (UINT64_C(1) << 19)
#define HX_OS_CAP_NET_OPTIONS (UINT64_C(1) << 20)
#define HX_OS_CAP_POSITIONED_IO (UINT64_C(1) << 21)
#define HX_OS_CAP_FILE_INFO (UINT64_C(1) << 22)
#define HX_OS_CAP_BOM (UINT64_C(1) << 23)
#define HX_OS_CAP_TEXT_NUMBER (UINT64_C(1) << 24)
#define HX_OS_CAP_CONFIG_SERIALIZE (UINT64_C(1) << 25)
#define HX_OS_CAP_TIMER (UINT64_C(1) << 26)
#define HX_OS_CAP_RANDOM (UINT64_C(1) << 27)
#define HX_OS_CAP_VIRTUAL_MEMORY (UINT64_C(1) << 28)
#define HX_OS_CAP_FILE_EXTRA (UINT64_C(1) << 29)
#define HX_OS_CAP_WATCH (UINT64_C(1) << 30)
#define HX_OS_CAP_LOOP_FD (UINT64_C(1) << 31)
#define HX_OS_CAP_SYSTEM_UTILS (UINT64_C(1) << 32)
#define HX_OS_CAP_PROCESS (UINT64_C(1) << 33)
#define HX_OS_CAP_DELAYED_TASKS (UINT64_C(1) << 34)
#define HX_OS_CAP_LOCAL_SOCKETS (UINT64_C(1) << 35)
#define HX_OS_CAP_DUPLEX_PIPE (UINT64_C(1) << 36)
#define HX_OS_CAP_REGISTRY (UINT64_C(1) << 37)
#define HX_OS_CAP_SLEEP_INHIBIT (UINT64_C(1) << 38)
#define HX_OS_CAP_PERFORMANCE_REQUEST (UINT64_C(1) << 39)
#define HX_OS_CAP_OBFUSCATION (UINT64_C(1) << 40)
#define HX_OS_CAP_CALENDAR (UINT64_C(1) << 41)
#define HX_OS_CAP_TEXT_LOOKUP (UINT64_C(1) << 42)
#define HX_OS_CAP_BITSTREAM (UINT64_C(1) << 43)
#define HX_OS_CAP_SIGNALS (UINT64_C(1) << 44)
#define HX_OS_CAP_SETTINGS (UINT64_C(1) << 45)
#define HX_OS_CAP_PROFILER (UINT64_C(1) << 46)
#define HX_OS_CAP_WIDE_MATH (UINT64_C(1) << 47)
#define HX_OS_CAP_PRECISE_WAIT (UINT64_C(1) << 48)
#define HX_OS_CAP_LEXER (UINT64_C(1) << 49)
#define HX_OS_CAP_PROCEDURES (UINT64_C(1) << 50)
#define HX_OS_CAP_VARIANT (UINT64_C(1) << 51)
#define HX_OS_CAP_FILE_SEARCH (UINT64_C(1) << 52)
#define HX_OS_CAP_COROUTINES (UINT64_C(1) << 53)
#define HX_OS_CAP_STACKFUL_COROUTINES (UINT64_C(1) << 54)
#define HX_OS_CAP_COROUTINE_IO (UINT64_C(1) << 55)
#define HX_OS_CAP_FORMAT (UINT64_C(1) << 56)
#define HX_OS_CAP_KCP (UINT64_C(1) << 58)
#define HX_OS_CAP_CODEC (UINT64_C(1) << 59)
#define HX_OS_CAP_IPC (UINT64_C(1) << 57)
#define HX_OS_CAP_STREAM (UINT64_C(1) << 60)

/* required includes NUL. These queries do not need initialization. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_version_string(
	char *buffer, size_t capacity, size_t *required);

HX_OS_API uint32_t HX_OS_CALL hx_os_abi_version(void);

#ifdef __cplusplus
}
#endif
#endif
