# Persistent messages with SQLite

[简体中文](PERSISTENT_QUEUE.zh-CN.md) · [README](../README.md)

Use the SQLite-backed persistent queue (`hx_os_persistent_queue`) when records—such as gate recognition events, billing records, or telemetry payloads—must survive network outages, process crashes, or system reboots. Producers assign a stable business ID to each record and persist it to disk; consumers claim (`take`) a record under a timed lease and acknowledge (`ack`) it only after the remote service confirms receipt.

### Quick Start Example

With examples and the SQLite backend enabled (see the [run instructions](../examples/README.md#run) for binary paths), build `hx_os_persistent_queue` and run `publish` followed by `consume`:

```sh
cmake --build build --config Release --target hx_os_persistent_queue
./build/bin/hx_os_persistent_queue publish records.db
./build/bin/hx_os_persistent_queue consume records.db
```

- `publish` writes three JSON recognition records into `records.db`.
- `consume` prints three `Handle recognition-...` lines and acknowledges each record.
- Running `consume` a second time waits about one second and exits cleanly without redelivering acknowledged records.

When integrating into your own application, open [persistent_queue.c](../examples/persistent_queue.c) and replace the `printf` call with your actual upload logic: call `ack` only after the server confirms success, or call `release` on a retryable upload failure so the record becomes available immediately.

## 1. Write, take, acknowledge

Taking a record from disk, processing it, and acknowledging completion looks like this:

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

> [!IMPORTANT]
> **Destroying the in-memory message (`destroy`) does not acknowledge (`ack`) or release the lease**
> - `hx_os_persistent_message_destroy(&message)` only frees the in-memory copy held by your process—it **neither deletes the record from SQLite nor releases its active lease**.
> - Call `hx_os_persistent_queue_ack()` only after business processing succeeds to permanently remove the record.
> - Call `hx_os_persistent_queue_release()` on a retryable failure to return the record to the ready state immediately instead of waiting for the lease to expire.

## 2. Runnable JSON workflow

The complete [persistent_queue.c](../examples/persistent_queue.c) program demonstrates publishing and consuming recognition records from factory and hospital gates:

```sh
cmake --build build-shared-linux --target hx_os_persistent_queue
build-shared-linux/bin/hx_os_persistent_queue publish records.db
build-shared-linux/bin/hx_os_persistent_queue consume records.db
```

You can run the `consume` command in two terminals simultaneously to observe multiple processes sharing the same queue file (on Windows with Visual Studio, pass `--config Debug` and run the executable under `bin/Debug/`).

For C++11 applications, [PersistentQueue.hpp](../include/hx/PersistentQueue.hpp) provides `hx::PersistentQueue::open`, `push`, `take`, `ack`, `release`, `renew`, and `close` using exception-free `hx::Result` and RAII-owned message objects.

## 3. Delivery and crash recovery

Multiple processes and threads can safely open the same local SQLite queue file:
- **Timed leases and receipt tokens**: The default processing lease is `30` seconds. Call `renew` to extend an unexpired lease, or `release` to return the record immediately. If a consumer crashes or its lease expires before `ack`, a subsequent `take` redelivers the same message ID with a **new receipt token**. If the stalled consumer later wakes up and attempts `ack` or `renew` with its stale receipt, the call fails with `HX_OS_CLOSED` and cannot delete the new consumer's delivery.
- **At-least-once delivery and idempotent pushes**:
  - Because lease expiry causes redelivery, your remote server should deduplicate records by their stable message ID.
  - Pushing a message ID that is already pending in the queue with the **exact same payload** succeeds idempotently (`HX_OS_OK`), whereas pushing the same pending ID with a **different payload** returns `HX_OS_EXISTS`. Once a record is acknowledged and deleted, its ID is not retained forever.
- **Clocks and local filesystem requirement**:
  - Persisted lease expirations use UTC milliseconds (since monotonic clocks do not survive reboots), so system wall-clock jumps affect lease expiry; local waiting in `take_until` still uses the monotonic clock.
  - Always place the database file on a local filesystem, never on an NFS/SMB network mount.
- **Timestamp sampling under the write lock**: `take`, `ack`, and `renew` sample the current UTC time after acquiring the SQLite write transaction. Waiting for a competing writer does not extend an expiring lease: if the lease has expired by the time the write lock is acquired, `ack` and `renew` return `HX_OS_CLOSED` and leave the record queued.

## 4. Durability, batches and capacity

- **Transaction durability**: Every single `push`/`ack` and every batch executes inside a SQLite transaction and requests disk synchronization before returning `HX_OS_OK`. Newly created databases configure a `DELETE` rollback journal and `synchronous = EXTRA` (actual power-loss durability still depends on the OS, filesystem, and storage hardware honoring flush requests).
- **Atomic batch transactions (`push_many` / `ack_many`)**: Each batch accepts up to `1,024` items and commits the entire batch as a single SQLite transaction—unlike the in-memory message queue's partial-prefix behavior. If a commit fails due to an I/O error, the transaction outcome may be indeterminate; retry using the original message IDs.
- **Bounded capacity**: Default limits are `65,536` pending messages, `64 MiB` total payload bytes, and `1 MiB` per message. When full, new pushes return `HX_OS_LIMIT`; unacknowledged records are never evicted. Capacity limits are persisted when the database is first created; passing `NULL` options when opening an existing queue inherits its stored limits, while passing mismatched limits is rejected.

## 5. Lifecycle and native use

- **No background threads and bounded lock waits**: The queue starts no background threads or network services. `open`, `push`, and `ack` use a `5`-second SQLite lock-contention budget (native disk flushes may exceed that bound). `take` is nonblocking; `take_until` retries empty or contended states using short bounded sleeps and supports cancellation tokens (though it cannot interrupt an in-kernel filesystem call).
- **Closing and message lifetime**: Exclude active and new callers before calling `hx_os_persistent_queue_close()` (a failed close preserves the handle). Returned `hx_os_persistent_message` objects own their payload bytes independently of the queue handle and remain readable even after the queue closes; ID strings and byte views borrowed from a message expire when `hx_os_persistent_message_destroy()` is called.
- **Module trimming and direct SQLite usage**: `HX_OS_ENABLE_PERSISTENT_QUEUE` can be toggled independently and requires `HX_OS_ENABLE_RANDOM` and `HX_OS_ENABLE_THREAD_EX`. The queue owns its dedicated schema and SQLite connection; if your application also uses SQLite directly, open a separate connection via [hx_c_platform::sqlite](NATIVE_DEPENDENCIES.md) and never modify the queue's tables or journal mode.
