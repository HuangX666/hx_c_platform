# One-time initialization

[简体中文](ONCE.zh-CN.md)

## 1. Initialize shared state once

`hx_os_once` publishes one successful initialization to concurrent callers. It is independently owned and does not start a thread or change process-global configuration.

```c
static hx_os_status initialize(void *user, hx_os_error *error)
{
	int *port = user;

	(void)error;
	*port = 8080;
	return HX_OS_OK;
}

hx_os_once *once = NULL;
int port = 0;

hx_os_status status = hx_os_once_create(NULL, &once, NULL);
if (status == HX_OS_OK)
	status = hx_os_once_run(once, initialize, &port, NULL);
```

Keep `once` and `port` alive while workers call `run`. Every caller uses an equivalent initializer and the same logical state. Returning `OK` makes initialized writes visible to subsequent successful callers. Failed initialization is not remembered; a later caller can retry. The initializer must undo its own partially created resources before reporting failure.

## 2. Waiting and lifetime

`run_until` adds a monotonic deadline and optional cancellation token to contention waiting. It cannot interrupt an initializer already executing. An uninitialized object can start initialization with deadline `NOW`; completed initialization wins over cancellation. Reentering the same object from its initializer returns `DEADLOCK`.

Exclude new and active callers before destruction. Observed in-flight initialization returns `BUSY` and preserves the object. There is no reset operation that could invalidate published state. Destroying the once object does not destroy the initialized application resources.

After initialization succeeds, run_until returns OK even if its token was already cancelled or the deadline is NOW. This completed path does not subscribe to cancellation or rerun the initializer.

C++ `hx::Once::run` accepts a callable returning `void` or `Result<void>`. It uses no exceptions. The object must outlive all calls, and the application's state has its own lifetime.

## 3. Example

Build and run `hx_os_once`. [once.c](../examples/once.c) starts four workers; the initialization message appears once and each worker reads port 8080. Enable with `HX_OS_ENABLE_ONCE`. Cancellation requires the coordination module; ordinary initialization does not require a process-wide library init.
