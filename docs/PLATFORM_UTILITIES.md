# Platform utilities

[简体中文](PLATFORM_UTILITIES.zh-CN.md)

Common operations have direct C entry points. Optional C++11 headers wrap the
same implementation. A NULL context selects the existing immutable default
allocator. Feature switches retain declarations and failure stubs, with the
corresponding capability bit cleared.

## System information

`hx_os_sysinfo.h` provides byte counts, CPU topology, page size, filesystem
space, current-process memory and OS version. `HX_OS_ENABLE_SYSINFO` defaults
to ON. Library version and ABI queries remain available with it disabled.
These calls do not create workers, install handlers or alter process state.

```c
hx_string version = HX_STRING_INIT;
hx_os_error error;
hx_os_status status = hx_os_sys_version_string(&version, &error);
/* Check status, then use hx_string_c_str(&version). */
hx_string_destroy(&version);
```

For versioned disk/process result structs, use their `*_INFO_INIT` macros.
Buffer-based text queries report the required capacity including NUL. Scalar
and structured outputs retain their prior values on failure.

CPU counts describe online system CPUs, across Windows processor groups, not
the current process affinity mask or a cgroup CPU quota. Linux physical counts
deduplicate online package/core pairs in sysfs; unavailable topology is
NOT_SUPPORTED, never a logical-core count presented as physical cores.

Available RAM is Windows available physical memory, Linux `MemAvailable`, or
macOS free plus inactive pages. The macOS value is an estimate, and these
snapshots do not promise that a subsequent allocation succeeds. Linux queries
describe the OS view, not a container-specific memory limit. Disk available
space uses POSIX `f_bavail`, distinct from total free blocks `f_bfree`.

Current-process resident bytes are the working set/RSS. Virtual bytes describe
reserved plus committed virtual address space on Windows, `/proc/self/statm`
size on Linux, and Mach virtual size on macOS; they are not commit charge or
physical memory consumption. Native resource queries can block.

`hx_os_cpu_usage_create` takes an initial process CPU and monotonic sample.
Each successful query returns the interval average since the previous sample:
`100 * process_cpu_delta / elapsed_delta / initial_logical_cpu_count`.
Children are excluded. Callers serialize access to each sampler; samplers are
independent and allocate through their context. A zero-length interval returns
WOULD_BLOCK without advancing the sample. OS counter granularity and changes
in CPU topology can affect short samples. The sampler does not clamp results.

```cpp
#include <hx/SystemInfo.hpp>
auto version = hx::SystemInfo::version();
auto space = hx::SystemInfo::diskSpace(".");
hx::CpuUsage cpu;
/* Do work or wait before sampling. */
double percent = cpu.query();
```

C++ convenience methods throw the existing `hx::StatusError`, including its
native error fields. `CpuUsage` owns its sampler and supports moves. These
headers need C++11; the underlying library remains C99. The standalone C
example is `examples/system_info.c`.

## Dynamic libraries

`HX_OS_ENABLE_LIBRARY` defaults to ON. `hx_os_library_open`, `symbol` and
`close` provide the basic C interface; `_ex` variants also accept an initialized
`hx_string` for owned diagnostic text. `hx::Library` supplies movable ownership
and `symbol<Pointer>(name)`; a failure throws `LibraryError` with an owned
message and the original status/domain/code.

```cpp
#include <hx/Library.hpp>
hx::Library plugin("./plugin.so");
using Abi = uint32_t(HX_OS_CALL *)(void);
auto abi = plugin.symbol<Abi>("hx_plugin_abi_version");
// Check ABI before calling the rest of the plugin interface.
```

Paths are UTF-8 filesystem paths; a relative filename is resolved against the
current directory rather than searched as a bare POSIX soname. Dependencies
still follow the native loader policy. POSIX uses RTLD_NOW and LOCAL/GLOBAL;
Windows ignores the visibility flag. No function changes loader search paths.
The POSIX loader has text errors instead of a portable numeric error code, so
the main error uses HX_OS_ERROR_LOADER and the optional diagnostic captures the
text. It copies up to 2047 native bytes before any allocator callback can
reenter the loader, marks truncation and escapes non-UTF-8 bytes when necessary.
Diagnostic allocation failure never replaces the primary operation error.

Serialize each handle with its close. A symbol borrows module storage, and
NULL can be a successful POSIX data-symbol address. Stop calls, release plugin
objects, disconnect callbacks and finish plugin TLS destructors before unload.
Close consumes the handle even on native failure; an explicit C++ close can
report errors, whereas the destructor cannot throw. Loading/unloading may run
module constructors/destructors and can block.

`examples/load_plugin.c` loads the test module, negotiates ABI using the
library's ABI query, and reads the plugin version. CTest builds this fixture
itself; the example needs no preinstalled plugin or external process.

C callers can use `hx_os_library_bind` or `_bind_ex` to write a native
function pointer directly into a typed variable, passing its address and
`sizeof(variable)`. This avoids repeating pointer conversion code. The
destination stays unchanged on failure.

## File and path conveniences

`HX_OS_ENABLE_FILE_UTILS` defaults to ON. Read text directly with
`hx_os_file_read_text(path, &text, &error)` and publish an existing `hx_string`
with `hx_os_file_write_text(path, &text, &error)`. Reading is strict UTF-8,
removes an initial UTF-8 BOM and limits the entire input to 16 MiB. The explicit
load APIs accept a different bound/BOM policy. Invalid input, exhausted limits,
allocation failure and I/O failure leave an existing output string unchanged.
Binary loads own their returned memory, including an empty allocation for an
empty file; release it with `hx_os_memory_free`.

Atomic saving creates an exclusive temporary in the target directory, writes
all bytes, requests file persistence, closes and renames it over the target.
It replaces the directory entry (including a destination link) and can change
file identity/metadata. The extended result distinguishes publication from
parent-directory synchronization. A successful publication is not a guarantee
against power loss. Windows directory synchronization returns NOT_SUPPORTED in
the result, without turning completed publication into failure. Failed staging
attempts remove their temporary where possible; permissions or external
mutation can prevent that cleanup.

Copy rejects destructive copies onto the same file and does not promise
metadata preservation or a snapshot of a concurrently changing source. Plain
save/copy can leave partial output on failure. Explicit backup is a copy,
without a persistence guarantee. Replace with backup uses native replacement
on Windows and two renames on POSIX. Existing backup names are rejected; the
caller excludes concurrent changes to all participating paths. There is no
automatic rollback if a later step fails. The POSIX path deliberately uses
ordinary rename after the collision check, including on WSL mounts that do not
support no-replace rename.

Current/absolute paths have buffer and `hx_string` variants. Absolute resolution
requires an existing target and resolves links. Changing cwd is explicitly
process-global: callers coordinate all relative-path operations. Lexical
basename/dirname/extension views are allocation-free, recognize both separator
styles, and remain available with FILE_UTILS disabled. An extension includes
its dot; a leading-only dot does not count.

Glob returns an owned vector of owning `hx_string` values; destroy it with
`hx_os_sequence_destroy`. Matching supports UTF-8 `*`/`?`, with `?` consuming a
codepoint, and is case-sensitive within matched components. Literal directory
prefixes follow native path lookup. No character classes/escaping or recursive
`**` expansion are provided. Dot names require a literal leading dot. Empty
matches succeed; order is unspecified. It needs CONTAINERS as well as FILE_UTILS.

Walk defaults are depth 64 (maximum configurable 256), one million visited
entries, 65,536 matches, and separate 16 MiB budgets for vector storage and
retained text. Traversal frames are also bounded; native directory buffers are
outside allocator accounting. Recursive removal does not follow links, uses
heap traversal frames and can partially complete before a limit or failure.
POSIX traversal opens children relative to directory descriptors with NOFOLLOW;
Windows opens reparse points themselves and holds directories against rename.
Concurrent external mutation remains a caller coordination boundary.

```cpp
#include <hx/Files.hpp>
auto text = hx::Files::readText("settings.json");
hx::Files::writeText("settings.json", text);
hx::File file("input.bin");
hx::Directory directory(".");
std::string name;
while (directory.next(name)) {
    // Use the owned UTF-8 name.
}
```

`File`/`Directory` uniquely own core handles even when FILE_UTILS is disabled.
`File::get()` borrows its handle. Position operations require caller
serialization. `readSome` distinguishes EOF from a zero-byte request, and
`IoError::completed()` retains byte progress on I/O failure. `Files` wraps the
optional convenience layer and returns standard C++ strings/vectors.

`examples/text_file.c` reads UTF-8 in one call and optionally atomically
publishes it to a second path; CTest runs the same C source on both hosts.

## Environment

`HX_OS_ENABLE_ENV` defaults to ON. `hx_os_env_get_string` returns owned UTF-8
using the output string's context; `hx::Environment::get` returns std::string.
`getOr` uses its fallback only when the name is missing. An empty value remains
an empty value. Buffer queries include NUL in required capacity.

`hx_os_env_set(name, value, replace, error)` explicitly changes the process
environment; replace=0 leaves even an empty existing value intact. Removing a
missing name succeeds. Inputs are borrowed during the call, names cannot be
empty or contain '=', and invalid UTF-8 is rejected. Values are copied by the
OS. Changes affect future children, not the parent or already-running children.

Calls through one loaded library instance serialize. Native/CRT/another copy
of the library must not mutate the environment concurrently. Output allocator
callbacks run outside this lock and may call the getters. String capacity
retries are limited to eight attempts; continual growth returns BUSY while
preserving the old output. Windows uses the Win32 Unicode environment; CRT
`getenv`/`_environ` caches are separate and may not observe Win32 changes.
Use these getters and setters together. POSIX native non-UTF-8 values report
INVALID_ENCODING instead of changing the locale or guessing an encoding.

```cpp
#include <hx/Environment.hpp>
auto value = hx::Environment::getOr("APP_MODE", "normal");
hx::Environment::set("APP_MODE", "diagnostic");
```

`examples/environment.c` queries a named variable directly. Its CTest supplies
a private test value in the child environment and does not edit host settings.

## Special paths

With PATHS enabled (default), use `hx_os_path_config_home_string(&path, &error)`
or `hx::Paths::configHome()`. All seven named queries have buffer and owning-string
forms. No global initialization, directory creation or cwd change is performed.
Paths are absolute UTF-8 without trailing separators except roots. They need not
exist or be writable. Required size includes NUL; string failure preserves its
old value and allocator.

| Query | Windows | Linux | macOS |
| --- | --- | --- | --- |
| home | Absolute USERPROFILE, else Profile folder | Absolute HOME, else current UID database | Same POSIX home lookup |
| config_home | RoamingAppData | Absolute XDG_CONFIG_HOME, else HOME/.config | HOME/Library/Application Support |
| data_home | LocalAppData | Absolute XDG_DATA_HOME, else HOME/.local/share | HOME/Library/Application Support |
| program_data | ProgramData | /var/lib | /Library/Application Support |
| temp | GetTempPathW | Absolute TMPDIR, else /tmp | Absolute TMPDIR, else Darwin user temp, then /tmp |
| executable | GetModuleFileNameW | /proc/self/exe | _NSGetExecutablePath + realpath |
| module | This library's directory | Absolute dladdr module directory | Absolute dladdr module directory |

`program_data` is machine-wide application data/state, separate from user data
and read-only installed resources. Append your application subdirectory and
explicitly create it when needed. Empty/relative XDG values are ignored per the
[XDG specification](https://specifications.freedesktop.org/basedir/latest/).
Invalid UTF-8 remains an error. Windows uses SHGetKnownFolderPath with
KF_FLAG_DONT_VERIFY and CoTaskMemFree; tests invoke it without CoInitialize.
No Objective-C or new third-party dependency is needed.

`module` means the executable directory for static linkage. POSIX loaders that
report only a relative module name return NOT_SUPPORTED, avoiding incorrect
resolution after cwd changes. Linux executable lookup requires procfs and
preserves native failure. Renamed/deleted images may yield their recorded name,
not an existing path. POSIX system path scratch is bounded to 1 MiB. Queries may
block in native folder providers or the UID database.

PATHS works independently of ENV, FILE_UTILS and LIBRARY. Private environment
reading shares its lock/codec while public ENV symbols still report unsupported
when disabled. The host coordinates native environment/cwd mutation. No hidden
thread or hot-path lock is added; explicit queries allocate native/string scratch.
`examples/special_paths.c` demonstrates the same direct C call on both hosts.

## Thread options, names and semaphores

THREAD_EX defaults to ON and adds no background threads. Use
`hx_os_thread_create_with_options` with `HX_OS_THREAD_OPTIONS_INIT` and an
optional byte-sized stack reservation (zero uses native defaults). The explicit
initializer receives the caller's capacity and writes only the known prefix;
creation validates size/version before reading the stack field. POSIX rejects
sizes below its native minimum or alignment requirement. Windows retains
_beginthreadex for CRT cleanup and uses STACK_SIZE_PARAM_IS_A_RESERVATION;
values beyond its unsigned-size parameter return RANGE. Existing create uses
the same default native path and does not add an allocation or lock.

Names are strict UTF-8. Linux and macOS truncate at a complete codepoint within
15 and 63 bytes respectively; macOS cannot name another thread. Windows uses
runtime lookup of SetThreadDescription in KernelBase, as required for early
Windows 10 versions; unavailable implementations (including Win7) return
NOT_SUPPORTED. Native failures retain errno/HRESULT. Exclude concurrent join
and destroy when naming an object. A joined thread returns CLOSED.

The semaphore is a mutex/condition-based bounded token count. post(count>0)
retains tokens without waiters and atomically rejects overflow with LIMIT.
wait_until consumes one available token even at NOW, otherwise waits against
an absolute monotonic deadline. Timeout retains the object; INFINITE is
explicitly allowed. No FIFO or hard return deadline is promised. Destroy only
after excluding operations and waiters. Creation uses three context allocations;
post/wait allocate nothing and take one mutex. It has no worker or queue.

`hx::Semaphore` owns its synchronous resource with movable RAII. `hx::Thread`
uses the same explicit owner-variable facade as ThreadPool: closeUntil joins
and destroys on success, retains ownership on timeout, and never waits or
terminates from its destructor. Callback data remains borrowed until join.
`examples/thread_options.c` shows stack selection and an explicit close policy;
small embedded stacks must account for callbacks, TLS and sanitizer overhead.

## Pipes and local I/O loops

`hx_os_pipe_pair_anonymous` creates two synchronous file endpoints. Use the
normal file read/write/close calls. Both platforms use blocking byte streams;
no waiter thread is created. Disk-only operations report NOT_SUPPORTED. A zero
byte operation succeeds; after all writers close, buffered bytes are consumed
before EOF. Broken writes report CLOSED with the native code. Destruction must
not race a synchronous call. Linux uses pipe2 with CLOEXEC; macOS sets CLOEXEC
with fcntl, so the host must serialize concurrent fork/exec during creation.

For deadlines and cancellation use `hx_os_pipe_pair_async` and
`hx_os_pipe_read_async` / `hx_os_pipe_write_async`. Writes copy by default and
complete all bytes; extended submit can borrow. Reads complete with available
bytes or EOF. The new pipe operation has a capacity-aware initializer and does
not change hx_os_operation. Bytes are raw, without automatic text decoding.
`hx_os_pipe_pair_child` creates an async parent and a synchronous child endpoint
in either direction, initially non-inheritable; process spawning later chooses
what to duplicate into the child.

`hx_os_loop_create_local(NULL, NULL, &loop, &error)` needs no network object or
network initialization. It accepts posts, timers and local I/O; network/DNS
operations report NOT_SUPPORTED. The original network factory remains valid.
LOOP and NETWORK are independently selectable. PIPE without LOOP still supports
synchronous endpoints; ASYNC_PIPE capability requires both PIPE and LOOP.

One request per pipe endpoint is accepted, on the loop creator thread. Pipe
requests share the loop's max_requests and max_pending_bytes, including borrowed
buffer sizes. Rejection retains nothing and invokes no callback. Completion is
exactly once on the poller, never inline. Request waiting includes callback
return; it does not drive the loop. POSIX uses nonblocking descriptors and the
same epoll/kqueue registrations as sockets. Windows uses private, one-instance,
byte-mode named pipes with overlapped parent endpoints and IOCP; anonymous
CreatePipe handles remain synchronous. No hard deadline is promised.

close_until marks the endpoint closing, requests cancellation and drives native
retirement. Timeout retains the pointer, native state and buffers for retry.
Other loop callbacks can run during close; waiting for unfinished work in a
callback or reentering an active close returns DEADLOCK. Close attached pipes
before destroying their loop. Final native close consumes the endpoint even
on a native close error; never retry that consumed pointer. Closing the loop
cancels requests but does not implicitly free attached endpoints.

POSIX pipe writes temporarily block SIGPIPE only in the calling thread, preserve
previously pending SIGPIPE, consume a newly generated broken-write signal and
restore the original mask. No process-level handler is installed. The host
must not concurrently consume this thread's pending SIGPIPE through another
signal-waiting mechanism. Cancellation can report native partial byte progress;
it does not promise that a failed/cancelled write delivered zero bytes.

C++11 `hx::Pipes::anonymous()` returns a pair of RAII File objects. `IoLoop` and
`AsyncPipe` preserve explicit C owner variables for retryable close. `IoRequest`
owns a reference; dropping it neither cancels the request nor destroys borrowed
read/capture data. Lambda callbacks are copied into retained C++ state. Exceptions
are caught at the C boundary and can be rethrown with rethrowCallbackError after
completion. Those callback/capture allocations use the C++ allocator and lie
outside the C buffer budget; the accepted request count still bounds their
number. C entry points do not add those allocations.

`examples/pipe_transfer.c` performs a complete transfer with a local loop and
explicit shutdown. Existing regular-file I/O adds a pipe-kind branch, without
new allocations or locks. Socket poll records now share a two-int readiness
record; no extra socket allocation/syscall is introduced. Async pipes reuse
request allocation/queue locking; default writes add their owned copy. POSIX
writes additionally perform thread signal-mask/pending-signal operations.

## Socket options and positioned file I/O

NET_OPTIONS defaults to ON and requires NETWORK for its capability. Call
hx_os_socket_set_nodelay, set_keepalive or set_reuseaddr directly with a boolean.
These select the correct native levels/names internally. Address reuse stays
opt-in: create and bind do not enable it. Native reuse semantics and keepalive
scheduling remain platform-specific. Setters return BUSY once a socket belongs
to a loop, even between requests. The borrowed `hx::SocketOptions` facade never
closes a socket; it provides these setters and peerAddress().

The raw option_set/get escape hatch uses target-native level/name/value layouts
from the target SDK. Length is bytes, capped at INT_MAX; get returns the actual
native length on success and does not implement two-phase sizing. Failure keeps
the input length but the native buffer may have changed. In the tested Windows
provider TCP_NODELAY returned one byte even with an int-sized buffer, so callers
must inspect the returned length. C++ get<T> requires an exact size match and
reports RANGE otherwise; it cannot infer arbitrary native option value types.
Peer lookup preserves output on failure; an unconnected socket retains the
native getpeername error. Serialize calls with socket use and exclude close.

POSITIONED_IO is independent of FILE_UTILS. `hx_os_file_read_at` and write_at
accept an absolute byte offset and return a single chunk plus exact native
progress. Short successful I/O is distinct from EOF; zero requested bytes
succeeds. Requests are capped at INT_MAX, offsets/ranges must fit signed 64-bit
native positions. Callers handle additional chunks/retries. Append-mode writes
are NOT_SUPPORTED, and wrong open-mode access reports ACCESS_DENIED. Pipes are
not seekable. These calls may block and have no cancellation/deadline promise.

POSIX uses pread/pwrite with EINTR retry. Windows lazily reopens the existing
file object with OVERLAPPED using ReOpenFile, caches that handle with native
interlocked publication, and uses a distinct event/OVERLAPPED for each call.
It never seeks/restores the shared cursor and never upgrades the original open
mode. All overlapping operations retain the file until completion; the owner
excludes close/truncate and coordinates overlapping writes. Final file close
releases both handles. `hx::File::readAt` distinguishes EOF and `writeAt` returns
partial progress; IoError preserves progress on failure.

Regular open/read/write add no allocation or lock for the positioned feature.
The Windows private file object grows to 24 bytes on x64 (16 on x86), POSIX to
12 bytes. First positioned use can open competing candidate handles; only the
published handle remains. Each positioned Windows call has atomic cache lookup,
one event create/close, native I/O and a completion wait when needed. POSIX adds
only its positioned syscall. This is a cost inventory, not a throughput claim.

`examples/positioned_read.c` reads four bytes at offset four without a seek.
The Windows pipe adapter also now supplies retained non-NULL byte-count storage
to ReadFile/WriteFile, required by the documented Windows 7 contract; final
async progress still comes from IOCP completion. Win7 native execution is pending.

## File information, directory kinds and BOM

FILE_INFO (default ON) adds hx_os_path_stat_extended and
hx_os_directory_next_info. Initialize the extended output with
HX_OS_FILE_INFO_EXTENDED_INIT or the capacity-aware initializer. Existing file
information layouts and directory-next signatures remain unchanged. Query
failure preserves the extended output; success writes only the known structure.
Follow mode is explicit and names remain UTF-8. The native projection is shared
with the old stat path, avoiding duplicate filesystem logic.

Timestamps are signed Unix seconds with nanoseconds in [0, 1e9). Metadata change
time uses POSIX ctime or Windows FILE_BASIC_INFO.ChangeTime, not Windows creation
time. Resolution, update deferral and access-time suppression follow the
filesystem. Windows queries both metadata records through one open file handle;
concurrent mutation can still change fields between native calls. read_only is
the Windows attribute or absence of the POSIX owner-write mode bit. POSIX reports
known owner-read/write mode bits; Windows marks both unknown instead of guessing
ACL permissions. These summaries are not an authorization check, nor a statement
about read-only mounts. Windows ignores its read-only attribute on directories.

Directory info classifies the entry itself, without following links. Windows
reparse points report LINK; POSIX uses d_type when known and otherwise fstatat
relative to the open directory. Concurrently removed entries may produce a
native lookup failure. OUTPUT_FULL and native lookup failure retain the pending
entry/kind output; retry with capacity or use legacy next to skip a stale entry.
Invalid UTF-8 entries retain the existing skip-and-error behavior. Enumeration
order and snapshot consistency are not promised. The legacy call adds no type
lookup syscall. C++ Files::info and Directory::next(name, kind) provide direct
owned results. examples/file_info.c queries metadata in one function call.

BOM is an independent default-on pure utility. hx_os_text_bom_detect recognizes
the longest complete UTF-8/16LE/16BE/32LE/32BE marker, checking UTF-32 before the
shared UTF-16 prefix. No marker returns NOT_FOUND and prefix_bytes=0 while keeping
encoding unchanged. This is not streaming encoding detection: a two-byte FF FE
buffer is a complete UTF-16LE marker; obtain four bytes when identifying a
possible UTF-32 stream. Detection does not validate or convert the payload.
hx_os_text_bom_bytes generates at most four bytes without a terminator; encodings
without a supported BOM report NOT_SUPPORTED. C++ Text::detectBom exposes found,
bytes and encoding; encoding is valid only when found is true. The byte generator
returns an owned byte vector. The core codec still adds/removes no BOM implicitly.

The C BOM functions allocate nothing and take no locks. Stat and directory info
create no background activity; native metadata queries may block. POSIX known
directory types add no stat; unknown types require one fstatat. Windows extended
stat adds one FileBasicInfo query, but shares the existing handle and projection.

## Configuration serialization and numeric text

CONFIG_SERIALIZE defaults to ON and requires CONFIG. Use
hx_os_config_serialize(document, format, &text, NULL, &error) or the one-call
hx_os_config_save(document, format, path, NULL, &error). Save also needs
FILE_UTILS and follows atomic publication's persistence/partial-failure contract.
Documents remain immutable. The serializer always uses its builtin writer,
regardless of the document's parse backend; explicit writer options must select
BUILTIN. YAML, XML, TOML and ENV output currently report NOT_SUPPORTED.

JSON emits compact UTF-8, preserving valid JSON numeric spellings without
conversion through double. TOML integer bases are converted exactly and decimal
underscores/leading plus are removed; non-finite numbers, XML nodes and datetime
nodes cannot silently become JSON strings. INI supports root string values plus
one level of sections containing strings. It emits global keys before sections,
preserving order within those groups; comments/whitespace are not retained.
Keys/values that the INI parser would trim or misinterpret are UNREPRESENTABLE.
Properties supports a flat object of strings, escaping separators, spaces,
backslashes and control characters, while retaining Unicode as UTF-8. Numbers,
bools, arrays, null and nested structures cannot silently become line-format
strings. Unsupported representations leave the previous output unchanged.

Serialization performs a bounded measurement pass followed by one output
allocation and a write pass. Both traversals charge max_work; each obeys
max_nodes/max_depth (maximum 128 recursive frames). max_input_bytes is the
serialized output limit, and max_memory_bytes bounds its allocation including
allocator overhead. Caller-owned document/old output and the bounded C stack are
outside that heap budget. Output uses its hx_string context. Allocator failure
or a limit during either pass preserves the previous output. Concurrent readers
use independent outputs and keep the document alive. No locks, global mutable
settings or background work are introduced in serialization.

TEXT_NUMBER separately exposes parse_double and format_double and can work
without CONFIG. The config getters, numeric grammar and third-party adapters
share the same first-party helpers; their existing behavior is retained even
when the public text module is off. Parsing accepts a complete ASCII token in
the existing numeric grammar (including TOML underscores/bases and signed inf/nan),
without whitespace or embedded NUL; overflow/underflow is RANGE. Formatting uses
17 significant digits for IEEE binary64, canonical nan/inf, and C numeric locale.
It never changes the process locale; POSIX temporarily selects and restores the
calling thread's locale. The locale objects use native C-runtime allocation,
outside injected allocator accounting, as the original config getter did.

C++ ConfigDocument owns an immutable reference, supports copying/moving, typed
pointer queries, stringOr fallback only for missing keys, and serialize/save.
Text::parseDouble/formatDouble provide owned C++ results. C++ serialization uses
a default-context temporary and returns std::string; the C output API remains
available for context-specific allocation. examples/config_save.c serializes
an exact large integer or saves directly when given a filename.

## Periodic timers

TIMER is default-on and depends on LOOP, independently of NETWORK.
hx_os_timer_create(loop, interval_ns, callback, user, &timer, &error) starts
a periodic schedule one positive interval after creation. The creator must
drive the loop. Intervals use monotonic nanoseconds, without a resolution or
hard real-time guarantee. Scheduling stays on the original time axis; missed
periods, including time spent in a callback, coalesce into one tick per poll.
The legacy one-shot operation and its public structure are unchanged.

Callbacks run outside locks on the poller. The error pointer is borrowed until
return: OK means a tick, a terminal clock/range error means the timer stops.
Stop is a thread-safe request; no extra cancellation tick is delivered.
wait_until includes the final callback and optional cleanup returning.
An unfinished owner-thread wait is DEADLOCK. close_until is owner-only and
pumps cancellation; TIMEOUT retains the same object for retry. Once retired,
close consumes the object even when reporting its terminal error. Native clock
failure after a tick may require a separate terminal error notification.

The cleanup factory transfers responsibility only on acceptance. It runs once
outside locks, before retirement is visible. A callback/cleanup cannot close
itself synchronously (DEADLOCK); it may request stop. Keep borrowed user state
alive through retirement. Other threads must finish using the timer before
its owner closes it. Loop close cancels timers, but loop destruction remains
BUSY until their objects are explicitly closed.

Each active timer holds one existing loop request slot. It adds one small
object at creation, with no allocation, thread creation or queue growth per
tick. Due polling reads the monotonic clock and invokes a direct C callback;
the normal request path adds a private timer pointer and a branch. Timer size/
alignment is 40/8 on Windows x86, 64/8 on Windows/Linux x64. Existing public
layouts are unchanged. This resource inventory is not a runtime speed claim.

C++ hx::PeriodicTimer::every(owner, loop, milliseconds, lambda) is a host-owned
facade with explicit closeUntil; destruction never waits or hides the owner's
C pointer. Exceptions stop the timer, then rethrowCallbackError retrieves them
after retirement. Lambda captures are released in cleanup even if the facade
still exists. Native terminal errors are preserved by wait/close. See the
standalone C example examples/periodic_timer.c.

## Pool worker identity

hx_os_thread_pool_inside(pool, &inside) reports whether the calling thread is
one of that live pool's workers, including its task cleanup callback. Invalid
arguments and disabled pools report INVALID/NOT_SUPPORTED without changing the
output. Keep the pool alive during the call; this does not acquire ownership.
It reads the pool's existing thread-local identity: no lock, allocation, syscall
or additional worker bookkeeping. The C++ facade exposes pool.inside().

A worker waiting for an unfinished task from its own pool gets DEADLOCK, including
zero-deadline waits; waiting for an already finished task is permitted. Closing
one's own pool also reports DEADLOCK. False for another pool does not prove that
waiting there is safe: cross-pool dependency cycles remain the caller's concern.
No work stealing, inline task execution or dependency graph is added.

```c
int inside;
hx_os_status status = hx_os_thread_pool_inside(pool, &inside);
if (status == HX_OS_OK && !inside)
	status = hx_os_task_wait_until(task, deadline, &work_status, &error);
/* Inside a worker, continue asynchronously instead of waiting for new work. */
```

## Native error text

hx_os_error_message now appends a strict UTF-8 diagnostic for GAI errors and,
on Apple, Mach errors. The existing status/domain/code prefix and all other
domain behavior are preserved. Unavailable, oversized, non-UTF-8 or out-of-int
native messages keep the numeric fallback. cause_code remains available in the
original error object; formatting never modifies it. required includes NUL;
a short buffer is terminated when capacity is nonzero, but partial output need
not end on a UTF-8 boundary. Use the reported capacity for the complete message.

POSIX uses gai_strerror; Apple additionally uses mach_error_string. Windows
uses FormatMessageW into call-local storage followed by strict UTF-8 conversion,
without WSAStartup, working even with NETWORK disabled. Microsoft's
[gai_strerrorW documentation](https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-gai_strerrorw)
warns about shared, non-thread-safe storage. Diagnostic wording is native and
locale dependent; do not match it for control flow or concurrently change the
process locale. There is no new first-party heap allocation or lock in C.
C++ StatusError::message() returns an owned std::string with full native detail;
what() retains its existing nonthrowing status-only behavior and object layout.
The hello C example now uses the detailed diagnostic on failure.

## Randomness, UUID, CRC32 and secure clearing

RANDOM defaults to ON. hx_os_random_bytes fills caller storage using the system
cryptographic generator; it never substitutes a time-based or ordinary PRNG.
It may block for native entropy and has no deadline/cancellation promise. NULL
is valid only for zero bytes. Native failure clears the entire output; invalid
arguments or disabled-module rejection leave it unchanged. No library context,
heap allocation or background thread is needed; OS providers can allocate.

Windows resolves BCryptGenRandom from an absolute system-directory DLL path,
using CryptGenRandom only when that entry is unavailable. Each call releases its
module/provider; there is no hidden process-lifetime cache. CNG failures retain
their signed code in the new appended NTSTATUS domain (numeric diagnostic), and
legacy failures retain Win32 details. advapi32 is a private native dependency,
also listed for static pkg-config consumers. Linux uses the getrandom syscall,
retrying short reads/EINTR; only ENOSYS or missing syscall headers take the legacy
path. That path first reads 32 bytes from /dev/random, then /dev/urandom, checks
the expected character-device identities and closes both descriptors. It can
block indefinitely on an entropy-starved old embedded kernel; denied access is
an error. It uses a private device adapter because public file_open intentionally
accepts regular files. Apple uses arc4random_buf; native target runs are pending.
[Linux random-device semantics](https://www.man7.org/linux/man-pages/man4/random.4.html)
explain why early-boot /dev/urandom alone is insufficient for this fallback.

UUID generation sets v4/version and variant bits and preserves the output on
failure. Formatting is lowercase 8-4-4-4-12 plus NUL, accepts overlapping input,
and is available with RANDOM disabled. hx_os_uuid_generate_string(&text, &error)
combines generation and formatting using the output string's own context;
allocation failure also preserves the old string. C++ Random::uuid returns an
owned string, with fill/bytes for raw output. No collision guarantee or entropy
certification is inferred from tests. See examples/random_uuid.c.

CRC32 is always available: reflected CRC-32/ISO-HDLC, polynomial 0xedb88320,
initial/final all-ones xor. Seed zero starts a stream; prior output continues it.
The fixed 16-entry table occupies 64 read-only bytes with two lookups per byte;
there is no lazy initialization, allocation or lock. It detects accidental
corruption and is not a cryptographic integrity check. secure_zero uses
SecureZeroMemory, detected explicit_bzero on Linux, or volatile byte writes.
It prevents elimination of these writes, not residual copies/registers/swap, and
provides no synchronization. Neither utility introduces global configuration.

## Pages and file mappings

VIRTUAL_MEMORY defaults to ON, independently of SYSINFO and FILE_UTILS.
hx_os_pages_alloc rounds a positive byte count to the native page size and owns
a zero-initialized anonymous mapping plus context-allocated metadata. NONE,
READ, READ|WRITE, READ|EXECUTE and READ|WRITE|EXECUTE are supported requests;
WRITE or EXECUTE without READ is NOT_SUPPORTED. Native policy may reject
executable pages; the library does not silently weaken protections. Whole-range
protect includes instruction-cache synchronization when enabling EXECUTE.
Protection failure can leave native partial changes; restore known permissions
before use. OS/hardware permissions are not a security-isolation guarantee.
Callers exclude concurrent access during protection changes and destruction.

hx_os_file_map_open(path, mode, &map, &error) maps a whole regular UTF-8-named
file in one call. create additionally accepts context, open file, byte offset
and length (zero means through EOF). Empty ranges are EOF, oversize ranges are
RANGE; the file is never extended. The implementation aligns the native offset
and returns the requested subview. File cursor is unchanged. Every mapping owns
a duplicate native file resource (plus a Windows mapping object), so the caller
may close its original file after create. Creation still requires exclusion
from closing/truncating that file.

READ provides a const borrowed view; write_view returns NULL. SHARED_WRITE needs
read/write file access and rejects APPEND. PRIVATE_COPY permits private writes
through a read-only file handle and never writes those pages back. Neither mode
is a snapshot; other file mutations and concurrent truncation must be coordinated.
A page fault caused by truncation or storage failure can become SIGBUS or a native
exception outside a C status return. Remote filesystem and mixed mapped/ordinary
I/O coherence follow the native system, not an added library guarantee.

Only SHARED_WRITE can flush: writeback uses msync(MS_SYNC)/FlushViewOfFile;
durable=1 also requests fsync/FlushFileBuffers on the owned resource. Success is
not proof of hardware durability; failure leaves the map usable for retry.
Destroy does not implicitly sync. An unmap failure retains the object; after
unmap it is consumed even if native handle close reports an error. Pages free
similarly retains its object on failure. Borrowed views cannot outlive the object.
See [Windows flush semantics](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-flushviewoffile)
and [POSIX/Linux mapping behavior](https://www.man7.org/linux/man-pages/man2/mmap.2.html).

C++ Pages and MappedFile provide move-only synchronous RAII. Call close explicitly
to observe native release errors; destructors cannot report them and never issue
implicit flush. C mapping convenience is demonstrated in examples/mapped_file.c.
Each object adds one metadata allocation; actual pages are OS-managed and outside
context heap accounting. Map creation adds duplication and mapping system APIs,
with no shared library lock, background work or access-time wrapper. Layout
(size/alignment): Windows x86 pages 8/4, map 28/4; Windows x64 16/8, 56/8;
Linux x64 16/8, 40/8. Existing public structures remain unchanged.

## File range locks, timestamps and permission summaries

FILE_EXTRA defaults to ON and needs neither FILE_INFO nor FILE_UTILS.
lock_at(file, offset, bytes, shared, deadline, error) attempts a positive byte
range without moving the cursor; its signed-64-bit end must be representable.
Shared locks require READ, exclusive locks WRITE. NOW attempts once; conflicts
then become TIMEOUT. Finite/infinite waits use monotonic deadlines and native
nonblocking lock attempts with requested sleep increments up to 10 ms. Native
calls/scheduling can overrun a deadline. Failure retains the file, with native
conflict detail on TIMEOUT. Unlock matches a successfully acquired range.
There is no library allocation, mutex or worker for lock/unlock.

POSIX fcntl record locks are process-owned and advisory; closing any descriptor
for the inode, including a mapping's duplicate, can drop the process's locks.
Windows LockFileEx locks belong to the handle and affect ordinary conflicting
I/O, but not mapped views. Its positioned I/O adapter uses a different handle,
so use cursor I/O through the locking handle while the region is held. Do not
use these locks as thread mutexes, overlap/upgrade/reenter ranges, or assume
fairness. See [POSIX/Linux locking](https://www.man7.org/linux/man-pages/man2/fcntl_locking.2.html)
and [Windows range locks](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex).
C++ FileRangeLock borrows File through unlock/destruction, uses NOW by default,
and transfers a lock only by move construction. Explicit unlock reports errors.

Initialize file times with HX_OS_FILE_TIMES_INIT or the capacity-checked init.
fields selects access/modify seconds plus nanoseconds since Unix epoch; omitted
fields are unchanged. Zero fields is a no-op, not an existence/access check.
Nanoseconds outside [0,1e9), unsupported native time ranges or bad size/version
are errors. POSIX uses utimensat with UTIME_OMIT and explicit nofollow; Windows
uses a wide-path attribute handle and SetFileTime, truncating to 100 ns. Native
filesystem precision can be coarser. Creation/metadata-change time is not set.
Windows cannot represent dates before 1601; 32-bit time_t targets may reject
otherwise valid 64-bit times. Updates can already be applied if close fails.

Permission changes replace POSIX owner read/write/execute bits while preserving
group/other mode. Concurrent path replacement and permission changes need caller
exclusion. Nofollow must be supported or fail, never silently follow a symlink.
On Windows READ must be present and EXECUTE is unsupported; WRITE only controls
the read-only attribute. No ACL editing/effective-access guarantee is provided.
The existing v1 stat permission summary remains read/write only. C++
FileAttributes::setTimes/permissions and examples/file_attributes.c offer direct
calls. Windows path conversion/attribute handles are transient; POSIX uses
native metadata calls without injected heap allocation. The new public times
structure is 40/8 bytes on Windows x86/x64 and Linux x64; old layouts are intact.
