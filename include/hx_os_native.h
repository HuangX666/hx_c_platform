#ifndef HX_C_PLATFORM_NATIVE_H
#define HX_C_PLATFORM_NATIVE_H

#include "hx_os_file.h"
#include "hx_os_net.h"
#include "hx_os_process.h"
#include "hx_os_thread.h"

#ifdef __cplusplus
extern "C" {
#endif

HX_OS_API int HX_OS_CALL hx_os_native_interop_available(void);

/**
 * \brief Explicit platform escape hatches.
 *
 * \details Values are BORROWED, never transferred or duplicated. Keep the owner
 * alive and externally exclude close, join/reaping, attach, configuration
 * changes and competing native/library operations for the entire use of the
 * value, not just this query. No reference is retained. Do not close, join,
 * detach, cancel, reap, reassign or transfer ownership of the returned native
 * object. Do not change blocking/append/overlapped modes, inheritance or
 * library-managed event registrations. Native calls retain their own
 * blocking/error behavior; this query supplies no cancellation.
 *
 * File: Windows HANDLE converted to uintptr_t; POSIX nonnegative descriptor.
 * File offsets and data are the owner's same open-file state, not a snapshot.
 * Native reads/writes require the caller to handle partial I/O and SIGPIPE.
 * Socket: Windows SOCKET; POSIX descriptor. Requires NETWORK. Attached or
 * pending sockets return BUSY, even for an intended read-only native query.
 * Process: Windows HANDLE; POSIX pid (not a descriptor). Requires PROCESS.
 * After this library has reaped/recorded exit, borrowing returns CLOSED. For a
 * diagnostic process ID independent of this restriction, use
 * hx_os_process_identifier(). Native identifiers may be reused by the OS.
 *
 * Outputs remain unchanged on failure. No allocation, lock, worker or native
 * syscall is added. Disabled modules return NOT_SUPPORTED. These operations do
 * not adopt external objects and cannot extend a borrowed native lifetime.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_native_borrow(
	const hx_os_file *file, uintptr_t *value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_native_borrow(
	const hx_os_socket *socket, uintptr_t *value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_process_native_borrow(
	const hx_os_process *process, uintptr_t *value, hx_os_error *error);

/**
 * \brief Thread: copies a Windows HANDLE or POSIX pthread_t into caller
 * storage, preserving that type's representation (pthread_t is not assumed
 * numeric).
 *
 * \details Include the native SDK header to declare its type, then pass &native
 * and sizeof(native). Too little capacity returns OUTPUT_FULL; extra bytes
 * remain untouched. NULL is invalid. Joined threads return CLOSED; an active
 * join returns BUSY. Host exclusion is still required through the native use.
 * The size query returns the native type size, or zero when disabled.
 */
HX_OS_API size_t HX_OS_CALL hx_os_thread_native_size(void);

HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_native_borrow(
	const hx_os_thread *thread, void *storage, size_t capacity,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
