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
