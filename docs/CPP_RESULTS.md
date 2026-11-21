# C++11 results, ownership and migration

[简体中文](CPP_RESULTS.zh-CN.md) · [README](../README.md) · [Thread pool](CPP_POOL.md)

Every fallible first-party C++11 API in this library returns `hx::Result<T>` or `hx::Result<void>`. No `try`/`catch` blocks, exception objects, or deferred exception propagation are used anywhere in the C++ facade. The underlying C99 implementation and public C ABI remain unchanged; simply include the desired `hx/*.hpp` header and link `hx_c_platform::cpp`.

### Quick Start: Reading a File with Explicit Error Handling

In the [complete C++ file program](USAGE.md#files), the application calls `hx::Files::readText()`, checks `if (!text)`, and prints `text.value().c_str()`:
- When `hello.txt` exists in the working directory, the program prints its contents.
- When the path does not exist, it prints `Read failed` along with the error status category.

`hx::Result<T>` packs either a return value or an `hx_os_error` diagnostic into a single stack-allocated value. For most operations you simply check `ok()` before reading `value()`. Operations that support partial completion—such as batch I/O—can also retain a completed item or byte count alongside an error or timeout (`hasValue()` tells you when a count is present). The sections below cover result propagation, partial completion, resource ownership, and exception-free builds.

## Reading and propagating a result

~~~cpp
#include <hx/Files.hpp>

hx::Result<hx::String> load_configuration(const char *path) noexcept
{
	auto result = hx::Files::readText(path);
	if (!result)
		return hx::Failure(result.error());

	return std::move(result).value();
}
~~~

- **Checking success and diagnostics**: `ok()` (or explicit boolean conversion) returns `true` when the status is `HX_OS_OK` and, for non-`void` results, a value is present. `status()` returns the C `hx_os_status` code. `error()` borrows the `hx_os_error` stored inside the result, preserving the native error domain, platform code, and cause. Copy that `hx_os_error` struct if the diagnostic must outlive the `Result` object. No thread-global last-error state or temporary diagnostic pointer is used.
- **Move semantics and value lifetime**:
  - `Result<T>` is **move-only**. Moving consumes the source object, setting its status to `HX_OS_CLOSED` with no stored value. Its inline storage requires non-throwing move construction and destruction (and non-throwing copy construction when constructing from a copied value).
  - `Result<T&>` is a copyable borrowed reference that never extends the lifetime of the referenced object.
  - `Result<void>` stores only the error record; a default-constructed `Result<void>` represents `HX_OS_OK`.
- **Accessing the value safely**: Calling `value()` requires `hasValue()` to be `true` (violating this precondition is a programming bug caught by a debug assertion, not a recoverable runtime path). On non-`void` results, `get()` returns a nullable pointer for checked access. Never read from a moved-from `Result`. `Result` itself adds no locks or heap allocations.

The [Variant facade](VARIANT.md#cpp) follows the exact same conventions: `set`, `emplace`, and `visit` return `Result<void>`, `clone` returns `Result<Variant>`, scalar `get` returns a value copy, and custom `get` returns a checked borrowed reference. Strings are stored as owned bytes and read via `StringView`.

Constructing `hx::Failure(HX_OS_OK)` records `HX_OS_INVALID`, including when converted to `Result<void>`.

## Partial completion and timeout

In streaming I/O, batch updates, or deadline-bounded waits, an operation can fail or time out after making partial progress. In those cases, **`ok()` is `false`, yet `hasValue()` can still be `true`**:

| Operation | `Result` behavior and partial-completion contract |
| --- | --- |
| File `readSome` / `readAt` and local-socket `readUntil` | Returns `Result<ReadResult>`; `bytes` and `eof` remain accessible when `hasValue()` is `true`, including on EOF or partial failure |
| File `writeAll` / `writeAt` and local-socket `writeUntil` | Returns `Result<size_t>`; completed byte count remains accessible via `value()` even when an error stops the write |
| `IoRequest::waitUntil` | A failed wait carries no value; once native I/O finishes, the result carries `completion` and `bytes` even if the I/O operation itself failed |
| `File::read` | Normal EOF succeeds (`HX_OS_OK`) with `eof = true`; failures preserve the byte progress completed so far |
| `Directory::forEach` / `SettingsUpdate::finish` | Retains the number of completed entries or applied settings on failure |
| `Directory::next` | EOF carries no directory entry; an allocation failure while copying the entry name leaves the pending entry unconsumed |
| `Lexer::next` | EOF carries an `END` token; a token-copy allocation failure leaves the lexer cursor in place for retry |
| `Task<T>::getUntil` | A wait timeout/failure preserves the `Task` handle; a completed outcome consumes the handle, including cancellation or task failure |
| `ProcessCapture::run` / `closeUntil` | Failure or timeout may retain the host-owned native session handle so cleanup can be retried |
| `Wait::repeat` and `SignalHandler::emit` | `hasValue()` exposes the number of completed callbacks alongside a callback failure |

> [!IMPORTANT]
> **Check status first, and remember that a deadline cannot preempt active work**
> Always check `ok()` or `status()` before treating `value()` as complete output. A monotonic deadline bounds how long the current call waits, but it cannot forcibly preempt in-flight OS I/O, active callbacks, or user cleanup code. Whenever an operation returns `HX_OS_TIMEOUT` or `HX_OS_BUSY`, keep host-owned `owner` variables, borrowed buffers, and code modules alive until the resource is cleanly closed and retired.

## Strings, buffers and allocator scope

- **`hx::String` and `hx::StringView`**: `hx::String` owns an `hx_string`, preserving UTF-8 bytes, explicit byte length, and embedded NUL (`\0`) characters. It is move-only; use `String::copy(view, context)` for an explicit fallible copy. `assign()` leaves existing contents intact if allocation fails. `c_str()` is NUL-terminated, while `size()` returns the exact byte length. `StringView` borrows external bytes (pointer + length) without implicit heap allocation; a view becomes invalid as soon as its underlying source is modified or destroyed.
- **`hx::Buffer`**: Owns bytes allocated through an `hx_os_context`. `Buffer::allocate()` returns `HX_OS_NO_MEMORY` on allocation failure, and destruction or move transfers ownership through the creating allocator. String-producing functions accept an optional `context` (`NULL` selects the default allocator), whereas functions that populate a caller-supplied `hx::String` (such as dynamic-library loader diagnostics) allocate through that `String`'s own context.
- **Allocator boundary**: C++ callbacks and thread-pool jobs store typed captures directly inside context-allocated storage; the library never routes its own allocations through `std::string`, `std::vector`, or `std::function`.

## Common operations with fewer checks

To avoid repetitive per-line checks during string manipulation, configuration updates, or directory scans, the facade provides chaining and cursor helpers (see the [direct-call example](../examples/direct_calls.cpp) and [C/C++ contracts](COMMON_UTILITIES.md)):

- **In-place ASCII transforms**: `String::trimAscii().lowerAscii()` mutates an already initialized string in place, returns a reference to `*this`, and performs no memory allocation (`append` and `reserve` remain fallible and return `Result`).
- **Batch update cursors (`update()` / `edit()`)**: `Settings` and parameter packs provide `update()` and `edit()` cursors. Setters apply immediately; the first failure skips subsequent setters on that cursor, and a single `finish()` call reports the outcome at the end (`SettingsUpdate::finish()` also retains the count of successfully applied settings on failure). Individual `Result`-returning setters remain available.
- **Zero-allocation string views (`*View`)**:
  - `ConfigDocument::stringView`, `CallDataView::stringView`, and `TextLookup::getView` return borrowed `StringView` slices (keep the backing document, parameter pack, or catalog alive and unmutated while using the view; missing catalog keys borrow the caller-supplied fallback).
  - `Settings::stringView` and `SettingValue::asStringView` return `SettingStringView`, a reference-counted lease over immutable value bytes that remains valid even after settings are updated or closed. Copying a `SettingStringView` lease allocates nothing, while a plain `StringView` obtained from that lease borrows from the lease.
- **Directory iteration and defaulted getters**: `Directory::forEach` opens, reads (reusing its internal name buffer), and closes a directory, treating both EOF and a `false` return from a boolean callback as normal completion. Defaulted settings getters fall back to the default value only on `HX_OS_NOT_FOUND`, preserving other errors such as type mismatches.

## Callbacks and lifetimes

C++ callbacks for signals, procedures, async pipes, directory watches, descriptor watches, and periodic timers can return `void`, `hx_os_status`, or `Result<void>`:
- Functors and lambdas must be marked `noexcept` (C++11 function pointers and references promise non-throwing behavior by contract), and capture construction/destruction must not fail.
- Returning `void` indicates `HX_OS_OK`, returning `hx_os_status` reports that status category, and returning `Result<void>` preserves the full `hx_os_error` record.

How callback failures are reported depends on the subsystem:
- **Synchronous signals and procedures**: Return the callback failure directly to the caller (procedure parameter mutations commit only on `HX_OS_OK`).
- **Async I/O requests**: Store callback failures separately from native I/O completion status; inspect `callbackResult()` after the request completes.
- **Watches and periodic timers**: A callback failure requests that the watch or timer stop; inspect `callbackResult()` after retirement or successful close (querying while callbacks are still active returns the corresponding waiting/busy status rather than racing callback state).

> [!NOTE]
> **Two resource-wrapping models: RAII value objects vs. host-owned views**
> 1. **Synchronous resources (RAII factory model)**: `File::open`, `Directory::open`, `Library::open`, `Pages::allocate`, `MappedFile::open`/`create`, `FileRangeLock::acquire`, `CpuUsage::create`, `Semaphore::create`, and platform request factories return `Result<Resource>`. Call explicit `close()` or `unlock()` before scope exit whenever you need to inspect close/unlock errors. `ProfileScope` is non-movable: construct it with a profiler, call `start()` and check the result, then call `finish()` when the completion status matters.
> 2. **Resources with background activity or retryable shutdown (host-owner view model)**: `ThreadPool`, `Thread`, `IoLoop`, `AsyncPipe`, `Process`, `ProcessCapture`, `DirectoryWatch`, `DescriptorWatch`, `PeriodicTimer`, `SignalHandler`, `ProcedureHandler`, `Settings`, and `Profiler` reference a host-owned native pointer variable. Their destructors **never hide unbounded waits or discard retryable handles**. Keep host owner variables, callback captures, and borrowed I/O buffers (including `StringView` buffers passed to async pipes) alive across all in-flight work and close retries.

## Updating existing consumers

When updating C++ code from earlier patterns to the `hx::Result` contract, use the following equivalents:

| Previous calling pattern | Current `hx::Result` pattern |
| --- | --- |
| Constructor performs fallible initialization | Static factory returns `Result<Resource>`; check `ok()` before moving or accessing `value()` |
| Text method returns `std::string` | Returns `Result<hx::String>`; inspect via `c_str()`, `size()`, or `view()` |
| Getter returns `T` directly | Returns `Result<T>`; check status before calling `value()` |
| Sequence of mutable setters | Use `update()` / `edit()` followed by one `finish()`, or check each `Result<void>` call |
| Deferred callback error propagation | Call `callbackResult()` after native resource retirement |
| Cancellation checks via stack unwinding | Call `token.check()` and return `Result<void>` |
| Thread-pool `submit` followed by direct `.get()` | Check `Result<Task<T>>`, then check `Result<T>` from `getUntil()` |
| Implicit global parser/loader error text | Read copied `hx_os_error` from `Result::error()`, or pass a caller-owned diagnostic `hx::String` |

Rebuild C++ consumers and plugins together when updating headers. Pure C callers are unaffected and keep their existing exported functions, public structs, and C99 ABI.

## Building without exceptions

This repository builds its own C++ examples and tests with exceptions disabled, while the exported `hx_c_platform::cpp` interface target leaves exception flags up to your application. To adopt the same exception-free build policy in your own CMake target:

~~~cmake
target_link_libraries(my_app PRIVATE hx_c_platform::cpp)
target_compile_features(my_app PRIVATE cxx_std_11)

if(MSVC)
	target_compile_options(my_app PRIVATE /EHs-c-)
	target_compile_definitions(my_app PRIVATE _HAS_EXCEPTIONS=0)
else()
	target_compile_options(my_app PRIVATE -fno-exceptions)
endif()
~~~

See [platform support](PLATFORMS.md) for compiler, OS, and deployment requirements across supported targets.
