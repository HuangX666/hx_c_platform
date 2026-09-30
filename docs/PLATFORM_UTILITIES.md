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
