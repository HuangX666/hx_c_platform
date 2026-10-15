# One-time initialization

[简体中文](ONCE.zh-CN.md)

**Start with a complete program.** Route workers through the same once object when they depend on one shared initialization result.

Open [once.c](../examples/once.c). Create once → start four threads → each calls once_run → read the initialized port → join all threads → destroy.

Follow the [example run instructions](../examples/README.md#run), enable examples and build only `hx_os_once`. This program needs no arguments or input files.

**Expected result:** `Settings initialized once` appears once and `Worker uses port 8080` four times. Replace initialize with your setup; once coordinates initialization but does not release application resources.

Short snippets below explain individual calls; use the complete example when running them. You can also use its source as main in the [minimal project](USAGE.md#c-project).

## 1. Initialize shared state once

`hx_os_once` publishes one successful initialization to concurrent callers. It is independently owned and does not start a thread or change process-global configuration.

This sequential program calls run twice but prints the initialization message once, followed by `Port: 8080`. Save it as main.c; the linked once.c demonstrates four threads sharing the same object.

```c
#include <hx_os.h>
#include <stdio.h>

static hx_os_status initialize(void *user, hx_os_error *error)
{
	int *port = user;

	(void)error;
	*port = 8080;
	puts("Settings initialized once");

	return HX_OS_OK;
}

int main(void)
{
	hx_os_once *once = NULL;
	int port = 0;

	hx_os_status status = hx_os_once_create(NULL, &once, NULL);
	if (status == HX_OS_OK)
		status = hx_os_once_run(once, initialize, &port, NULL);
	if (status == HX_OS_OK)
		status = hx_os_once_run(once, initialize, &port, NULL);

	if (status == HX_OS_OK)
		printf("Port: %d\n", port);
	else
		fprintf(stderr, "Initialize: %s\n", hx_os_status_name(status));

	(void)hx_os_once_destroy(&once, NULL);

	return status == HX_OS_OK ? 0 : 1;
}
```

Keep `once` and `port` alive while workers call `run`. Every caller uses an equivalent initializer and the same logical state. Returning `OK` makes initialized writes visible to subsequent successful callers. Failed initialization is not remembered; a later caller can retry. The initializer must undo its own partially created resources before reporting failure.

## 2. Waiting and lifetime

`run_until` adds a monotonic deadline and optional cancellation token to contention waiting. It cannot interrupt an initializer already executing. An uninitialized object can start initialization with deadline `NOW`; completed initialization wins over cancellation. Reentering the same object from its initializer returns `DEADLOCK`.

Exclude new and active callers before destruction. Observed in-flight initialization returns `BUSY` and preserves the object. There is no reset operation that could invalidate published state. Destroying the once object does not destroy the initialized application resources.

After initialization succeeds, run_until returns OK even if its token was already cancelled or the deadline is NOW. This completed path does not subscribe to cancellation or rerun the initializer.

C++ `hx::Once::run` accepts a callable returning `void` or `Result<void>`. It uses no exceptions. The object must outlive all calls, and the application's state has its own lifetime.

## 3. Example

Build and run `hx_os_once`. [once.c](../examples/once.c) starts four workers; the initialization message appears once and each worker reads port 8080. Enable with `HX_OS_ENABLE_ONCE`. Cancellation requires the coordination module; ordinary initialization does not require a process-wide library init.
