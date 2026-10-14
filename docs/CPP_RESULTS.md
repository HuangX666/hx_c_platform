# C++11 results, ownership and migration

[简体中文](CPP_RESULTS.zh-CN.md) · [README](../README.md) · [Thread pool](CPP_POOL.md)

All fallible first-party C++ entry points use `hx::Result<T>` or `hx::Result<void>`. No exception syntax, exception objects or deferred exception propagation are used. The C99 library and public C ABI are unchanged. Include the desired `hx/*.hpp` header and link `hx_c_platform::cpp`.

## Reading and propagating a result

The [Variant facade](VARIANT.md#cpp) follows the same result rules: set/emplace/visit return Result<void>, clone returns Result<Variant>, scalar get returns a copy and custom get returns a checked borrowed reference. Strings are stored as owned bytes and read through StringView. Copy construction is explicit/fallible through clone; a custom type's ordinary C++ constructor/destructor must meet the noexcept contract.

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

`ok()` / explicit boolean conversion means status OK and, for a non-void result, a value exists. `status()` returns the C status. `error()` borrows the result's copied `hx_os_error`, including native domain, native code and cause. Copy that record if it must outlive the result. No global last-error state or temporary diagnostic pointer is introduced.

`Result<T>` is move-only. Moving consumes the source and leaves it CLOSED without a value. Its inline slot requires non-failing move construction and destruction; copying a value into the slot requires a non-failing copy. `Result<T&>` is a copyable borrowed reference and never extends the referent's lifetime. `Result<void>` contains just the error record; default construction means success.

`value()` requires `hasValue()`. Violating this precondition is a programming error, with a debug assertion; it is not a recoverable error path. `get()` on a non-void result returns a nullable pointer and supports checked access. Do not use a moved-from result. Results do not add locks or heap allocations; contained resources may allocate as documented.

## Partial completion and timeout

| Operation | Result behavior |
| --- | --- |
| File readSome/readAt and local-socket readUntil | Result<ReadResult>; bytes/eof remain available with hasValue(), including EOF and partial failure |
| File writeAll/writeAt and local-socket writeUntil | Result<size_t>; completed bytes remain available even on error |
| IoRequest::waitUntil | Failed wait has no value; completed native I/O carries completion/bytes even on I/O error |
| File::read | EOF succeeds with eof=true; failures retain byte progress |
| Directory::forEach / SettingsUpdate::finish | Completed/applied count remains available on failure |
| Directory::next | EOF has no entry; allocation failure does not consume the pending entry |
| Lexer::next | EOF carries an END token; token-copy allocation failure leaves the cursor available for retry |
| Task<T>::getUntil | Wait failure preserves the task; completed outcome consumes it, including cancellation/failure |
| ProcessCapture::run / closeUntil | Failure or timeout may retain the host-owned native session for continuation |
| Wait::repeat and SignalHandler::emit | hasValue() can expose completed callback count alongside failure |

Check the operation's status before treating a value as successful data. A result can contain a value while `ok()` is false. A deadline requests bounded waiting; it cannot preempt native completion, callback execution or user cleanup. Preserve owners, borrowed buffers and code modules until successful close/retirement.

## Strings, buffers and allocator scope

`hx::String` owns an `hx_string`, preserving UTF-8, byte lengths and embedded NUL. It is move-only; `String::copy(view, context)` makes an explicit fallible copy. `assign()` preserves old contents on failure. `c_str()` is terminated; use `size()` for binary-safe lengths. `StringView` borrows bytes, permits pointer+length, and has no implicit allocating conversion. A view expires when its source changes or is destroyed.

`hx::Buffer` owns context-allocated bytes. `Buffer::allocate()` returns NO_MEMORY on allocation failure. Destruction and movement release/transfer through the creating allocator. String output methods accept an optional context; default NULL selects the existing default allocator. For explicitly provided output String objects, such as loader diagnostics, that object's context is used.

C++ callbacks and thread-pool jobs store typed captures directly in context-allocated storage. They do not route library-owned allocation through standard string, vector or type-erased function containers. Custom allocator callbacks and user types must honor the no-failure construction/destruction contract. User code remains responsible for any standard-library allocation it chooses to perform; disabling compiler exceptions alone cannot make such allocation recoverable.

## Common operations with fewer checks

`String::trimAscii().lowerAscii()` edits the same initialized string, returns a reference and allocates nothing. `append` and `reserve` remain fallible. Settings and parameter packs offer explicit `update()` / `edit()` cursors: setters apply immediately, the first failure skips later setters, and `finish()` reports it once. Settings finish retains the successful count on failure. Individual Result-returning setters remain available. See the complete [direct-call example](../examples/direct_calls.cpp) and [C/C++ contracts](COMMON_UTILITIES.md).

`ConfigDocument::stringView`, `CallDataView::stringView` and `TextLookup::getView` return borrowed text. Keep their document/pack/catalog storage alive and exclude mutation. A missing catalog entry instead borrows the supplied fallback. `Settings::stringView` and `SettingValue::asStringView` return `SettingStringView`, which retains immutable value bytes and survives settings updates/close; copying the lease allocates nothing. A plain `StringView` obtained from that lease still borrows the lease. All copying string getters remain available.

`Directory::forEach` handles open/read/close and reuses its name buffer; EOF and a bool callback returning false end successfully. `File::read` reports normal EOF through `ReadResult::eof` with success, while `readSome/readAt` retain their existing EOF status. Default-valued settings getters fall back only on NOT_FOUND.

## Callbacks and lifetimes

Callbacks for signals, procedures, async pipes, directory/descriptor watches and periodic timers can return void, `hx_os_status` or `Result<void>`. Functors/lambdas must declare `noexcept`; C++11 function pointers/references promise this behavior by contract. Capture construction/destruction must not fail. Void means success, a status reports its category, and Result preserves full native details.

Signal/procedure calls return the failure directly. Procedure parameter updates commit only on success. Async I/O stores callback failure separately from the native completion, available through `callbackResult()` after completion. Watch/timer failure requests stop; read `callbackResult()` after retirement or successful close. Early inspection returns the applicable waiting/busy status rather than racing callback state.

Native resources and facades retain separate references to callback state. Native retirement destroys captures once even if the facade remains alive to inspect diagnostics. Destroying an I/O request facade before completion releases observation only; native operation and its capture remain alive. Read/write buffers passed to async pipes are borrowed until completion callback return, including StringView writes. Keep their actual storage alive.

Synchronous resources use RAII and factories returning Result: File::open, Directory::open, Library::open, Pages::allocate, MappedFile::open/create, FileRangeLock::acquire, CpuUsage::create, Semaphore::create and the platform request factories. Call explicit close/unlock if its error matters. Default inert constructors do not create resources. ProfileScope is nonmovable; construct it with a profiler, call `start()` and check its result, then `finish()` when the completion status matters.

ThreadPool, Thread, IoLoop, AsyncPipe, Process, ProcessCapture, DirectoryWatch, DescriptorWatch, PeriodicTimer, SignalHandler, ProcedureHandler, Settings and Profiler refer to host-owned native owner variables. Their facades do not hide infinite waits or discard retryable handles on destruction. Host variables and callback captures must survive all in-flight work and close retries. Exclude concurrent changes to owner variables.

## Updating existing consumers

| Previous calling pattern | Current pattern |
| --- | --- |
| Resource constructor performs fallible work | Factory returns Result<Resource>; check before moving/accessing value |
| Text method returns a standard string | Result<hx::String>; use c_str()/size()/view() |
| Getter returns T directly | Result<T>; inspect status and then value |
| Multiple mutable setters | update()/edit() plus one finish(), or check individual Result<void> calls |
| Deferred callback error propagation | callbackResult() after native retirement |
| Cancellation checks through unwinding | token.check() returns Result<void> |
| submit followed by direct value retrieval | Check Result<Task<T>>, then Result<T> from getUntil() |
| Implicit token/parser error text | Copied hx_os_error; optional parser diagnostic or caller-owned loader String |

These are C++ source and template-layout changes. Recompile C++ consumers/plugins together. C callers retain their existing exported functions, public structures and ABI. No new third-party dependency is introduced.

## Building without exceptions

The repository applies these settings privately to its C++ test/example targets. Installed interface targets do not change unrelated consumers. For an application target that adopts the same policy:

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

See [platform support](PLATFORMS.md) for target build/deployment requirements and limits. Public examples are independent of private maintainer regressions.
