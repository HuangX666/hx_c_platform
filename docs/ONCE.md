# One-time initialization

[简体中文](ONCE.zh-CN.md)

Use `hx_os_once` when multiple worker threads depend on shared configuration or lazily initialized state that must be initialized at most once before any thread reads it.

### Quick Start Example

Open [once.c](../examples/once.c) for a complete runnable program: it creates an `hx_os_once` instance, starts four worker threads that each call `hx_os_once_run()`, reads the initialized port number in every worker, joins all four threads, and destroys the `once` object.

Follow the [example run instructions](../examples/README.md#run) to enable examples and build `hx_os_once` (no command-line arguments or input files are required):

```sh
cmake --build build --config Release --target hx_os_once
```

Expected output: `Settings initialized once` is printed exactly once, and `Worker uses port 8080` is printed four times. You can also copy [once.c](../examples/once.c) as your starting `main.c` in the [minimal project](USAGE.md#c-project) setup.

## 1. Initialize shared state once

`hx_os_once` is an explicitly owned synchronization primitive that spawns no background threads and modifies no process-global state.

The self-contained `main.c` below calls `hx_os_once_run()` twice in sequence, running `initialize()` only on the first call (see [once.c](../examples/once.c) for the four-thread concurrent version):

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

- **Memory visibility guarantee**: Once the initializer callback returns `HX_OS_OK`, all waiting and future callers of `hx_os_once_run()` receive `HX_OS_OK` and can safely read the data written by the initializer (such as `port`) without additional locking.
- **Retryable on failure**: If the initializer returns a non-`HX_OS_OK` status, the `hx_os_once` object is **not** permanently poisoned; a waiting or subsequent caller can retry initialization. The failing initializer callback must clean up any partially created resources before returning an error.

## 2. Waiting and lifetime

In addition to `hx_os_once_run()`, `hx_os_once_run_until(once, fn, user, deadline, token, error)` bounds how long a contending thread waits for another thread's in-progress initialization:
- **Does not interrupt the active initializer**: The deadline and cancellation token apply only to threads waiting for an already running initializer; they never preempt the thread executing the callback.
- **Completed fast path**: Once initialization has succeeded, subsequent calls to `hx_os_once_run_until()` immediately return `HX_OS_OK`—even if passed `HX_OS_DEADLINE_NOW` or an already-cancelled token—without subscribing to cancellation or rerunning the callback. If the object is still uninitialized and uncontended, passing `HX_OS_DEADLINE_NOW` starts the initializer directly.
- **Recursive reentry detection**: Calling `run` or `run_until` on the same `hx_os_once` object from inside its own initializer callback returns `HX_OS_DEADLOCK`.

> [!IMPORTANT]
> **Destruction boundary and application state ownership**
> - Exclude all active and new callers before calling `hx_os_once_destroy()` (if an in-flight call is detected, `destroy` returns `HX_OS_BUSY` and preserves the object).
> - `hx_os_once` intentionally omits a `reset` operation so already-published state can never be invalidated under concurrent readers.
> - Destroying the `hx_os_once` handle frees only the synchronization object itself—it does **not** free the application resources initialized by your callback.

In C++11, `<hx/Once.hpp>` provides `hx::Once::run`, accepting any exception-free callable returning `void` or `Result<void>`.

## 3. Example

Build and run `hx_os_once` to execute the four-thread initialization workflow in [once.c](../examples/once.c). The module is controlled by `HX_OS_ENABLE_ONCE`; cancellable waiting requires the coordination module (`HX_OS_ENABLE_THREAD_EX`), and basic usage requires no process-wide library initialization.
