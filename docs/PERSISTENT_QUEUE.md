# Persistent messages with SQLite

[简体中文](PERSISTENT_QUEUE.zh-CN.md) · [README](../README.md)

## 1. Write, take, acknowledge

Use this queue for records that must survive disconnects or application restarts. A producer assigns a stable record ID and pushes JSON or arbitrary bytes. A consumer takes an owned message, handles/uploads it, then acknowledges only after business success.

```c
hx_os_persistent_queue *queue = NULL;
hx_os_persistent_message *message = NULL;

hx_os_status status = hx_os_persistent_queue_open(
	NULL, "records.db", NULL, &queue, NULL);
if (status == HX_OS_OK)
	status = hx_os_persistent_queue_take_until(queue,
		hx_os_deadline_after_ms(1000), NULL, &message, NULL);

if (status == HX_OS_OK) {
	hx_os_bytes_view body = hx_os_persistent_message_data(message);

	printf("%.*s\n", (int)body.size, (const char *)body.data);
	// Replace printing with real work; acknowledge only on success.
	status = hx_os_persistent_queue_ack(queue, message, NULL);
}

hx_os_persistent_message_destroy(&message);
hx_os_persistent_queue_close(&queue, NULL);
```

Replace the comment with real processing and check its outcome. On a retryable business failure, call release instead of ack. Destroying the returned copy neither acknowledges nor releases its lease.

## 2. Runnable JSON workflow

The complete [persistent_queue.c](../examples/persistent_queue.c) uses recognition records from factory and hospital gates:

```sh
cmake --build build-shared-linux --target hx_os_persistent_queue
build-shared-linux/bin/hx_os_persistent_queue publish records.db
build-shared-linux/bin/hx_os_persistent_queue consume records.db
```

Start the last command in two terminals to test multiple consumers. Each successful take claims one record; the example prints it and acknowledges it. Replace the print with an upload and acknowledge only when the server confirms. Windows uses `--config Debug` and the executable under `bin/Debug/`.

C++11 provides `hx::PersistentQueue::open`, `push`, `take`, `ack`, `release`, `renew`, and `close` in [PersistentQueue.hpp](../include/hx/PersistentQueue.hpp), using Result and owned messages.

## 3. Delivery and crash recovery

The same local queue file supports multiple processes and threads. The default lease lasts 30 seconds. renew explicitly extends an unexpired receipt; release makes it available immediately. After expiry or a consumer crash, a later take returns the same ID with a new receipt. Old receipts return CLOSED and cannot delete a later delivery.

Delivery is at least once. The remote business service must deduplicate by stable ID. A pending ID with identical payload is an idempotent push; different payload returns EXISTS. Once acknowledged and deleted, that ID is not remembered forever. An uncertain commit must be retried with the original IDs.

Leases persist UTC milliseconds. System-clock changes affect expiry. Deadlines for local waiting remain monotonic; no persisted monotonic clock is invented. Use a local filesystem, not a network-mounted database file.

Claim, ack and renew sample UTC after acquiring the SQLite write transaction. Waiting for a competing writer does not extend an expired receipt: ack/renew return CLOSED if its lease has expired by then, and the record remains queued. A successful renewal starts its duration from that sample. Synchronization and returning to the caller can still consume the remaining lease; success does not promise a minimum lease remaining when the function returns.

## 4. Durability, batches and capacity

Single push/ack and each batch execute a transaction and request synchronization before success. The backend uses a DELETE rollback journal and synchronous EXTRA for new databases. Hardware/filesystem guarantees still determine actual power-loss durability.

push_many and ack_many accept up to 1024 items and commit the whole batch once. This is an atomic database batch, unlike the completed-prefix semantics of the in-memory message queue. Failed commits can be indeterminate.

Defaults are 65,536 pending messages, 64 MiB payload total, and 1 MiB per message. A full queue returns LIMIT; unacknowledged records are never evicted. Capacity is stored at creation. NULL options inherit an existing queue's capacity; explicitly incompatible capacities are rejected. SQLite pages, journal overhead and returned copies are additional. Deleted pages are reusable; the file is not promised to shrink immediately.

## 5. Lifecycle and native use

No worker or network service starts. Open/write/ack have a five-second SQLite lock-contention budget; native disk synchronization can exceed it. take is nonblocking; take_until retries emptiness/contention using bounded sleeps and supports cancellation. It cannot interrupt arbitrary filesystem work.

Exclude active/new callers before close. A close failure retains the handle. Returned messages own their bytes independently of the queue. IDs/views borrowed from a message expire when it is destroyed.

PERSISTENT_QUEUE is independently selectable and requires RANDOM and THREAD_EX. SQLite has its own allocator; library objects use the chosen context. The queue owns its dedicated schema and native connection. Use a separate application connection for direct SQLite work through [hx_c_platform::sqlite](NATIVE_DEPENDENCIES.md), without editing the queue schema or changing its journal policy.
