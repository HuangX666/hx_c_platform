#ifndef HX_C_PLATFORM_RING_H
#define HX_C_PLATFORM_RING_H

#include "hx_os_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Caller-owned object and byte storage; no allocation, locking or waiting.
 * All fields are private implementation state. Initialize before use, never
 * copy a live ring, and serialize access including each entire borrow.
 * Storage must outlive the object and any spans, and must not overlap the
 * object. This fixed layout will not be enlarged by future versions.
 */
typedef struct hx_os_ring {
	unsigned char *data;
	size_t capacity;
	size_t head;
	size_t count;
	unsigned int borrowed;
} hx_os_ring;

typedef struct hx_os_ring_read_span {
	const unsigned char *data;
	size_t size;
} hx_os_ring_read_span;

typedef struct hx_os_ring_write_span {
	unsigned char *data;
	size_t size;
} hx_os_ring_write_span;

/* init accepts uninitialized objects and does not read their old contents.
 * Reinitialization requires no active operation/borrow and discards data.
 * NULL storage is valid only for capacity=0. No destructor is required.
 * size/capacity/space accept NULL and return zero. clear preserves storage.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_ring_init(hx_os_ring *ring,
	void *storage, size_t capacity, hx_os_error *error);

HX_OS_API size_t HX_OS_CALL hx_os_ring_size(const hx_os_ring *ring);
HX_OS_API size_t HX_OS_CALL hx_os_ring_capacity(const hx_os_ring *ring);
HX_OS_API size_t HX_OS_CALL hx_os_ring_space(const hx_os_ring *ring);

HX_OS_API hx_os_status HX_OS_CALL hx_os_ring_clear(
	hx_os_ring *ring, hx_os_error *error);

/* Exact byte copies: insufficient input returns NEED_INPUT; insufficient
 * space returns OUTPUT_FULL. Failure preserves contents and output. NULL
 * read output discards. Other buffers must not overlap the ring or storage.
 * A successful zero-byte copy changes nothing. No overwrite or implicit EOF.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_ring_write(hx_os_ring *ring,
	const void *data, size_t bytes, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_ring_read(hx_os_ring *ring,
	void *data, size_t bytes, hx_os_error *error);

/* Borrow all currently readable/writable bytes as at most two ordered spans.
 * An empty read returns NEED_INPUT, a full write returns OUTPUT_FULL; neither
 * creates a borrow. Only one borrow may exist; copying, clearing or another
 * borrow returns BUSY while it exists. Output span arrays must not alias the
 * ring or its storage. Unused spans are {NULL,0}.
 * consume/commit accept a prefix of the offered bytes, then end the entire
 * borrow. Passing zero abandons it. RANGE/INVALID failures retain the borrow
 * for retry. A successful end invalidates BOTH spans, including unused bytes.
 * Never read/write spans after that point. commit publishes only bytes the
 * caller initialized. No operation or C++ destructor commits automatically.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_ring_read_spans(hx_os_ring *ring,
	hx_os_ring_read_span spans[2], hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_ring_write_spans(hx_os_ring *ring,
	hx_os_ring_write_span spans[2], hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_ring_consume(
	hx_os_ring *ring, size_t bytes, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_ring_commit(
	hx_os_ring *ring, size_t bytes, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
