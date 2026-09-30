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
