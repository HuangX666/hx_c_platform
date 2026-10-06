# Platform utilities

[简体中文](PLATFORM_UTILITIES.zh-CN.md)

Common operations have direct C entry points. Optional C++11 headers wrap the same implementation. A NULL context selects the existing immutable default allocator. Feature switches retain declarations and failure stubs, with the corresponding capability bit cleared.

## System information

`hx_os_sysinfo.h` provides byte counts, CPU topology, page size, filesystem space, current-process memory and OS version. `HX_OS_ENABLE_SYSINFO` defaults to ON. Library version and ABI queries remain available with it disabled. These calls do not create workers, install handlers or alter process state.

```c
hx_string version = HX_STRING_INIT;
hx_os_error error;

hx_os_status status = hx_os_sys_version_string(&version, &error);

/* Check status, then use hx_string_c_str(&version). */
hx_string_destroy(&version);
```

For versioned disk/process result structs, use their `*_INFO_INIT` macros. Buffer-based text queries report the required capacity including NUL. Scalar and structured outputs retain their prior values on failure.

CPU counts describe online system CPUs, across Windows processor groups, not the current process affinity mask or a cgroup CPU quota. Linux physical counts deduplicate online package/core pairs in sysfs; unavailable topology is NOT_SUPPORTED, never a logical-core count presented as physical cores.

Available RAM is Windows available physical memory, Linux `MemAvailable`, or macOS free plus inactive pages. The macOS value is an estimate, and these snapshots do not promise that a subsequent allocation succeeds. Linux queries describe the OS view, not a container-specific memory limit. Disk available space uses POSIX `f_bavail`, distinct from total free blocks `f_bfree`.

Current-process resident bytes are the working set/RSS. Virtual bytes describe reserved plus committed virtual address space on Windows, `/proc/self/statm` size on Linux, and Mach virtual size on macOS; they are not commit charge or physical memory consumption. Native resource queries can block.

`hx_os_cpu_usage_create` takes an initial process CPU and monotonic sample. Each successful query returns the interval average since the previous sample: `100 * process_cpu_delta / elapsed_delta / initial_logical_cpu_count`. Children are excluded. Callers serialize access to each sampler; samplers are independent and allocate through their context. A zero-length interval returns WOULD_BLOCK without advancing the sample. OS counter granularity and changes in CPU topology can affect short samples. The sampler does not clamp results.

```cpp
#include <hx/SystemInfo.hpp>

hx::Result<double> sample_cpu() noexcept
{
	auto cpu = hx::CpuUsage::create();
	if (!cpu)
		return hx::Failure(cpu.error());

	hx_os_error error = {};

	auto status = hx_os_sleep_until(hx_os_deadline_after_ms(10), &error);
	if (status != HX_OS_OK)
		return hx::Failure(status, error);

	return cpu.value().query();
}
```

C++ convenience methods return Result<T> with copied native error details. CpuUsage::create returns Result<CpuUsage>; the sampler is move-only. These headers require C++11 and support exceptions disabled; the core remains C99. See [Result contracts](CPP_RESULTS.md) and examples/system_info.c.

## Dynamic libraries

`HX_OS_ENABLE_LIBRARY` defaults to ON. `hx_os_library_open`, `symbol` and `close` provide the basic C interface; `_ex` variants also accept an initialized `hx_string` for owned diagnostic text. `hx::Library` supplies movable ownership and `symbol<Pointer>(name)` through Result; failures retain the original status/domain/code. An optional caller-owned String receives loader diagnostic text.

```cpp
#include <hx/Library.hpp>

hx::Result<uint32_t> plugin_abi(const char *path) noexcept
{
	auto plugin = hx::Library::open(path);
	if (!plugin)
		return hx::Failure(plugin.error());

	using Abi = uint32_t(HX_OS_CALL *)(void);

	auto symbol = plugin.value().symbol<Abi>("hx_plugin_abi_version");
	if (!symbol)
		return hx::Failure(symbol.error());

	return symbol.value()();
}
```

Paths are UTF-8 filesystem paths; a relative filename is resolved against the current directory rather than searched as a bare POSIX soname. Dependencies still follow the native loader policy. POSIX uses RTLD_NOW and LOCAL/GLOBAL; Windows ignores the visibility flag. No function changes loader search paths. The POSIX loader has text errors instead of a portable numeric error code, so the main error uses HX_OS_ERROR_LOADER and the optional diagnostic captures the text. It copies up to 2047 native bytes before any allocator callback can reenter the loader, marks truncation and escapes non-UTF-8 bytes when necessary. Diagnostic allocation failure never replaces the primary operation error.

Serialize each handle with its close. A symbol borrows module storage, and NULL can be a successful POSIX data-symbol address. Stop calls, release plugin objects, disconnect callbacks and finish plugin TLS destructors before unload. Close consumes the handle even on native failure; an explicit C++ close can report errors, whereas the destructor cannot report its status. Loading/unloading may run module constructors/destructors and can block.

`examples/load_plugin.c` loads the test module, negotiates ABI using the library's ABI query, and reads the plugin version. CTest builds this fixture itself; the example needs no preinstalled plugin or external process.

C callers can use `hx_os_library_bind` or `_bind_ex` to write a native function pointer directly into a typed variable, passing its address and `sizeof(variable)`. This avoids repeating pointer conversion code. The destination stays unchanged on failure.

## File and path conveniences

`HX_OS_ENABLE_FILE_UTILS` defaults to ON. Read text directly with `hx_os_file_read_text(path, &text, &error)` and publish an existing `hx_string` with `hx_os_file_write_text(path, &text, &error)`. Reading is strict UTF-8, removes an initial UTF-8 BOM and limits the entire input to 16 MiB. The explicit load APIs accept a different bound/BOM policy. Invalid input, exhausted limits, allocation failure and I/O failure leave an existing output string unchanged. Binary loads own their returned memory, including an empty allocation for an empty file; release it with `hx_os_memory_free`.

Atomic saving creates an exclusive temporary in the target directory, writes all bytes, requests file persistence, closes and renames it over the target. It replaces the directory entry (including a destination link) and can change file identity/metadata. The extended result distinguishes publication from parent-directory synchronization. A successful publication is not a guarantee against power loss. Windows directory synchronization returns NOT_SUPPORTED in the result, without turning completed publication into failure. Failed staging attempts remove their temporary where possible; permissions or external mutation can prevent that cleanup.

Copy rejects destructive copies onto the same file and does not promise metadata preservation or a snapshot of a concurrently changing source. Plain save/copy can leave partial output on failure. Explicit backup is a copy, without a persistence guarantee. Replace with backup uses native replacement on Windows and two renames on POSIX. Existing backup names are rejected; the caller excludes concurrent changes to all participating paths. There is no automatic rollback if a later step fails. The POSIX path deliberately uses ordinary rename after the collision check, including on WSL mounts that do not support no-replace rename.

Current/absolute paths have buffer and `hx_string` variants. Absolute resolution requires an existing target and resolves links. Changing cwd is explicitly process-global: callers coordinate all relative-path operations. Lexical basename/dirname/extension views are allocation-free, recognize both separator styles, and remain available with FILE_UTILS disabled. An extension includes its dot; a leading-only dot does not count.

Glob returns an owned vector of owning `hx_string` values; destroy it with `hx_os_sequence_destroy`. Matching supports UTF-8 `*`/`?`, with `?` consuming a codepoint, and is case-sensitive within matched components. Literal directory prefixes follow native path lookup. No character classes/escaping or recursive `**` expansion are provided. Dot names require a literal leading dot. Empty matches succeed; order is unspecified. It needs CONTAINERS as well as FILE_UTILS.

Walk defaults are depth 64 (maximum configurable 256), one million visited entries, 65,536 matches, and separate 16 MiB budgets for vector storage and retained text. Traversal frames are also bounded; native directory buffers are outside allocator accounting. Recursive removal does not follow links, uses heap traversal frames and can partially complete before a limit or failure. POSIX traversal opens children relative to directory descriptors with NOFOLLOW; Windows opens reparse points themselves and holds directories against rename. Concurrent external mutation remains a caller coordination boundary.

```cpp
#include <hx/Files.hpp>
#include <cstdio>

hx::Result<void> list_directory(const char *path) noexcept
{
	auto directory = hx::Directory::open(path);
	if (!directory)
		return hx::Failure(directory.error());

	for (;;) {
		auto entry = directory.value().next();
		if (entry.status() == HX_OS_EOF)
			return hx::Result<void>();

		if (!entry)
			return hx::Failure(entry.error());

		std::puts(entry.value().name());
	}
}
```

File::open and Directory::open return move-only core owners, including with FILE_UTILS disabled. File::get borrows its native handle. Serialize cursor operations. readSome/readAt return Result<ReadResult>; EOF and failures can retain byte progress through hasValue(). Files convenience methods return Result<String> or Result<Buffer>. Directory::next(true) also queries entry kind; default next() requires no optional file-info capability.

`examples/text_file.c` reads UTF-8 in one call and optionally atomically publishes it to a second path; CTest runs the same C source on both hosts.

`File::read` additionally treats EOF as successful `ReadResult{bytes, eof}`; errors still retain progress. `hx_os_directory_visit` and `Directory::forEach` visit directly with one reusable bounded name buffer and guaranteed close; EOF/explicit stop is success. See [visitor and text-view contracts](COMMON_UTILITIES.md).

## Environment

HX_OS_ENABLE_ENV defaults ON. hx_os_env_get_string uses the output string's context. hx::Environment::get/getOr return Result<String> and accept an optional output context. Fallback applies only to a missing name; an empty value remains empty. Buffer queries include NUL in required capacity.

`hx_os_env_set(name, value, replace, error)` explicitly changes the process environment; replace=0 leaves even an empty existing value intact. Removing a missing name succeeds. Inputs are borrowed during the call, names cannot be empty or contain '=', and invalid UTF-8 is rejected. Values are copied by the OS. Changes affect future children, not the parent or already-running children.

Calls through one loaded library instance serialize. Native/CRT/another copy of the library must not mutate the environment concurrently. Output allocator callbacks run outside this lock and may call the getters. String capacity retries are limited to eight attempts; continual growth returns BUSY while preserving the old output. Windows uses the Win32 Unicode environment; CRT `getenv`/`_environ` caches are separate and may not observe Win32 changes. Use these getters and setters together. POSIX native non-UTF-8 values report INVALID_ENCODING instead of changing the locale or guessing an encoding.

```cpp
#include <hx/Environment.hpp>

hx::Result<hx::String> application_mode() noexcept
{
	return hx::Environment::getOr("APP_MODE", "normal");
}
```

`examples/environment.c` queries a named variable directly. Its CTest supplies a private test value in the child environment and does not edit host settings.

## Special paths

With PATHS enabled (default), use `hx_os_path_config_home_string(&path, &error)` or `hx::Paths::configHome()`. All seven named queries have buffer and owning-string forms. No global initialization, directory creation or cwd change is performed. Paths are absolute UTF-8 without trailing separators except roots. They need not exist or be writable. Required size includes NUL; string failure preserves its old value and allocator.

| Query | Windows | Linux | macOS |
| --- | --- | --- | --- |
| home | Absolute USERPROFILE, else Profile folder | Absolute HOME, else current UID database | Same POSIX home lookup |
| config_home | RoamingAppData | Absolute XDG_CONFIG_HOME, else HOME/.config | HOME/Library/Application Support |
| data_home | LocalAppData | Absolute XDG_DATA_HOME, else HOME/.local/share | HOME/Library/Application Support |
| program_data | ProgramData | /var/lib | /Library/Application Support |
| temp | GetTempPathW | Absolute TMPDIR, else /tmp | Absolute TMPDIR, else Darwin user temp, then /tmp |
| executable | GetModuleFileNameW | /proc/self/exe | _NSGetExecutablePath + realpath |
| module | This library's directory | Absolute dladdr module directory | Absolute dladdr module directory |

`program_data` is machine-wide application data/state, separate from user data and read-only installed resources. Append your application subdirectory and explicitly create it when needed. Empty/relative XDG values are ignored per the [XDG specification](https://specifications.freedesktop.org/basedir/latest/). Invalid UTF-8 remains an error. Windows uses SHGetKnownFolderPath with KF_FLAG_DONT_VERIFY and CoTaskMemFree; tests invoke it without CoInitialize. No Objective-C or new third-party dependency is needed.

`module` means the executable directory for static linkage. POSIX loaders that report only a relative module name return NOT_SUPPORTED, avoiding incorrect resolution after cwd changes. Linux executable lookup requires procfs and preserves native failure. Renamed/deleted images may yield their recorded name, not an existing path. POSIX system path scratch is bounded to 1 MiB. Queries may block in native folder providers or the UID database.

PATHS works independently of ENV, FILE_UTILS and LIBRARY. Private environment reading shares its lock/codec while public ENV symbols still report unsupported when disabled. The host coordinates native environment/cwd mutation. No hidden thread or hot-path lock is added; explicit queries allocate native/string scratch. `examples/special_paths.c` demonstrates the same direct C call on both hosts.

## Thread options, names and semaphores

THREAD_EX defaults to ON and adds no background threads. Use `hx_os_thread_create_with_options` with `HX_OS_THREAD_OPTIONS_INIT` and an optional byte-sized stack reservation (zero uses native defaults). The explicit initializer receives the caller's capacity and writes only the known prefix; creation validates size/version before reading the stack field. POSIX rejects sizes below its native minimum or alignment requirement. Windows retains _beginthreadex for CRT cleanup and uses STACK_SIZE_PARAM_IS_A_RESERVATION; values beyond its unsigned-size parameter return RANGE. Existing create uses the same default native path and does not add an allocation or lock.

Names are strict UTF-8. Linux and macOS truncate at a complete codepoint within 15 and 63 bytes respectively; macOS cannot name another thread. Windows uses runtime lookup of SetThreadDescription in KernelBase, as required for early Windows 10 versions; unavailable implementations (including Win7) return NOT_SUPPORTED. Native failures retain errno/HRESULT. Exclude concurrent join and destroy when naming an object. A joined thread returns CLOSED.

The semaphore is a mutex/condition-based bounded token count. post(count>0) retains tokens without waiters and atomically rejects overflow with LIMIT. wait_until consumes one available token even at NOW, otherwise waits against an absolute monotonic deadline. Timeout retains the object; INFINITE is explicitly allowed. No FIFO or hard return deadline is promised. Destroy only after excluding operations and waiters. Creation uses three context allocations; post/wait allocate nothing and take one mutex. It has no worker or queue.

`hx::Semaphore` owns its synchronous resource with movable RAII. `hx::Thread` uses the same explicit owner-variable facade as ThreadPool: closeUntil joins and destroys on success, retains ownership on timeout, and never waits or terminates from its destructor. Callback data remains borrowed until join. `examples/thread_options.c` shows stack selection and an explicit close policy; small embedded stacks must account for callbacks, TLS and sanitizer overhead.

## Pipes and local I/O loops

`hx_os_pipe_pair_anonymous` creates two synchronous file endpoints. Use the normal file read/write/close calls. Both platforms use blocking byte streams; no waiter thread is created. Disk-only operations report NOT_SUPPORTED. A zero byte operation succeeds; after all writers close, buffered bytes are consumed before EOF. Broken writes report CLOSED with the native code. Destruction must not race a synchronous call. Linux uses pipe2 with CLOEXEC; macOS sets CLOEXEC with fcntl, so the host must serialize concurrent fork/exec during creation.

For deadlines and cancellation use `hx_os_pipe_pair_async` and `hx_os_pipe_read_async` / `hx_os_pipe_write_async`. Writes copy by default and complete all bytes; extended submit can borrow. Reads complete with available bytes or EOF. The new pipe operation has a capacity-aware initializer and does not change hx_os_operation. Bytes are raw, without automatic text decoding. `hx_os_pipe_pair_child` creates an async parent and a synchronous child endpoint in either direction, initially non-inheritable; process spawning later chooses what to duplicate into the child.

`hx_os_loop_create_local(NULL, NULL, &loop, &error)` needs no network object or network initialization. It accepts posts, timers and local I/O; network/DNS operations report NOT_SUPPORTED. The original network factory remains valid. LOOP and NETWORK are independently selectable. PIPE without LOOP still supports synchronous endpoints; ASYNC_PIPE capability requires both PIPE and LOOP.

One request per pipe endpoint is accepted, on the loop creator thread. Pipe requests share the loop's max_requests and max_pending_bytes, including borrowed buffer sizes. Rejection retains nothing and invokes no callback. Completion is exactly once on the poller, never inline. Request waiting includes callback return; it does not drive the loop. POSIX uses nonblocking descriptors and the same epoll/kqueue registrations as sockets. Windows uses private, one-instance, byte-mode named pipes with overlapped parent endpoints and IOCP; anonymous CreatePipe handles remain synchronous. No hard deadline is promised.

close_until marks the endpoint closing, requests cancellation and drives native retirement. Timeout retains the pointer, native state and buffers for retry. Other loop callbacks can run during close; waiting for unfinished work in a callback or reentering an active close returns DEADLOCK. Close attached pipes before destroying their loop. Final native close consumes the endpoint even on a native close error; never retry that consumed pointer. Closing the loop cancels requests but does not implicitly free attached endpoints.

POSIX pipe writes temporarily block SIGPIPE only in the calling thread, preserve previously pending SIGPIPE, consume a newly generated broken-write signal and restore the original mask. No process-level handler is installed. The host must not concurrently consume this thread's pending SIGPIPE through another signal-waiting mechanism. Cancellation can report native partial byte progress; it does not promise that a failed/cancelled write delivered zero bytes.

C++11 Pipes::anonymous returns Result<pair<File, File>>. IoLoop and AsyncPipe preserve host-owned variables for retryable close. IoRequest owns an observation reference; dropping it does not cancel native work. Read/write buffers are borrowed through callback return, including StringView writes. Typed captures use the loop context, with one separate state allocation per submission and no allocation per callback. Accepted request count bounds active callback states; retained facade references can keep diagnostic state alive. Callbacks return void, a status or Result<void>; callbackResult() reports their outcome after completion. C entry points do not allocate this C++ state.

`examples/pipe_transfer.c` performs a complete transfer with a local loop and explicit shutdown. Existing regular-file I/O adds a pipe-kind branch, without new allocations or locks. Socket poll records now share a two-int readiness record; no extra socket allocation/syscall is introduced. Async pipes reuse request allocation/queue locking; default writes add their owned copy. POSIX writes additionally perform thread signal-mask/pending-signal operations.

## Socket options and positioned file I/O

NET_OPTIONS defaults to ON and requires NETWORK for its capability. Call hx_os_socket_set_nodelay, set_keepalive or set_reuseaddr directly with a boolean. These select the correct native levels/names internally. Address reuse stays opt-in: create and bind do not enable it. Native reuse semantics and keepalive scheduling remain platform-specific. Setters return BUSY once a socket belongs to a loop, even between requests. The borrowed `hx::SocketOptions` facade never closes a socket; it provides these setters and peerAddress().

The raw option_set/get escape hatch uses target-native level/name/value layouts from the target SDK. Length is bytes, capped at INT_MAX; get returns the actual native length on success and does not implement two-phase sizing. Failure keeps the input length but the native buffer may have changed. In the tested Windows provider TCP_NODELAY returned one byte even with an int-sized buffer, so callers must inspect the returned length. C++ get<T> requires an exact size match and reports RANGE otherwise; it cannot infer arbitrary native option value types. Peer lookup preserves output on failure; an unconnected socket retains the native getpeername error. Serialize calls with socket use and exclude close.

POSITIONED_IO is independent of FILE_UTILS. `hx_os_file_read_at` and write_at accept an absolute byte offset and return a single chunk plus exact native progress. Short successful I/O is distinct from EOF; zero requested bytes succeeds. Requests are capped at INT_MAX, offsets/ranges must fit signed 64-bit native positions. Callers handle additional chunks/retries. Append-mode writes are NOT_SUPPORTED, and wrong open-mode access reports ACCESS_DENIED. Pipes are not seekable. These calls may block and have no cancellation/deadline promise.

POSIX uses pread/pwrite with EINTR retry. Windows lazily reopens the existing file object with OVERLAPPED using ReOpenFile, caches that handle with native interlocked publication, and uses a distinct event/OVERLAPPED for each call. It never seeks/restores the shared cursor and never upgrades the original open mode. All overlapping operations retain the file until completion; the owner excludes close/truncate and coordinates overlapping writes. Final file close releases both handles. `hx::File::readAt` distinguishes EOF and `writeAt` returns partial progress; Result::hasValue() preserves progress on failure.

Regular open/read/write add no allocation or lock for the positioned feature. The Windows private file object grows to 24 bytes on x64 (16 on x86), POSIX to 12 bytes. First positioned use can open competing candidate handles; only the published handle remains. Each positioned Windows call has atomic cache lookup, one event create/close, native I/O and a completion wait when needed. POSIX adds only its positioned syscall. This is a cost inventory, not a throughput claim.

`examples/positioned_read.c` reads four bytes at offset four without a seek. The Windows pipe adapter also now supplies retained non-NULL byte-count storage to ReadFile/WriteFile, required by the documented Windows 7 contract; final async progress still comes from IOCP completion. Win7 native execution is pending.

## File information, directory kinds and BOM

FILE_INFO (default ON) adds hx_os_path_stat_extended and hx_os_directory_next_info. Initialize the extended output with HX_OS_FILE_INFO_EXTENDED_INIT or the capacity-aware initializer. Existing file information layouts and directory-next signatures remain unchanged. Query failure preserves the extended output; success writes only the known structure. Follow mode is explicit and names remain UTF-8. The native projection is shared with the old stat path, avoiding duplicate filesystem logic.

Timestamps are signed Unix seconds with nanoseconds in [0, 1e9). Metadata change time uses POSIX ctime or Windows FILE_BASIC_INFO.ChangeTime, not Windows creation time. Resolution, update deferral and access-time suppression follow the filesystem. Windows queries both metadata records through one open file handle; concurrent mutation can still change fields between native calls. read_only is the Windows attribute or absence of the POSIX owner-write mode bit. POSIX reports known owner-read/write mode bits; Windows marks both unknown instead of guessing ACL permissions. These summaries are not an authorization check, nor a statement about read-only mounts. Windows ignores its read-only attribute on directories.

Directory info classifies the entry itself, without following links. Windows reparse points report LINK; POSIX uses d_type when known and otherwise fstatat relative to the open directory. Concurrently removed entries may produce a native lookup failure. OUTPUT_FULL and native lookup failure retain the pending entry/kind output; retry with capacity or use legacy next to skip a stale entry. Invalid UTF-8 entries retain the existing skip-and-error behavior. Enumeration order and snapshot consistency are not promised. The legacy call adds no type lookup syscall. C++ Files::info and Directory::next(true) provide direct owned results. examples/file_info.c queries metadata in one function call.

BOM is an independent default-on pure utility. hx_os_text_bom_detect recognizes the longest complete UTF-8/16LE/16BE/32LE/32BE marker, checking UTF-32 before the shared UTF-16 prefix. No marker returns NOT_FOUND and prefix_bytes=0 while keeping encoding unchanged. This is not streaming encoding detection: a two-byte FF FE buffer is a complete UTF-16LE marker; obtain four bytes when identifying a possible UTF-32 stream. Detection does not validate or convert the payload. hx_os_text_bom_bytes generates at most four bytes without a terminator; encodings without a supported BOM report NOT_SUPPORTED. C++ Text::detectBom exposes found, bytes and encoding; encoding is valid only when found is true. The byte generator returns an Result<Buffer>. The core codec still adds/removes no BOM implicitly.

The C BOM functions allocate nothing and take no locks. Stat and directory info create no background activity; native metadata queries may block. POSIX known directory types add no stat; unknown types require one fstatat. Windows extended stat adds one FileBasicInfo query, but shares the existing handle and projection.

## Configuration serialization and numeric text

CONFIG_SERIALIZE defaults to ON and requires CONFIG. Use hx_os_config_serialize(document, format, &text, NULL, &error) or the one-call hx_os_config_save(document, format, path, NULL, &error). Save also needs FILE_UTILS and follows atomic publication's persistence/partial-failure contract. Documents remain immutable. The serializer always uses its builtin writer, regardless of the document's parse backend; explicit writer options must select BUILTIN. All seven formats have output APIs. YAML uses block collections, XML preserves its element/attribute/text model, TOML uses root assignments and inline containers, and ENV writes flat quoted strings. See [serialization and object mapping](SERIALIZATION.md) for representability and examples.

JSON emits compact UTF-8, preserving valid JSON numeric spellings without conversion through double. TOML integer bases are converted exactly and decimal underscores/leading plus are removed; non-finite numbers, XML nodes and datetime nodes cannot silently become JSON strings. INI supports root string values plus one level of sections containing strings. It emits global keys before sections, preserving order within those groups; comments/whitespace are not retained. Keys/values that the INI parser would trim or misinterpret are UNREPRESENTABLE. Properties supports a flat object of strings, escaping separators, spaces, backslashes and control characters, while retaining Unicode as UTF-8. Numbers, bools, arrays, null and nested structures cannot silently become line-format strings. Unsupported representations leave the previous output unchanged.

Serialization performs a bounded measurement pass followed by one output allocation and a write pass. Both traversals charge max_work; each obeys max_nodes/max_depth (maximum 128 recursive frames). max_input_bytes is the serialized output limit, and max_memory_bytes bounds its allocation including allocator overhead. Caller-owned document/old output and the bounded C stack are outside that heap budget. Output uses its hx_string context. Allocator failure or a limit during either pass preserves the previous output. Concurrent readers use independent outputs and keep the document alive. No locks, global mutable settings or background work are introduced in serialization.

TEXT_NUMBER separately exposes parse_double and format_double and can work without CONFIG. The config getters, numeric grammar and third-party adapters share the same first-party helpers; their existing behavior is retained even when the public text module is off. Parsing accepts a complete ASCII token in the existing numeric grammar (including TOML underscores/bases and signed inf/nan), without whitespace or embedded NUL; overflow/underflow is RANGE. Formatting uses 17 significant digits for IEEE binary64, canonical nan/inf, and C numeric locale. It never changes the process locale; POSIX temporarily selects and restores the calling thread's locale. The locale objects use native C-runtime allocation, outside injected allocator accounting, as the original config getter did.

C++ ConfigDocument owns an immutable reference with copy/move support. parse/load, typed queries, serialize/save and missing-only stringOr return Result. Optional hx_os_config_error output retains parser position/message details. Text conversion and serialization return Result<String>, with an optional context. examples/config_save.c preserves exact large integers.

`ConfigDocument::stringView(pointer)` returns Result<StringView> without copying bytes. Keep the immutable document alive; querying an owning temporary is rejected. The existing string/stringOr methods return independent owned copies.

## Periodic timers

TIMER is default-on and depends on LOOP, independently of NETWORK. hx_os_timer_create(loop, interval_ns, callback, user, &timer, &error) starts a periodic schedule one positive interval after creation. The creator must drive the loop. Intervals use monotonic nanoseconds, without a resolution or hard real-time guarantee. Scheduling stays on the original time axis; missed periods, including time spent in a callback, coalesce into one tick per poll. The legacy one-shot operation and its public structure are unchanged.

Callbacks run outside locks on the poller. The error pointer is borrowed until return: OK means a tick, a terminal clock/range error means the timer stops. Stop is a thread-safe request; no extra cancellation tick is delivered. wait_until includes the final callback and optional cleanup returning. An unfinished owner-thread wait is DEADLOCK. close_until is owner-only and pumps cancellation; TIMEOUT retains the same object for retry. Once retired, close consumes the object even when reporting its terminal error. Native clock failure after a tick may require a separate terminal error notification.

The cleanup factory transfers responsibility only on acceptance. It runs once outside locks, before retirement is visible. A callback/cleanup cannot close itself synchronously (DEADLOCK); it may request stop. Keep borrowed user state alive through retirement. Other threads must finish using the timer before its owner closes it. Loop close cancels timers, but loop destruction remains BUSY until their objects are explicitly closed.

Each active timer holds one existing loop request slot. It adds one small object at creation, with no allocation, thread creation or queue growth per tick. Due polling reads the monotonic clock and invokes a direct C callback; the normal request path adds a private timer pointer and a branch. Timer size/ alignment is 40/8 on Windows x86, 64/8 on Windows/Linux x64. Existing public layouts are unchanged. This resource inventory is not a runtime speed claim.

C++ PeriodicTimer::every returns Result<PeriodicTimer> and references a host-owned native pointer. closeUntil is explicit. A failed callback result requests stop; callbackResult() reports it after retirement. Cleanup releases typed captures even while the facade retains diagnostics. wait/close preserve native terminal errors. See examples/periodic_timer.c.

## Pool worker identity

hx_os_thread_pool_inside(pool, &inside) reports whether the calling thread is one of that live pool's workers, including its task cleanup callback. Invalid arguments and disabled pools report INVALID/NOT_SUPPORTED without changing the output. Keep the pool alive during the call; this does not acquire ownership. It reads the pool's existing thread-local identity: no lock, allocation, syscall or additional worker bookkeeping. The C++ facade exposes pool.inside().

A worker waiting for an unfinished task from its own pool gets DEADLOCK, including zero-deadline waits; waiting for an already finished task is permitted. Closing one's own pool also reports DEADLOCK. False for another pool does not prove that waiting there is safe: cross-pool dependency cycles remain the caller's concern. No work stealing, inline task execution or dependency graph is added.

```c
int inside;

hx_os_status status = hx_os_thread_pool_inside(pool, &inside);
if (status == HX_OS_OK && !inside)
	status = hx_os_task_wait_until(task, deadline, &work_status, &error);
/* Inside a worker, continue asynchronously instead of waiting for new work. */
```

## Native error text

hx_os_error_message now appends a strict UTF-8 diagnostic for GAI errors and, on Apple, Mach errors. The existing status/domain/code prefix and all other domain behavior are preserved. Unavailable, oversized, non-UTF-8 or out-of-int native messages keep the numeric fallback. cause_code remains available in the original error object; formatting never modifies it. required includes NUL; a short buffer is terminated when capacity is nonzero, but partial output need not end on a UTF-8 boundary. Use the reported capacity for the complete message.

POSIX uses gai_strerror; Apple additionally uses mach_error_string. Windows uses FormatMessageW into call-local storage followed by strict UTF-8 conversion, without WSAStartup, working even with NETWORK disabled. Microsoft's [gai_strerrorW documentation](https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-gai_strerrorw) warns about shared, non-thread-safe storage. Diagnostic wording is native and locale dependent; do not match it for control flow or concurrently change the process locale. There is no new first-party heap allocation or lock in C. C++ Result::error() retains the native record; pass it to hx_os_error_message for caller-owned diagnostic text. The hello C example now uses the detailed diagnostic on failure.

## Randomness, UUID, CRC32 and secure clearing

RANDOM defaults to ON. hx_os_random_bytes fills caller storage using the system cryptographic generator; it never substitutes a time-based or ordinary PRNG. It may block for native entropy and has no deadline/cancellation promise. NULL is valid only for zero bytes. Native failure clears the entire output; invalid arguments or disabled-module rejection leave it unchanged. No library context, heap allocation or background thread is needed; OS providers can allocate.

Windows resolves BCryptGenRandom from an absolute system-directory DLL path, using CryptGenRandom only when that entry is unavailable. Each call releases its module/provider; there is no hidden process-lifetime cache. CNG failures retain their signed code in the new appended NTSTATUS domain (numeric diagnostic), and legacy failures retain Win32 details. advapi32 is a private native dependency, also listed for static pkg-config consumers. Linux uses the getrandom syscall, retrying short reads/EINTR; only ENOSYS or missing syscall headers take the legacy path. That path first reads 32 bytes from /dev/random, then /dev/urandom, checks the expected character-device identities and closes both descriptors. It can block indefinitely on an entropy-starved old embedded kernel; denied access is an error. It uses a private device adapter because public file_open intentionally accepts regular files. Apple uses arc4random_buf; native target runs are pending. [Linux random-device semantics](https://www.man7.org/linux/man-pages/man4/random.4.html) explain why early-boot /dev/urandom alone is insufficient for this fallback.

UUID generation sets v4/version and variant bits and preserves the output on failure. Formatting is lowercase 8-4-4-4-12 plus NUL, accepts overlapping input, and is available with RANDOM disabled. hx_os_uuid_generate_string(&text, &error) combines generation and formatting using the output string's own context; allocation failure also preserves the old string. C++ Random::uuid returns an Result<String>, with fill/bytes for raw output. No collision guarantee or entropy certification is inferred from tests. See examples/random_uuid.c.

CRC32 is always available: reflected CRC-32/ISO-HDLC, polynomial 0xedb88320, initial/final all-ones xor. Seed zero starts a stream; prior output continues it. The fixed 16-entry table occupies 64 read-only bytes with two lookups per byte; there is no lazy initialization, allocation or lock. It detects accidental corruption and is not a cryptographic integrity check. secure_zero uses SecureZeroMemory, detected explicit_bzero on Linux, or volatile byte writes. It prevents elimination of these writes, not residual copies/registers/swap, and provides no synchronization. Neither utility introduces global configuration.

## Pages and file mappings

VIRTUAL_MEMORY defaults to ON, independently of SYSINFO and FILE_UTILS. hx_os_pages_alloc rounds a positive byte count to the native page size and owns a zero-initialized anonymous mapping plus context-allocated metadata. NONE, READ, READ|WRITE, READ|EXECUTE and READ|WRITE|EXECUTE are supported requests; WRITE or EXECUTE without READ is NOT_SUPPORTED. Native policy may reject executable pages; the library does not silently weaken protections. Whole-range protect includes instruction-cache synchronization when enabling EXECUTE. Protection failure can leave native partial changes; restore known permissions before use. OS/hardware permissions are not a security-isolation guarantee. Callers exclude concurrent access during protection changes and destruction.

hx_os_file_map_open(path, mode, &map, &error) maps a whole regular UTF-8-named file in one call. create additionally accepts context, open file, byte offset and length (zero means through EOF). Empty ranges are EOF, oversize ranges are RANGE; the file is never extended. The implementation aligns the native offset and returns the requested subview. File cursor is unchanged. Every mapping owns a duplicate native file resource (plus a Windows mapping object), so the caller may close its original file after create. Creation still requires exclusion from closing/truncating that file.

READ provides a const borrowed view; write_view returns NULL. SHARED_WRITE needs read/write file access and rejects APPEND. PRIVATE_COPY permits private writes through a read-only file handle and never writes those pages back. Neither mode is a snapshot; other file mutations and concurrent truncation must be coordinated. A page fault caused by truncation or storage failure can become SIGBUS or a native exception outside a C status return. Remote filesystem and mixed mapped/ordinary I/O coherence follow the native system, not an added library guarantee.

Only SHARED_WRITE can flush: writeback uses msync(MS_SYNC)/FlushViewOfFile; durable=1 also requests fsync/FlushFileBuffers on the owned resource. Success is not proof of hardware durability; failure leaves the map usable for retry. Destroy does not implicitly sync. An unmap failure retains the object; after unmap it is consumed even if native handle close reports an error. Pages free similarly retains its object on failure. Borrowed views cannot outlive the object. See [Windows flush semantics](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-flushviewoffile) and [POSIX/Linux mapping behavior](https://www.man7.org/linux/man-pages/man2/mmap.2.html).

C++ Pages and MappedFile provide move-only synchronous RAII. Call close explicitly to observe native release errors; destructors cannot report them and never issue implicit flush. C mapping convenience is demonstrated in examples/mapped_file.c. Each object adds one metadata allocation; actual pages are OS-managed and outside context heap accounting. Map creation adds duplication and mapping system APIs, with no shared library lock, background work or access-time wrapper. Layout (size/alignment): Windows x86 pages 8/4, map 28/4; Windows x64 16/8, 56/8; Linux x64 16/8, 40/8. Existing public structures remain unchanged.

## File range locks, timestamps and permission summaries

FILE_EXTRA defaults to ON and needs neither FILE_INFO nor FILE_UTILS. lock_at(file, offset, bytes, shared, deadline, error) attempts a positive byte range without moving the cursor; its signed-64-bit end must be representable. Shared locks require READ, exclusive locks WRITE. NOW attempts once; conflicts then become TIMEOUT. Finite/infinite waits use monotonic deadlines and native nonblocking lock attempts with requested sleep increments up to 10 ms. Native calls/scheduling can overrun a deadline. Failure retains the file, with native conflict detail on TIMEOUT. Unlock matches a successfully acquired range. There is no library allocation, mutex or worker for lock/unlock.

POSIX fcntl record locks are process-owned and advisory; closing any descriptor for the inode, including a mapping's duplicate, can drop the process's locks. Windows LockFileEx locks belong to the handle and affect ordinary conflicting I/O, but not mapped views. Its positioned I/O adapter uses a different handle, so use cursor I/O through the locking handle while the region is held. Do not use these locks as thread mutexes, overlap/upgrade/reenter ranges, or assume fairness. See [POSIX/Linux locking](https://www.man7.org/linux/man-pages/man2/fcntl_locking.2.html) and [Windows range locks](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex). C++ FileRangeLock borrows File through unlock/destruction, uses NOW by default, and transfers a lock only by move construction. Explicit unlock reports errors.

Initialize file times with HX_OS_FILE_TIMES_INIT or the capacity-checked init. fields selects access/modify seconds plus nanoseconds since Unix epoch; omitted fields are unchanged. Zero fields is a no-op, not an existence/access check. Nanoseconds outside [0,1e9), unsupported native time ranges or bad size/version are errors. POSIX uses utimensat with UTIME_OMIT and explicit nofollow; Windows uses a wide-path attribute handle and SetFileTime, truncating to 100 ns. Native filesystem precision can be coarser. Creation/metadata-change time is not set. Windows cannot represent dates before 1601; 32-bit time_t targets may reject otherwise valid 64-bit times. Updates can already be applied if close fails.

Permission changes replace POSIX owner read/write/execute bits while preserving group/other mode. Concurrent path replacement and permission changes need caller exclusion. Nofollow must be supported or fail, never silently follow a symlink. On Windows READ must be present and EXECUTE is unsupported; WRITE only controls the read-only attribute. No ACL editing/effective-access guarantee is provided. The existing v1 stat permission summary remains read/write only. C++ FileAttributes::setTimes/permissions and examples/file_attributes.c offer direct calls. Windows path conversion/attribute handles are transient; POSIX uses native metadata calls without injected heap allocation. The new public times structure is 40/8 bytes on Windows x86/x64 and Linux x64; old layouts are intact.

## Directory monitoring and external descriptor readiness

WATCH defaults to ON, independently of LOOP/NETWORK/FILE_UTILS. One hx_os_watch_create(context, path, recursive, callback, user, &watch, &error) starts one joinable worker and waits for its subscription to be armed. No worker starts merely by linking/enabling the module. The output pointer is assigned before callbacks can begin, although a callback may run before create returns. User callbacks run on that worker outside locks; stop can be requested there. close_until stops, joins, includes cleanup/TLS return, and consumes a retired watch even when reporting a terminal backend error. TIMEOUT retains the same object for retry; self-close is DEADLOCK. Exclude concurrent destruction/users. Acceptance transfers optional cleanup exactly once; rejection transfers nothing.

Events borrow strict UTF-8 relative paths with slash separators through callback return. Empty path is root. CHANGED/ADDED/REMOVED/RENAMED are best effort, without transaction order or an exact audit history. old_path may be NULL; Linux reports observed rename paths when the pair is not assembled, Windows pairs adjacent old/new names in one buffer, Apple supplies observed paths. DIRECTORY is positive evidence only (Windows notifications do not provide this type). ERROR ends the watch; OVERFLOW means reconcile a fresh directory snapshot, not just retry the last event. Invalid native filename encoding also reports overflow/error detail.

Defaults: 64 KiB native input buffer, 32 KiB maximum path storage, 1024 software watch records, depth 64 and 4 MiB retained variable-storage budget. Allocation headers and transient old/new copies during growth/rebuild count against that budget. Fixed thread/event controls, native thread stacks, bounded temporary Windows path conversion and OS/CF-managed storage are outside it. Path scratch and tree records grow as needed; there is no library event queue. Slow callbacks can overflow native buffers. The directory/depth bounds apply to software scans on Linux; Windows/Apple tree subscriptions use one native source.

Windows uses overlapped ReadDirectoryChangesW and private stop/completion events. Cancellation retires the native operation on the worker before buffer release. Linux uses inotify/eventfd, openat with O_NOFOLLOW and /proc/self/fd to pin each watched directory. Missing procfs, duplicate inode aliases and 9p return NOT_SUPPORTED. Topology changes rebuild bounded recursive coverage, then emit OVERFLOW because notifications can be lost during the scan. Lookup is linear in the bounded record count; rebuilding can temporarily hold both trees. A limit or scan failure is terminal, not silent partial recursion. Root move/deletion ends coverage. Other remote filesystem delivery remains a native limitation.

Apple uses native C FSEvents/CoreFoundation, with no Objective-C or third-party runtime. Its stream runs on the owned worker's CFRunLoop; the retained run loop survives stop requests until join. The localized deprecated scheduling call preserves this single-thread callback contract and the 10.13 baseline. Dropped, coalesced and root-change flags become overflow/terminal events. Source is implemented; native compile/run and sandbox behavior remain pending. [Apple scheduling contract](https://developer.apple.com/documentation/coreservices/1447824-fseventstreamschedulewithrunloop) describes the run-loop lifecycle. C++ DirectoryWatch keeps a host-owned C pointer, requires closeUntil, records callback Result failures and releases captures during cleanup. callbackResult() is valid after close. See watch_directory.c.

LOOP_FD is a separate default-on POSIX extension requiring LOOP. attach_fd owns a CLOEXEC duplicate and one persistent request slot; it does not change O_NONBLOCK. The creator polls; callbacks borrow the watcher and perform their own nonblocking descriptor I/O. READ/WRITE also indicate EOF/error readiness, which the actual I/O resolves. The descriptor getter is owner-only; do not close its borrowed result. Original descriptors may close independently, but offsets and open-file flags remain shared, and POSIX record-lock close rules apply. Windows returns NOT_SUPPORTED without advertising a capability bit.

Caller-owned user state stays valid through the final callback and cleanup. The owner-only close pumps cancellation; failed native unregistration retains the source for retry. After unregistration, descriptor-close errors consume it. Loop destruction is BUSY until all watcher objects close. No allocation occurs per readiness notification. Timer and descriptor close share one cancellation/ pump implementation. C++ DescriptorWatch provides noexcept lambdas and explicit results. A private pointer is added to each loop/request (8 measured bytes per object on Windows x86/x64 and Linux x64); existing public layouts are unchanged. Watch size/alignment is 152/8 on Windows x86 and 216/8 on both x64 hosts; options/event are 28/4 and 48/8 on x86, 48/8 and 56/8 on x64. Descriptor watch is 44/4 or 72/8.

```c
hx_os_loop_watch *watch = NULL;

hx_os_status status = hx_os_loop_attach_fd(loop, fd, HX_OS_WAIT_READ,
	ready_callback, user, &watch, &error);
/* The callback can use hx_os_loop_watch_descriptor(watch), then request stop. */
/* Drive loop; finally check hx_os_loop_detach_fd(&watch, &error). */
```

## Process control, capture and system helpers (batch 18)

PROCESS and SYSTEM_UTILS default to ON and can be disabled independently. The system module adds Shell32 on Windows for native Unicode arguments. Capture requires PROCESS + PIPE + LOOP, and works with NETWORK disabled. The integer helpers and explicit POSIX timespec conversions remain available without either module; Windows timespec operations return NOT_SUPPORTED.

`hx_os_process_spawn` takes a literal UTF-8 executable path, argument array excluding argv[0], optional complete environment and optional standard-stream redirections. The library supplies argv[0]. There is no shell or PATH search. NULL arguments means no arguments; NULL environment inherits a snapshot, whereas an array containing only NULL supplies an empty environment. Environment entries are nonempty-name=value with unique names; Windows compares names case-insensitively. No SystemRoot/PATH values are injected into an explicit environment. Callers must provide any variables their child requires. Inputs are borrowed during spawn; the process owns native resources afterwards.

Arguments and environment are individually bounded to 4 MiB and 65536 entries. Windows converts to UTF-16, quotes according to CRT/CommandLineToArgvW rules, enforces the native 32767-wide-character command-line bound and sorts the Unicode environment. Children with a custom command parser may use different rules. Explicit executable selection avoids command-line executable ambiguity. CreateProcessW receives a private standard-handle allowlist. Missing inherited Windows standard streams use NUL. Temporary inheritable duplicates require host coordination with unrelated broad-inheritance spawns. POSIX uses posix_spawn with private CLOEXEC duplicates and dup2 actions. Host descriptors lacking CLOEXEC remain inherited; close/spawn coordination stays the host's responsibility. Inherited POSIX environment bytes are preserved; public environment enumeration instead validates UTF-8 before invoking any visitor.

Process operations require exclusion on each object. NOW polls once; finite/infinite waits use the existing monotonic deadline, polling native completion with at most 5 ms requested sleeps. Scheduling can delay return. Timeout leaves the object and result valid for retry. Successful results are cached. Nonzero exit codes are successful waits; POSIX signal termination is reported separately without fabricating an exit code. Windows checks completion before reading the exit code, so a real exit code 259 is valid. Destroy returns BUSY while running; close_until waits and then destroys. The library never kills, detaches, starts a reaper thread or changes SIGCHLD. The host must not independently reap this child or ignore SIGCHLD.

`hx_os_process_capture_run` performs spawn plus concurrent stdout/stderr collection on a private owner-thread loop. Child stdin is EOF. No worker thread is started. Default retained output is limited to 1 MiB per stream, raw bytes. Both pipes, the loop and initial reads are prepared before spawn. Once a child is accepted, even TIMEOUT, LIMIT or I/O failure returns an owned session. Wait and close can be retried on that session. Complete success requires both stream EOFs and child exit; descendants retaining write handles may delay EOF.

At the exact output bound, LIMIT pauses that stream even if EOF might follow. The caller can raise_limit or explicitly discard future output while continuing to drain. A previously accepted read still commits its retained prefix. Discard after an I/O error closes that retired failed stream, which can expose a broken pipe to the child. There is no silent truncation. Borrowed byte views last until the next wait/close. Text conversion defaults to strict UTF-8, preserves BOM, uses the output string's context and is transactional. An incomplete character in a timeout snapshot may fail conversion. Legacy encodings still require the existing optional codec backend; built-in ISO-8859-1/Unicode conversions remain independent of it.

The session uses two request slots, a 32 KiB in-flight byte budget, geometrically grown retained buffers and optional 16 KiB discard scratch per stream. Limits bound retained output, not all heap/native memory: buffer growth temporarily holds old+new storage; request objects, allocator metadata, spawn snapshots and native resources add overhead. Each read currently uses the existing request allocation/mutex/event machinery. No new locks, indirect calls, allocations or system calls were added to existing file/thread/pool hot paths. Synchronous pipe creation now records its actual read/write direction for spawn validation.

`hx_os_process_arguments` owns a UTF-8 snapshot (including argv[0]): native Unicode command line on Windows, /proc/self/cmdline on Linux, native argc/argv on Apple. Missing Linux procfs returns NOT_SUPPORTED. The shared C example argument adapter uses this API; trimmed builds require host-provided UTF-8 argv and cannot promise Windows narrow-CRT Unicode fidelity. Hostname has buffer and hx_string forms; process ID is native. Environment enumeration takes a bounded snapshot under the library environment lock, then calls visitors outside that lock, permitting reentry and mutation. External native/CRT environment changes still require host exclusion. Snapshot staging uses CRT allocation while locked to avoid injected allocator callbacks there.

CPU flags describe detected usable SSE/SSE2/SSE4.1/AVX/AVX2/NEON. AVX checks XSAVE, OSXSAVE and XCR0 XMM/YMM state. ARM uses native auxiliary-vector/OS capability queries where available; unknown bits are omitted. WOW64 and Rosetta are OS compatibility/translation reports, not virtual-machine detection. Unknown Linux emulation returns NOT_SUPPORTED and preserves output. Byte swaps and udiv64 are portable inline C; division by zero preserves outputs and returns INVALID. Supported ARM compilers select their normal division ABI helper. The division benchmark compares identical native division expressions; it does not claim an assembly speedup.

POSIX duration conversion checks time_t range. Deadline conversion requires an explicit CLOCK_MONOTONIC or CLOCK_REALTIME and samples that clock plus remaining library duration. Infinite deadlines return NOT_SUPPORTED. It never casts one clock's epoch to another; callers must recheck predicates/deadlines after wake because wall-clock changes/suspend and scheduling can affect native waits.

C++ System returns Result<String>, Result<StringList> and scalar results. Process and ProcessCapture retain host-owned native handles on failure or timeout. Their destructors do not wait, detach or kill. Process arguments are bounded initializer lists of borrowed UTF-8 strings, consumed/copied during the call.

```c
hx_os_process_capture *capture = NULL;
const char *arguments[] = {"--version", NULL};

hx_os_status status = hx_os_process_capture_run(NULL, executable, arguments,
	NULL, hx_os_deadline_after_ms(2000), &capture, &error);
if (status == HX_OS_OK) {
	hx_string output = HX_STRING_INIT;

	status = hx_os_process_capture_text(capture, HX_OS_PROCESS_STDOUT,
		&output, &error);

	/* Use output, then destroy it. */
	hx_string_destroy(&output);
}
/* Non-NULL capture remains owned on timeout/limit/error. Continue waiting,
 * raise limits or explicitly discard as needed, then check close_until.
 */
```

See [capture_process.c](../examples/capture_process.c) for an executable example with an explicit retry/discard policy, and the public process header for all ownership/error contracts. Native macOS, Windows 7 and embedded targets remain pending validation.

## Scheduling requests and delayed tasks (batch 19)

THREAD_EX adds explicit thread/current-thread priority and affinity requests. Windows maps idle/normal/time-critical to native relative thread priorities; the process priority class still matters. Linux requests SCHED_IDLE/OTHER with priority zero, or SCHED_RR at its native maximum. Apple requests the native scheduler's low/middle/high ranges (SCHED_OTHER for idle/normal, RR for high). Permission/native failures are preserved. These requests promise neither a fixed CPU frequency nor hard real-time response. Raising priority on an idle worker in tests is followed by restoring normal before it is released. Changing priority back can itself require privileges on older systems. Callers exclude concurrent join/destruction/scheduling changes.

Affinity mask zero is INVALID. Linux bits identify CPUs 0..63; the adapter reads back the actual set because the kernel can intersect a request with cpuset/ online restrictions. A mismatch reports NOT_SUPPORTED and attempts to restore the old set; restore errno is in error.cause_code. A failed operation can have temporarily or permanently changed affinity. Successful readback cannot stop later hotplug/cpuset changes. Windows bits select the thread's current processor group, limited to native pointer width; upper bits on x86 return NOT_SUPPORTED. macOS returns NOT_SUPPORTED for strict binding. No affinity hints masquerade as a strict binding guarantee. C++ Thread exposes setPriority/setAffinity.

DELAYED_TASKS defaults ON, requires THREAD_POOL and has a capability bit. It is independent of LOOP/TIMER/NETWORK. Absolute monotonic due times indicate when a task becomes eligible, not when it must start or finish. NOW uses the ordinary ready queue; INFINITE is INVALID. C++ submitAfter takes milliseconds, with nonpositive delays becoming NOW; submitAt accepts a library deadline. Cancelable variants preserve the existing cancellation token, typed result, explicit failure and bounded preparation contracts.

The sorted future list shares queued+submitting capacity with the ready FIFO. No timer manager thread, second queue budget or task dependency graph is added. Existing workers wait on the pool condition until the earliest due time or a notification. Equal due times are promoted in insertion order, behind already ready work. Multiple workers may start/finish them in a different order. Insertion and queued cancellation scan at most the bounded future-list length. Promotion wakes other workers. Native scheduler clock/condition errors retire affected future tasks with a failing work_status and their normal cleanup.

Queued cancellation moves a future task to the ready list, without waiting for its due time. Cleanup still runs exactly once on a worker outside the pool lock. DRAIN honors future due times; CANCEL_PENDING promotes cancelled future tasks for retirement. Timeout retains the pool/tasks and a retry may escalate from drain to cancel-pending. Running tasks remain cooperatively cancellable.

A future task's metadata shares its task allocation and adds one private mutex. Cancellation takes that mutex before consulting the pool; the worker clears that pool pointer outside the pool lock before executing/cleaning the task. Joining all workers therefore removes every delayed task's access to the pool. Retained tasks and cancellation sources remain safe after pool destruction. The internal close path marks cancellation under the pool mutex without taking the per-task lock, avoiding lock-order inversion. Allocation and prepare callbacks remain outside the pool mutex, including close/preparation races.

With this module enabled, ordinary tasks add one nullable private pointer and branches; they add no allocation count, lock acquisition, indirect dispatch, or clock/system call when there are no future tasks. Disabled builds remove the extra task/pool fields and delayed logic. Measured size/alignment with it enabled: task 88/8, pool 104/8 and delay metadata 32/8 on Windows/Linux x64; 56/8, 64/8 and 24/8 on Windows x86. Existing public structures are unchanged. A future task additionally pays for its mutex allocation and, while pending, native timed waits/clock reads. See VALIDATION for before/after measurements; these do not stand in for hx_c_log integration or its performance acceptance.

```c
hx_os_task *task = NULL;

hx_os_status status = hx_os_thread_pool_submit_delayed(pool,
	hx_os_deadline_after_ms(100), work, user, cleanup, &task, &error);
/* On acceptance, wait/cancel/release through the ordinary task API. */
```

```cpp
#include <hx/ThreadPool.hpp>

hx::Result<hx::Task<int>> delayed_answer(hx::ThreadPool &pool) noexcept
{
	return pool.submitAfter(std::chrono::milliseconds(100),
		[](int value) noexcept { return value * 2; }, 21);
}
```

See [delayed_task.c](../examples/delayed_task.c) for a C example and the existing pool ownership contracts for shutdown. Native references: [Linux affinity](https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html), [Windows affinity](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setthreadaffinitymask).

## Local sockets and duplex file endpoints (batch 20)

LOCAL_SOCKETS defaults ON and requires NETWORK. A pathname local stream uses HX_OS_LOCAL / HX_OS_LOCAL_STREAM; both the dedicated factory and ordinary socket_create accept that combination. Local address storage is separate from the existing IP address/operation ABI. Paths are strict UTF-8 with a terminating NUL inside 108 bytes; macOS further enforces its shorter native sun_path. Abstract addresses, descriptor passing, peer credentials and datagrams are outside this interface. Native returned names must fit and validate as UTF-8; empty output denotes an unnamed peer. No truncation is hidden.

local_listen combines create/bind/listen. It does not unlink an existing path, and socket close does not remove the pathname. The caller owns its directory and removes its own socket name after use. Failure after successful bind can leave the newly created name. Windows uses the native AF_UNIX provider with Win32 UTF-8 pathname semantics, available on supported Windows 10+ systems. Win7 continues supporting the rest of the library; this factory returns NOT_SUPPORTED there. The capability bit describes compiled adaptation; provider availability is determined by the native operation.

local_connect combines creation and deadline-aware connection. TIMEOUT retains the socket; finish_connect resumes using its copied path. Repeated attempts must use the same pathname; terminal failure requires closing the socket. In particular, Linux AF_UNIX queue-full EAGAIN means no connection has started, so the library retries the connection with bounded requested sleeps rather than reporting writability as connection success. In-progress completion checks both SO_ERROR and getpeername. Other convenience-factory failures close the new socket and preserve the original error.

Existing send/receive/wait/shutdown/close functions work on local streams. The new socket_receive_until combines a nonblocking stream read and readiness wait, returning some bytes, EOF, error or TIMEOUT (no private buffering). Accepted/connected local sockets can use existing loop READ/WRITE operations, including cancellation and native retirement. Loop CONNECT/ACCEPT reject local sockets explicitly: the old operation/completion IP address cannot hold their pathnames. Accept_until and connect_until/finish_connect provide direct entries. Do not mix direct I/O with pending loop operations. Socket/network ownership, thread exclusion and loop-owner rules remain the same.

DUPLEX_PIPE defaults ON independently of NETWORK, PIPE and LOOP. pipe_pair_socket returns two owning hx_os_file endpoints; each reads and writes. POSIX uses socketpair(AF_UNIX, SOCK_STREAM). Windows uses a private IPv4 TCP loopback pair, not AF_UNIX. Creation defaults to a 5-second monotonic deadline; the _until entry makes the policy explicit. POSIX performs one local socketpair operation; deadlines do not cancel synchronous native calls or imply a hard return-time guarantee. Failure returns two NULL endpoints.

The Windows handshake uses nonblocking connection/accept with deadline waits, exclusive local binds, a bounded 64-attempt accept loop and verification of the expected client's loopback address/port. Both published endpoints are then blocking and noninheritable. Each owns one balanced Winsock reference, so they work without a library network instance. Winsock recv/send/closesocket handle these endpoints; they are never passed to ReadFile/WriteFile/CloseHandle. The extension storage is allocated only for socket-backed file objects. They cannot serve as Windows child standard handles; process spawn rejects that combination. POSIX socket file descriptors can serve as standard streams.

File short-read/short-write/EOF contracts apply. One reader and one writer may operate on an endpoint concurrently; same-direction calls and close require exclusion. shutdown(SHUT_WRITE) sends EOF while preserving the read direction. Seek, size, sync, truncation and mappings are unsupported. There is no durable flush, message-boundary or peer-credential guarantee. Windows loopback adds ports, TCP buffering and potential firewall/provider failure modes. This synchronous pair does not replace the existing dedicated asynchronous pipes. POSIX pipe/socket writes share the existing thread-local SIGPIPE suppression implementation, without installing a process signal handler.

C++ DuplexPipe::pair returns two move-only File owners. LocalSocket references a host-owned socket pointer; connect/finishConnect/acceptUntil and explicit close preserve reachability on timeout or pending I/O. It also supplies readUntil/writeUntil, pathname strings and shutdown.

Socket setup/error/readiness code is shared by network and standalone duplex adapters. Core socket creation keeps its native call count and allocation count; enabling local support adds a nullable private pointer, and only local sockets append their 112-byte path/state storage to the same allocation. Windows ordinary file I/O/close add a socket-kind branch when DUPLEX_PIPE is enabled; regular files add no allocation/native call. POSIX reuses the existing pipe-write branch when PIPE is already enabled. New Windows duplex creation uses two wrapper allocations, two WSAStartup references, a temporary listener and two connected sockets. Native resource cleanup balances those references.

```c
hx_os_file *first = NULL, *second = NULL;

hx_os_status status = hx_os_pipe_pair_socket(NULL, &first, &second, &error);
/* Use file read/write; shutdown preserves the opposite direction. */
```

```cpp
#include <hx/Local.hpp>

hx::Result<std::pair<hx::File, hx::File>> make_channel() noexcept
{
	auto channel = hx::DuplexPipe::pair();
	if (!channel)
		return hx::Failure(channel.error());

	auto written = channel.value().first.writeAll("hello", 5);
	if (!written)
		return hx::Failure(written.error());

	auto stopped = hx::DuplexPipe::shutdown(channel.value().first);
	if (!stopped)
		return hx::Failure(stopped.error());

	return channel;
}
```

See [duplex_message.c](../examples/duplex_message.c). [Microsoft AF_UNIX description](https://devblogs.microsoft.com/commandline/af_unix-comes-to-windows/) documents the UTF-8 pathname/provider boundary. macOS, Win7 and embedded native runs remain pending.


## Registry, power requests and obfuscation (batch 21)

Use `hx_os_registry_open_user` to open an explicit HKCU path and `hx_os_registry_read_string` to receive strict UTF-8 directly in `hx_string`. A two-pass `read_string_buffer` entry shares that decoder. String reads accept REG_SZ/REG_EXPAND_SZ literally, never expand environment variables, bound native values to 1 MiB and retry capacity races at most eight times. Invalid UTF-16, embedded NUL, malformed DWORD and type mismatch remain distinct failures. Outputs stay unchanged on failure. Paths are relative registry paths with backslash separators, not filesystem paths. Creation, write access and WOW64 view selection are explicit. Exclusive creation preserves existing keys. Native creation may create parents and is not transactional. Serialize use of a key and exclude close. Close consumes the handle even on a native error. Other systems and disabled builds return NOT_SUPPORTED.

`hx_os_sleep_inhibit_create(NULL, "Exporting project", &request, &error)` creates an active request. Repeated set_active(true) is idempotent; destroy releases only this object's request. Transfer between threads is allowed after host synchronization; concurrent operations on one object require exclusion. No worker or global power setting is introduced. Windows uses Win7-compatible PowerCreateRequest/SystemRequired handles, rather than thread-shared execution state, so independent consumers can compose safely. Performance requests use ExecutionRequired (Win8+, Win7 reports NOT_SUPPORTED). User and OS policies can override requests; no CPU frequency or real-time guarantee is implied. Apple sleep uses the public IOKit C assertion API. Apple performance uses a single Objective-C implementation file for the public NSProcessInfo activity API, with exceptions contained at the C boundary. Disable PERFORMANCE_REQUEST to omit Foundation/Objective-C; sleep remains a C IOKit adapter. Linux returns NOT_SUPPORTED for both requests, with no DBus dependency. Release consumes the object, reports native cleanup errors, and never changes another token.

`hx::Registry`, `hx::SleepInhibit` and `hx::PerformanceRequest` provide movable C++11 synchronous RAII. Explicit close reports cleanup failure. These do not own asynchronous work or perform indefinite waits.

String obfuscation is reversible XOR, not cryptography. `tools/obfuscate_string.py --name app_label --text "example"` emits C99 pre-encoded data, with UTF-8 input bounded to 1 MiB. Python is a development tool, never a core dependency. `hx_os_obfuscate_string(&app_label, &text, &error)` returns validated UTF-8 using the output's context, preserving it on failure and wiping temporary plaintext. The caller owns the returned plaintext and its lifetime. The buffer API also supports raw bytes, exact in-place decoding or disjoint buffers; required includes NUL and insufficient capacity leaves storage unchanged. `constexpr auto text = hx::obfuscate<0x5a>("example")` is the C++11 convenience. Compiler/debug artifacts may retain literals; do not use this for secrets. The C example is `examples/obfuscated_string.c`.

REGISTRY, SLEEP_INHIBIT, PERFORMANCE_REQUEST and OBFUSCATION are independent CMake switches, default ON. Capabilities describe compiled adapters; native policy and version restrictions still apply. Windows adds advapi32 only when registry or the existing random fallback needs it. Apple propagates framework and Objective-C runtime link requirements for static consumers.


## Calendar, translation table and bit streams (batch 22)

`hx_os_wall_time_format_utc_string(milliseconds, NULL, &text, &error)` returns an ISO-like UTC timestamp directly in an owning UTF-8 string. The buffer entry supports two-pass capacity queries without partial output. Unix milliseconds are floored to seconds, including negative values. The shared Gregorian implementation covers years 0001..9999, independent of native time_t limits. The strict UTF-8 pattern is bounded to 65536 bytes and accepts %CCYY, %MM, %DD, %hh, %mm, %ss and %% only. Invalid patterns/encoding and out-of-range dates leave output unchanged. NULL chooses %CCYY-%MM-%DDT%hh:%mm:%ssZ. No leap-second representation, locale change or allocation occurs in the buffer formatter. The string entry allocates bounded temporary/output storage with its context.

`hx_os_wall_time_local_offset_minutes` returns the current native offset east of UTC plus DST 0/1/-1 (unknown). It uses the host runtime's timezone rules; the host must serialize external TZ/timezone changes. It does not look up historical named timezones. Fractional-minute offsets return NOT_SUPPORTED.

Create an independent translation catalog with `hx_os_text_lookup_create`, add copied UTF-8 pairs using `add`, and call `get_string` for an owned result. An absent key returns the supplied original text. Empty translations remain empty. Keys are exact case-sensitive bytes without Unicode normalization. Default limits are 4096 entries and 1 MiB of key/value bytes including NULs; options can set other finite bounds. Storage grows on demand. Sorted entries use binary-search lookup and O(n) insertion; metadata is additional to text limits. Transactional replacement can temporarily retain old+new text (at most twice the text limit); array growth temporarily retains both arrays. Read-only queries can run concurrently; exclude mutation/destruction. There is no catalog lock, locale singleton or background service. Buffer reads and bit operations allocate nothing. String output uses its own allocator. `examples/localized_time.c` combines the two convenient C interfaces.

Bit reader/writer values live on the stack and borrow caller buffers. Count units are explicit: init accepts bytes; read_bits/write_bits accept 0..64 bits. Bit order is MSB-first within both byte and value, on every architecture. Read past the end reports EOF; write past capacity reports OUTPUT_FULL; values that do not fit report RANGE. Failure preserves cursor and buffer/output. Only written bits change; writer alignment zero-pads to a byte boundary and reader alignment skips those bits. Zero-bit operations are valid with zero value. No shift by 64 or unchecked bytes-to-bits multiplication occurs. No allocation, lock, indirect call, system call or assembly is used.

CALENDAR, TEXT_LOOKUP and BITSTREAM are independent default-on switches. Disabled operations return NOT_SUPPORTED with absent capability bits. The C++11 conveniences are `hx::Calendar::utc`, `hx::TextLookup::get` and `hx::BitReader`/`hx::BitWriter`; they reuse the C implementations. Bit cursors remain borrowed views, while TextLookup provides movable synchronous RAII.


## Synchronous signals and stack parameters (batch 23a)

Create a dispatcher with `hx_os_signal_handler_create`, connect a copied signal name, and call `hx_os_signal_emit` with an optional stack `hx_os_calldata` pack. The C example is `examples/signal_notifications.c`. No background thread or queue is created. Callbacks run synchronously on the emitter, outside locks, in registration order for that emission's snapshot. Connections added after acceptance wait for the next emit. Disconnected callbacks that have not started are skipped. Concurrent emits can invoke the same callback concurrently; its user state must support that use. Callback failures do not suppress later eligible callbacks; emit returns the first non-OK status and matching native error details. The callback receives an initialized, non-NULL error pointer borrowed for the call. A mismatched error.status is normalized to its returned status without inventing native details.

Connection IDs include a generation. Reused capacity cannot let an old ID remove a new subscription. IDs belong to one handler and must not be forged. The handler owns subscriptions; an output ID is optional. Connect copies its strict UTF-8 name (1..127 bytes), borrows user state, and on acceptance adopts optional cleanup exactly once. Rejection calls neither callback nor cleanup. `disconnect` prevents future starts and may immediately execute eligible cleanup. `disconnect_until` additionally waits for running callbacks AND cleanup, using a monotonic deadline. Timeout keeps the ID/handler/user alive for retry. Waiting for the current callback/cleanup, including an ancestor in nested emission, reports DEADLOCK. General cross-thread wait cycles remain the host's responsibility.

`stop` only prevents new connections, emits and callback starts. `close_until` then retires all subscriptions, waits for accepted emitters/cleanup and consumes the handler on success. Failure retains it. The owner excludes other entrants and concurrent close during this final lifetime transition; accepted emits may finish concurrently. Successful close does not join the host's emitting threads: all API calls must have returned before unloading the platform DLL. Before unloading a consumer plugin, retire its callbacks/cleanup first. User code must return normally, without longjmp, thread exit or fiber switches. Synchronous cleanup can exceed a deadline; no hard callback cancellation is promised. The library never terminates the process or detaches a host thread.

Defaults are 256 connection slots, 64 simultaneous emits and nesting depth 16; options allow smaller embedded budgets. Hard limits are 65535 slots/emits and 64 nesting frames. Slots are allocated at creation. A nonempty emit allocates only matching ID storage, bounded by 8 * max_connections bytes, outside locks. It sorts IDs and invokes callbacks indirectly. Connect uses one mutex pair and no allocation. An empty emit allocates nothing; emission scans the bounded slot array. Callback dispatch takes locks around selection and retirement, never around user code. Thread-local ancestry is a pointer to active stack frames; there is no TLS destructor or process-wide callback registry. Existing module hot paths gain no extra locks or allocations.

The fixed-layout calldata pack has 16 entries and 512 bytes shared by copied names/strings, including terminators. Names use strict UTF-8 (1..127 bytes); strings support embedded NUL. Typed bool/i64/double/string/pointer getters separate missing keys from wrong types and preserve outputs on failure. Pointer values are borrowed and never released. Setting is transactional and reclaims replaced text without heap allocation. Copying the whole pack is safe because strings use offsets into its own storage; borrowed views expire on mutation. Emit borrows a read-only pack until return.

CallData setters return Result<void>; getters return Result<T>. SignalHandler::connect stores typed captures in its context and releases them on retirement. CallDataView borrows the parameter pack; its string getter returns Result<String>. Callbacks declare noexcept and return void, a status or Result<void>; emit returns the failure directly, preserving native details. The facade references the host owner and requires closeUntil. SIGNALS remains independently optional; operational entries report NOT_SUPPORTED when disabled.


`CallData::edit()` / `hx_os_calldata_edit_*` group immediate assignments with one final error check; earlier successes survive a later failure. `CallDataView::stringView` borrows the pack without allocation until mutation/destruction; `string` still copies. These helpers preserve signal and named-procedure publication contracts.

## Mutable settings and retained snapshots (batch 23b)

`hx_settings_create` makes an independent mutable object. Use typed setters, `set_default_*`, and typed getters directly; `examples/settings.c` shows the common path. Explicit and default values are separate. Only an absent literal key falls back; null, wrong types, invalid data and permission errors never do. There is no numeric-string coercion or recursive default merge. Root keys are literal byte keys, so a dot does not imply a path. The view APIs support empty and NUL-containing UTF-8 keys and values. The standard C-string conveniences borrow their arguments for the call. Setters copy into the object's context.

Values include null, bool, signed 64-bit integer, finite double, UTF-8 string, array, object and borrowed user pointer. Array/object factories deep-copy inputs; source values can be released afterward. Immutable values have reference ownership and can outlive the mutable owner. String getters copy into the output's context; array/object access returns retained snapshots. Copying a settings object deep-copies both layers into the chosen context and copies no observer/cleanup. The allocator and its code/data must outlive every retained value, including snapshots surviving settings close. Retaining a user pointer value never extends the pointed-to object's lifetime.

Concurrent reads and writes use immutable roots with brief mutex protection for acquisition/publication. Allocation, deallocation and observers run outside that mutex. A writer prepares new storage, checks the revision, and retries at most max_retries attempts (default 8); exhaustion returns BUSY with no lost update. Failures before publication leave both layers unchanged. A snapshot stays unchanged through subsequent writes. Effective snapshots combine missing default keys at the top level and can fail LIMIT if the combined tree exceeds its budget. Caller-retained snapshots consume caller-controlled memory.

Options default to 4096 nodes, 4 MiB retained allocation cost per root, depth 32, 64 active operations, 8 update attempts and observer depth 16. Hard maxima are 65536 nodes, 16 MiB retained tree storage, depth 64, 65535 active operations and 64 update attempts/observer frames. Immutable factories use those tree maxima. Temporary pair arrays are separately bounded by entry counts; import measures the complete destination before allocating children. Old roots and transient replacement roots may coexist. This is a per-tree budget, not a global process quota. Storage is allocated on demand; no background thread or queue starts.

Optional create-time observers run synchronously after commit, outside locks. They receive a borrowed fixed-layout event with key, layer, revision and exact before/after values; whole-load replacement carries root values. Concurrent notifications may arrive out of revision order. Reentrant reads are supported, and nested mutation has its own bound. Observers/cleanup must return normally, without exceptions, longjmp, thread exit or fiber switching. Notification is not a recoverable failure channel and cannot roll back an observed commit. The C++ facade accepts this explicit C observer contract through options.

`close_until` rejects new operations and waits for accepted operations and observers, then runs optional cleanup once. NOW/INFINITE are supported. Timeout preserves the object for retry; a self-close returns DEADLOCK without starting closure. The owner excludes other entrants and simultaneous final closes. Slow synchronous cleanup may exceed a deadline. No host thread is terminated or detached. Retained immutable snapshots remain valid after close and can keep the context BUSY. Join/quiesce all callers before unloading the platform or allocator plugin. Scalar reads allocate nothing, use two brief mutex sections and retain/release both roots. Queries on a retained value require no mutex. Updates copy one packed root/key block and retain unchanged children; explicit incoming composite values are copied into this context. Shutdown notifications are emitted only when the final active operation retires during close.

Configuration integration keeps hx_config immutable. Import/load require an object root and support JSON-compatible kinds; XML/datetime and integer range failures are explicit. Load constructs the complete explicit replacement before publication, leaves defaults intact and preserves old state on failure. It is a whole-object replacement, not a merge. JSON/INI/properties serialization reuses the existing serializer and representability rules. Default save writes only explicit values. User pointers fail serialization, including NULL pointers with the pointer tag; use a null value for JSON null. `to_config` can export an explicit/default/effective snapshot; `to_json`, `load_json` and `save_json` are conveniences. Save uses existing atomic file publication and its durability limits. Config options still bound text, memory, depth and work; rich load errors preserve source/position and native failures. Locale-independent double formatting and private document construction are shared with existing config code; TEXT_NUMBER can be disabled while settings conversion still works.

SETTINGS is independent of CONFIG, CONFIG_SERIALIZE, FILE_UTILS, SIGNALS and CONTAINERS; combinations missing prerequisites report NOT_SUPPORTED. C++ Settings setters return Result<void> individually. Getters return Result<T>, with owned String results. SettingValue factories return Result<SettingValue>; retained snapshots are copyable/movable and arrays/objects accept initializer lists of checked values. Mutable Settings references a host owner with explicit retryable close. C layouts and exports remain unchanged.

Settings string references (`hx_settings_get_string_ref`, `SettingValue::asStringView`, `Settings::stringView`) retain immutable bytes without copying/allocating. They survive owner updates/close and retain the originating context; C leases must be initialized, copied through the lease API and released. `Settings::update` / `hx_settings_update_*` reuse individual setters and notifications, preserve prior successes on first failure and report applied count at finish. There is no rollback or combined atomic publication. See [common-call ownership](COMMON_UTILITIES.md).

## Bounded interval profiler (batch 23c)

For owned structured snapshots, means, standard deviations and opt-in bounded P50/P95/P99, see the [profiler tutorial](PROFILER.md). The existing TSV format and shutdown behavior below remain compatible.

`hx_os_profiler_create`, `start_root`, `reach`, `leave` and `dump_string` provide explicit interval trees without background workers or native stack capture. See `examples/profile_intervals.c`; C++11 offers `hx::Profiler` and the lexical `hx::ProfileScope` guard. Successful entries must be balanced on the same thread/fiber. Names are copied strict UTF-8. Identical paths merge across threads; inclusive/exclusive nanoseconds are monotonic wall durations, so overlapping threads can sum to more than elapsed process wall time.

Defaults limit records to 1024, participating thread/fiber caches to 64, nesting to 32, names to 127 bytes, output to 4 MiB including NUL, and active API calls to 64. A warmed thread/path uses no new context allocations. Entering/leaving still uses short mutex sections, TLS access and clock reads; this is bounded instrumentation, not a claim of zero runtime overhead.

Completed child durations subtract from the parent's exclusive duration. Thread exit or `restart_thread` aborts open scopes and releases that thread's cache without deleting completed statistics. Aborts do not invent durations. Counters saturate at UINT64_MAX with flag 1; clock errors set flag 2. A failed leave retains its frame for retry. Dump captures a consistent bounded snapshot, formats outside the lock and preserves caller output on failure. TSV names are JSON-quoted; sizes include NUL. Record-growth races retry eight times before BUSY. Save uses the existing atomic file publisher.

Stop rejects new scopes while leave/restart/dump remain available. Close detaches the caller's idle cache and waits for active calls and foreign TLS caches. An open caller scope returns DEADLOCK without stopping. Timeout retains the handle. Native TLS retirement can return BUSY after the profiler callback finishes; retry completes key destruction. Long-lived host threads must call `restart_thread` or exit before plugin unload. Exclude new entrants and parallel final closes, and let host API calls return before unloading. Detected allocator reentry returns DEADLOCK; do not unwind through C calls. C++ scope guards must not be mixed with manual stack changes and must not outlive their host-owned profiler. Explicit close owns shutdown policy.

PROFILER can be disabled independently. FILE_UTILS is needed only for save; all other profiler operations work without settings, config or signals.
