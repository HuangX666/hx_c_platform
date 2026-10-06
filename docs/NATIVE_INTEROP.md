# Borrowing native objects

[简体中文](NATIVE_INTEROP.zh-CN.md) · [README](../README.md)

## 1. When to use this

Use ordinary library functions for portable work. Use these explicit interoperability queries when another API requires the native object of a file, socket, thread or process you already own. Each query borrows the existing object; there is no duplicate, ownership transfer, adopted external object or extended lifetime.

Include `<hx_os_native.h>` or `<hx_os.h>`. `HX_OS_ENABLE_NATIVE_INTEROP` defaults to ON; `hx_os_native_interop_available()` checks the build. Socket and process queries also require their own modules. Disabled combinations return `HX_OS_NOT_SUPPORTED`.

| Query | Windows result | POSIX result |
| --- | --- | --- |
| `hx_os_file_native_borrow` | HANDLE as uintptr_t | Nonnegative file descriptor as uintptr_t |
| `hx_os_socket_native_borrow` | SOCKET as uintptr_t | Nonnegative socket descriptor as uintptr_t |
| `hx_os_process_native_borrow` | HANDLE as uintptr_t | Child process ID as uintptr_t; this is **not** a descriptor |
| `hx_os_thread_native_borrow` | Copies a HANDLE into native storage | Copies pthread_t without assuming that it is an integer |

All failure paths keep the destination unchanged. Queries perform no allocation or OS call, take no lock and start no worker. Thread/socket state checks use existing atomics. This is not a native-operation cancellation facility.

## 2. Pass an owned file to a native query

Inside a function returning `hx_os_status`, with a valid open `hx_os_file *file`:

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

Include `<windows.h>` for the Windows branch or `<sys/stat.h>` for POSIX. In production, preserve the native query's GetLastError/errno details as appropriate. Finish all native use before closing `file` through the library. Never call CloseHandle/close on the borrowed value. A native file seek/read/write changes the same open-file state and offset observed by the library; it is not an independent stream. Handle short I/O and POSIX SIGPIPE according to the native operation.

## 3. Thread and process lifetime

For C, declare the real native thread type after including its native header:

```c
#if defined(_WIN32)
HANDLE native;
#else
pthread_t native;
#endif

hx_os_status status = hx_os_thread_native_borrow(
	thread, &native, sizeof(native), NULL);
```

Include `<pthread.h>` on POSIX. No integer conversion of pthread_t is needed. `hx_os_thread_native_size()` is available for tools that allocate their own storage; ordinary typed calls already know sizeof(native). Insufficient capacity returns OUTPUT_FULL; extra bytes are untouched. A joined thread returns CLOSED, and an active join returns BUSY. Do not call native join/detach/cancel or forcibly terminate a borrowed thread; complete its lifecycle through this library.

For processes, use the native value only before this library records/reaps child exit. After successful waiting, the borrow query returns CLOSED on both platforms, even though Windows internally still holds a process handle until destruction. `hx_os_process_identifier()` remains the diagnostic ID query. Do not reap the child externally or change its ownership; native IDs can later be reused.

## 4. Socket and concurrency boundaries

A socket attached to this library's loop returns BUSY, even after its requests finish. A pending request also prevents borrowing. The loop owns those I/O operations; the raw handle cannot be passed to a competing event system. There is no implicit detach, close or forced cancellation.

The caller must protect the owner and its state **through the entire native operation**, not only through the borrow call. Exclude close/destruction, join/reaping, loop attachment, mode changes and conflicting native/library operations during that period. A returned number does not retain the owner. Do not change nonblocking/append/overlapped modes, inheritance or managed event registrations behind the library. These restrictions apply to the third-party API receiving the value too: check that it borrows the object and leaves its ownership/modes intact.

The queries do not export internal mutex/condition-variable storage. Use the public synchronization interfaces; exposing their bytes would couple consumers to native implementation layout.

## 5. C++11 and executable checks

[native_file.c](../examples/native_file.c) is a complete portable program showing file ownership and a native query. Build `hx_os_native_file`, then run `build/bin/hx_os_native_file hello.txt` or `build/bin/Release/hx_os_native_file.exe hello.txt`. It prints the file size and closes through this library. Its CLI uses SYSTEM_UTILS for UTF-8 arguments; the interoperability module itself has no such dependency.

Include `<hx/Native.hpp>`:

```cpp
auto handle = hx::Native::file(file);
if (!handle)
	return handle.status();

// Use handle.value() synchronously with the appropriate native API.
// The original file owner must remain alive until that use finishes.
return HX_OS_OK;
```

`Native::socket`, `Native::process` and `Native::thread` follow the same Result pattern. The thread result holds `hx::NativeThread` (void pointer/HANDLE on Windows, pthread_t on POSIX); this facade includes pthread.h only on POSIX. No destructor closes a borrowed native value, and no exception is used.

The complete [test_native_interop.c](../tests/test_native_interop.c) demonstrates native file inspection/write followed by library read, a live native thread identity check, socket type inspection, rejection while a loop owns I/O, and a controlled child process followed by normal library cleanup. Run `ctest --test-dir build -C Release -R native_interop --output-on-failure`. It uses its own temporary file and child executable. See [validation](VALIDATION.md) for exact tested environments.
