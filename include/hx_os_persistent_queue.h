#ifndef HX_C_PLATFORM_PERSISTENT_QUEUE_H
#define HX_C_PLATFORM_PERSISTENT_QUEUE_H

#include "hx_os_params.h"
#include "hx_os_coord.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_persistent_queue hx_os_persistent_queue;
typedef struct hx_os_persistent_message hx_os_persistent_message;

typedef struct hx_os_persistent_options {
	uint32_t size, version;
	uint64_t max_messages, max_content_bytes;
	uint32_t max_message_bytes, lease_ms;
} hx_os_persistent_options;

#define HX_OS_PERSISTENT_OPTIONS_INIT                                          \
	{                                                                      \
		sizeof(hx_os_persistent_options), 1, 65536, 64 * 1024 * 1024,  \
			1024 * 1024, 30000                                     \
	}

typedef struct hx_os_persistent_item {
	const char *id;
	hx_os_bytes_view data;
} hx_os_persistent_item;

typedef struct hx_os_persistent_stats {
	uint64_t messages, content_bytes, in_flight;
	uint64_t max_messages, max_content_bytes;
} hx_os_persistent_stats;

HX_OS_API int HX_OS_CALL hx_os_persistent_queue_available(void);

/**
 * \brief Open a local SQLite message queue shared by processes and threads.
 * \param[in] context Optional allocator for first-party objects.
 * \param[in] path Borrowed, NUL-terminated UTF-8 database path.
 * \param[in] options Optional capacity and lease settings.
 * \param[out] out Receives the queue; NULL on failure.
 * \param[out] error Optional SQLite or platform error details.
 * \returns OK on success; otherwise a configuration, storage or lock error.
 *
 * \details IDs are caller-defined UTF-8 (1..128 bytes), stable across
 * retries/restarts. The database is dedicated to this queue. Capacities persist
 * at creation; NULL options inherit existing capacities. Explicit different
 * capacities are rejected. lease_ms is per handle (1..86400000); NULL uses 30
 * seconds. SQLite uses its own allocator. Queued payload bounds exclude its
 * journal, page overhead and returned message copies. No workers or network
 * services. Open/write/confirm use a five-second native lock-contention budget.
 * Native filesystem synchronization can exceed that budget; no hard deadline.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_persistent_queue_open(
	hx_os_context *context, const char *path,
	const hx_os_persistent_options *options, hx_os_persistent_queue **out,
	hx_os_error *error);

/**
 * \brief Each call is one transaction: successful single/batch writes request
 * disk synchronization before returning.
 *
 * \details Existing pending ID with identical bytes is idempotent; different
 * bytes returns EXISTS. Failed commit can be indeterminate: retry the SAME IDs,
 * never create replacement IDs blindly. The batch is all-or-nothing in SQLite,
 * not a completed-prefix operation. count<=1024. Inputs are borrowed through
 * return; payload is copied to DB. A full queue returns LIMIT; no eviction of
 * unconfirmed messages.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_persistent_queue_push(
	hx_os_persistent_queue *queue, const char *id, hx_os_bytes_view data,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_persistent_queue_push_many(
	hx_os_persistent_queue *queue, const hx_os_persistent_item *items,
	size_t count, hx_os_error *error);

/**
 * \brief Atomically claims the oldest available record and returns an owned
 * copy.
 *
 * \details Empty returns WOULD_BLOCK. Until retries empty/database contention
 * in bounded sleeps; deadlines/cancellation do not interrupt native SQLite
 * work. A lease uses persisted UTC milliseconds (not a durable monotonic
 * clock): wall-clock changes affect expiry. Crash/expiry allows re-delivery
 * with the same ID and a NEW receipt. Exactly-once business effects need peer
 * dedup. Timed-out/failed calls return *out=NULL. Keep the queue alive through
 * calls.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_persistent_queue_take(
	hx_os_persistent_queue *queue, hx_os_persistent_message **out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_persistent_queue_take_until(
	hx_os_persistent_queue *queue, hx_os_deadline deadline,
	hx_os_cancel_token *cancel, hx_os_persistent_message **out,
	hx_os_error *error);
HX_OS_API const char *HX_OS_CALL hx_os_persistent_message_id(
	const hx_os_persistent_message *message);
HX_OS_API hx_os_bytes_view HX_OS_CALL hx_os_persistent_message_data(
	const hx_os_persistent_message *message);
HX_OS_API void HX_OS_CALL hx_os_persistent_message_destroy(
	hx_os_persistent_message **message);

/**
 * \brief Ack removes a live, unexpired receipt; stale/expired receipts return
 * CLOSED and cannot delete a later delivery.
 *
 * \details release immediately requeues the same receipt; renew extends a live
 * receipt by lease_ms from the current UTC. Time is sampled after acquiring the
 * write transaction. Disk sync/return may outlast the remaining lease; this is
 * not a hard real-time guarantee. ack_many uses one synchronized transaction,
 * at most 1024 receipts. Message objects remain owned after these calls;
 * destroy them separately. Deletion reuses SQLite pages; it does not promise
 * instant file shrinking.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_persistent_queue_ack(
	hx_os_persistent_queue *queue, const hx_os_persistent_message *message,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_persistent_queue_ack_many(
	hx_os_persistent_queue *queue,
	const hx_os_persistent_message *const *messages, size_t count,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_persistent_queue_release(
	hx_os_persistent_queue *queue, const hx_os_persistent_message *message,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_persistent_queue_renew(
	hx_os_persistent_queue *queue, const hx_os_persistent_message *message,
	uint32_t lease_ms, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_persistent_queue_stats(
	hx_os_persistent_queue *queue, hx_os_persistent_stats *out,
	hx_os_error *error);
/**
 * \brief Exclude callers before close.
 *
 * \details Failure preserves ownership.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_persistent_queue_close(
	hx_os_persistent_queue **queue, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
