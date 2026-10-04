#ifndef HX_C_PLATFORM_WATCH_H
#define HX_C_PLATFORM_WATCH_H

#include "hx_os_thread.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct hx_os_watch hx_os_watch;

enum {
	HX_OS_WATCH_CHANGED,
	HX_OS_WATCH_ADDED,
	HX_OS_WATCH_REMOVED,
	HX_OS_WATCH_RENAMED,
	HX_OS_WATCH_OVERFLOW,
	HX_OS_WATCH_ERROR
};

/* DIRECTORY is positive evidence; absence can mean unknown on a backend. */
enum { HX_OS_WATCH_DIRECTORY = 1u };

typedef struct hx_os_watch_event {
	const char *path;
	const char *old_path;
	hx_os_error error;
	uint32_t size;
	uint32_t version;
	uint32_t kind;
	uint32_t flags;
} hx_os_watch_event;

typedef struct hx_os_watch_options {
	uint32_t size;
	uint32_t version;
	size_t buffer_bytes;
	size_t max_path_bytes;
	size_t max_directories;
	size_t max_depth;
	size_t max_memory_bytes;
} hx_os_watch_options;
#define HX_OS_WATCH_OPTIONS_INIT                                               \
	{                                                                      \
		sizeof(hx_os_watch_options), 1, 65536, 32768, 1024, 64,        \
			4 * 1024 * 1024                                        \
	}
typedef void(HX_OS_CALL *hx_os_watch_fn)(
	hx_os_watch *watch, const hx_os_watch_event *event, void *user);
typedef void(HX_OS_CALL *hx_os_watch_cleanup_fn)(void *user);

HX_OS_API hx_os_status HX_OS_CALL hx_os_watch_options_init(
	hx_os_watch_options *options, size_t capacity);

/*
 * Explicitly starts one worker, no global runtime. Creation waits until the
 * native subscription is armed. *out is assigned before callbacks start,
 * although a callback can run before create returns. user is borrowed until
 * close succeeds. Callback paths are borrowed strict UTF-8, relative with
 * '/' separators; "" means root. Rename old_path is best effort/nullable.
 * Native coalescing/order is not a transaction log. OVERFLOW means rescan;
 * ERROR is terminal. Callbacks run outside locks on this object's worker.
 * Linux recursion pins directory fds through /proc/self/fd. Missing procfs,
 * ambiguous aliases and 9p filesystems report NOT_SUPPORTED.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_watch_create(hx_os_context *context,
	const char *path, int recursive, hx_os_watch_fn function, void *user,
	hx_os_watch **out, hx_os_error *error);

/* Retained variable storage uses max_memory_bytes, including allocation
 * overhead. Fixed thread/event controls, transient bounded native path
 * conversion and OS-managed buffers are outside this budget.
 * max_directories/max_depth bound software recursive scans (Linux); native
 * Windows/Apple tree subscriptions do not allocate a record per directory.
 * Acceptance transfers optional cleanup, called once after callbacks stop.
 * Rejection calls no user callback/cleanup. No callback may throw/longjmp.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_watch_create_with_options(
	hx_os_context *context, const char *path, int recursive,
	const hx_os_watch_options *options, hx_os_watch_fn function,
	hx_os_watch_cleanup_fn cleanup, void *user, hx_os_watch **out,
	hx_os_error *error);

/* Thread-safe request; caller excludes concurrent object destruction. */
HX_OS_API void HX_OS_CALL hx_os_watch_stop(hx_os_watch *watch);

/* Stop/join/close, including callback, cleanup and TLS return. Timeout keeps
 * *watch alive for retry. Self-close reports DEADLOCK. Once retired consumes
 * it, even when reporting a terminal backend error. Exclude parallel close.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_watch_close_until(
	hx_os_watch **watch, hx_os_deadline deadline, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
