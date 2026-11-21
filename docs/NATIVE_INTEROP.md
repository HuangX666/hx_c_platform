# Borrowing native objects

[简体中文](NATIVE_INTEROP.zh-CN.md) · [README](../README.md)

For portable application logic, use the standard `hx_c_platform` file, socket, thread, and process APIs. When you need to pass a resource already opened by `hx_c_platform` to an OS API or third-party library that expects a raw native handle (such as a Windows `HANDLE` / `SOCKET` or a POSIX file descriptor `fd` / `pthread_t`), use the native interoperability API (`<hx_os_native.h>` / `<hx/Native.hpp>`).

To see how handle borrowing works end to end, prepare the 10-byte [demo.txt](../examples/README.md#inputs) input file, build `hx_os_native_file` from [native_file.c](../examples/native_file.c), and run it inside your `build` directory:

```sh
# Linux / macOS:
./bin/hx_os_native_file demo.txt

# Windows PowerShell:
.\bin\Release\hx_os_native_file.exe demo.txt
```

The program prints `Native query: 10 bytes`. Internally, [native_file.c](../examples/native_file.c) opens the file through `hx_c_platform`, borrows the underlying `HANDLE` or `fd` via `hx_os_file_native_borrow` to call `GetFileSizeEx` / `fstat`, reads the file, and finally closes it through the original `hx_os_file` owner.

## 1. When to use this

Use these explicit interoperability queries only when an external API requires the native representation of a file, socket, thread, or process that you already own through `hx_c_platform`. Every query returns a **borrowed value**—it does not duplicate the descriptor/handle, transfer ownership, adopt external resources, or extend the owner's lifetime.

Include `<hx_os_native.h>` or `<hx_os.h>`. The feature is controlled by `HX_OS_ENABLE_NATIVE_INTEROP` (`ON` by default), and `hx_os_native_interop_available()` reports whether compiled support is present. Socket and process queries also require their respective modules (`HX_OS_ENABLE_NETWORK` / `HX_OS_ENABLE_PROCESS`); disabled combinations return `HX_OS_NOT_SUPPORTED`.

| Query Function | Windows Result | POSIX Result |
| --- | --- | --- |
| `hx_os_file_native_borrow` | File `HANDLE` cast to `uintptr_t` | Non-negative file descriptor (`fd`) cast to `uintptr_t` |
| `hx_os_socket_native_borrow` | `SOCKET` cast to `uintptr_t` | Non-negative socket descriptor (`fd`) cast to `uintptr_t` |
| `hx_os_process_native_borrow` | Child process `HANDLE` cast to `uintptr_t` | Child process ID (`pid_t`) cast to `uintptr_t` (**not** a file descriptor) |
| `hx_os_thread_native_borrow` | Copies the thread `HANDLE` into caller storage | Copies `pthread_t` into caller storage without assuming it is an integer |

On any failure, the caller's destination variable is left untouched. Queries perform no heap allocation, make no OS syscalls, take no mutexes, and start no worker threads (thread and socket state checks read existing atomic fields). Borrowing a native handle does not add cancellation support to blocking native calls.

## 2. Pass an owned file to a native query

Inside a function returning `hx_os_status` that already holds an open `hx_os_file *file`, you can borrow the native handle and invoke a platform-specific API as follows:

```c
uintptr_t native;
hx_os_status status = hx_os_file_native_borrow(file, &native, NULL);
if (status != HX_OS_OK)
	return status;

#if defined(_WIN32)
DWORD kind = GetFileType((HANDLE)native);
if (kind == FILE_TYPE_UNKNOWN)
	return HX_OS_IO_ERROR;
#else
struct stat info;
if (fstat((int)native, &info) != 0)
	return HX_OS_IO_ERROR;
#endif

return HX_OS_OK;
```

Include `<windows.h>` on Windows or `<sys/stat.h>` on POSIX (and capture `GetLastError()` / `errno` as needed for your own diagnostics).

> [!WARNING]
> **Borrowed File Handle Rules**
> - **Never close a borrowed handle**: Complete all native operations before closing `file` through `hx_os_file_close`. Never call `CloseHandle()` or `close()` directly on the borrowed `native` value.
> - **Shared open-file description and file offset**: Performing a native `seek`, `read`, or `write` mutates the same kernel open-file state and cursor observed by `hx_c_platform`. Handle short I/O and POSIX `SIGPIPE` according to the native call's contract.

## 3. Thread and process lifetime

Because POSIX permits `pthread_t` to be a struct or pointer rather than an integer, declare the real native thread type after including the platform header and pass `sizeof(native)` to `hx_os_thread_native_borrow`:

```c
#if defined(_WIN32)
HANDLE native;
#else
pthread_t native;
#endif

hx_os_status status = hx_os_thread_native_borrow(
	thread, &native, sizeof(native), NULL);
```

Include `<pthread.h>` on POSIX (`hx_os_thread_native_size()` is also provided for bindings that allocate storage dynamically, though typed C callers already know `sizeof(native)`). If the destination buffer is too small, the call returns `HX_OS_OUTPUT_FULL`; any extra trailing bytes in a larger buffer are left untouched.

- **Thread lifetime rules**: Borrowing a thread that has already been joined returns `HX_OS_CLOSED`, and borrowing while a join is in progress returns `HX_OS_BUSY`. **Never call native `pthread_join`, `pthread_detach`, `pthread_cancel`, `WaitForSingleObject`, or terminate a borrowed thread**—always complete its lifecycle through `hx_c_platform`.
- **Process lifetime rules**: Borrow a child process's native handle/PID only before `hx_c_platform` waits for and reaps the child's exit. Once a wait succeeds, `hx_os_process_native_borrow` returns `HX_OS_CLOSED` on both Windows and POSIX (even though Windows keeps its internal `HANDLE` until the `hx_os_process` object is destroyed). To read the numeric process ID for logging or diagnostics, call `hx_os_process_identifier()`. **Never reap the child process externally or close its Windows handle**, as OS PIDs can be recycled once reaped.

## 4. Socket and concurrency boundaries

- **Loop-attached sockets cannot be borrowed**: Once a socket has been attached to an `hx_os_loop`, `hx_os_socket_native_borrow` returns `HX_OS_BUSY` even after all queued requests have completed (a pending request also returns `HX_OS_BUSY`). Because the event loop owns the socket's readiness registration or IOCP association, the raw descriptor cannot be handed over to a competing event system.
- **Keep the owner alive and serialized across the entire native operation**: You must protect the owning object and its state **throughout the entire native call**, not just during the `*_native_borrow` call itself. Exclude close/destruction, thread joining/process reaping, loop attachment, mode changes, and conflicting native or library operations for that entire window.
- **Do not mutate underlying descriptor flags**: Never alter nonblocking, append, or overlapped flags, handle inheritance (`CLOEXEC`), or event registrations behind the library's back. Before passing a borrowed handle to a third-party function, verify that it merely borrows the handle and leaves its ownership and descriptor flags intact.
- **Synchronization primitives remain opaque**: Native mutex and condition-variable storage is intentionally not exported so consumers do not couple to platform-specific struct layouts.

## 5. C++11 and executable checks

In C++11, include `<hx/Native.hpp>` to borrow native handles via `hx::Result<T>` without C++ exceptions or destructive RAII on the borrowed value:

```cpp
auto handle = hx::Native::file(file);
if (!handle)
	return handle.status();

// Use handle.value() synchronously with the appropriate native API.
// The original file owner must remain alive until that use finishes.
return HX_OS_OK;
```

`hx::Native::socket`, `hx::Native::process`, and `hx::Native::thread` follow the exact same `hx::Result` pattern. `hx::Native::thread` returns `hx::Result<hx::NativeThread>` (`void *` / `HANDLE` on Windows, `pthread_t` on POSIX; `<hx/Native.hpp>` includes `<pthread.h>` only on POSIX).

The runnable [native_file.c](../examples/native_file.c) example demonstrates borrowing a native file handle to query file size and then reading the file through the library (note: its CLI helper uses `HX_OS_ENABLE_SYSTEM_UTILS` for UTF-8 command-line arguments, though the interoperability module itself does not depend on `SYSTEM_UTILS`). See [platform support](PLATFORMS.md) for target OS requirements.

## 4. Import a socket you already own

While the `borrow` functions above let external code temporarily use a resource owned by `hx_c_platform`, you can also go in the opposite direction: use `hx_os_socket_import` from `<hx_os_net_interop.h>` to bring an existing native nonblocking TCP or UDP socket into `hx_c_platform`:

- **`HX_OS_SOCKET_TAKE` (transfer ownership)**: Consumes the caller's native socket handle only when the import succeeds (on failure, the caller retains ownership).
- **`HX_OS_SOCKET_DUP` (duplicate handle)**: Leaves the caller's original handle owned by the caller and creates a second descriptor/handle referencing the same underlying endpoint.

Before importing with either mode, exclude all active I/O, concurrent closes, and external event-loop registrations. On Windows, the caller must also ensure the socket was created with `WSA_FLAG_OVERLAPPED` and is not already associated with an existing IOCP port (Windows cannot reassign an existing IOCP binding). See [import and shutdown examples](NETWORK_IO.md#5-existing-native-sockets-and-shutdown) for shared-state and shutdown timeout rules.
