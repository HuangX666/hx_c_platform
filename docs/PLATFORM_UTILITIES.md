# Platform utilities

[简体中文](PLATFORM_UTILITIES.zh-CN.md)

This guide covers the cross-platform OS utilities in `hx_c_platform`: system hardware and resource inspection, dynamic library (plugin) loading, file and path conveniences, environment variables, standard OS directories, thread options and semaphores, anonymous and asynchronous pipes, positioned file I/O, virtual memory pages and file mappings, directory monitoring, child process spawning and output capture, local (`AF_UNIX`) sockets and duplex pipes, Windows registry access, sleep-inhibition power requests, UTC calendar formatting and translation tables, synchronous signals with stack parameter packs, mutable hierarchical settings, and the bounded interval profiler.

Pick a task from the table below to build and run a complete working example first (see the [example instructions](../examples/README.md#run) and [input setup](../examples/README.md#inputs) for build targets and sample input files):

| Task | Complete Example Program | Workflow and Expected Output |
| --- | --- | --- |
| Inspect OS version and current-process CPU usage | [system_info.c](../examples/system_info.c) | Query OS version → create CPU sampler → query after a short sleep → print version and CPU percentage |
| Read a UTF-8 text file and optionally publish an atomic copy | [text_file.c](../examples/text_file.c) | `hx_os_file_read_text` → optional `hx_os_file_write_text` → display text → destroy string |
| Read four bytes from the middle of a file without seeking | [positioned_read.c](../examples/positioned_read.c) | Open file → `hx_os_file_read_at(offset=4)` → close (`demo.txt` prints `34 35 36 37`) |
| Memory-map an existing file and compute its CRC32 | [mapped_file.c](../examples/mapped_file.c) | `hx_os_file_map_open` → inspect borrowed byte view → print byte size and CRC32 → destroy mapping |
| Read a process environment variable | [environment.c](../examples/environment.c) | Pass a variable name such as `PATH` → `hx_os_env_get_string` → print value (read-only) |
| Locate standard user configuration and data directories | [special_paths.c](../examples/special_paths.c) | `hx_os_path_config_home_string` → print resolved path (does not create the directory on disk) |
| Monitor a directory for live file changes | [watch_directory.c](../examples/watch_directory.c) | Create watcher → process callbacks for five seconds → close cleanly (edit a file from another terminal) |
| Spawn a child process and capture its `stdout` / `stderr` | [capture_process.c](../examples/capture_process.c) | Pass executable and arguments → `hx_os_process_capture_run` → wait → read captured output and close |
| Dynamically load an application plugin and bind functions | [load_plugin.c](../examples/load_plugin.c) | Open library → verify ABI → bind function pointers → invoke → unload (pairs with the [public plugin](../examples/README.md#plugin)) |
| Transfer bytes over local asynchronous pipes | [pipe_transfer.c](../examples/pipe_transfer.c) | Create local loop and pipe pair → submit async read/write → poll completion → close endpoints and loop |

Every module exposes a direct C99 API along with an optional exception-free C++11 header under `include/hx/` wrapping the same implementation. Passing a `NULL` `hx_context *` selects the built-in immutable default allocator. When a feature switch is disabled at CMake configure time, its declarations and stub functions remain linkable, its capability bit is cleared, and calls return `HX_OS_NOT_SUPPORTED`.

## System information

`hx_os_sysinfo.h` (controlled by `HX_OS_ENABLE_SYSINFO`, `ON` by default) queries physical and available memory, logical and physical CPU topology, page size, filesystem space, current-process memory usage, and the OS version string. Library version and ABI queries remain available even when `HX_OS_ENABLE_SYSINFO=OFF`. None of these calls spawn background threads, install signal handlers, or mutate process state:

```c
hx_string version = HX_STRING_INIT;
hx_os_error error;

hx_os_status status = hx_os_sys_version_string(&version, &error);

// Check status, then use hx_string_c_str(&version).
hx_string_destroy(&version);
```

- **Struct initialization and buffer conventions**: Initialize versioned disk and process info structs with their `*_INFO_INIT` macros. Buffer-based text queries report the required capacity including the terminating `NUL`. On failure, scalar and struct outputs remain untouched.
- **CPU topology, RAM, and disk space semantics**:
  - **CPU counts**: Report online system CPUs (across all processor groups on Windows) rather than the current process's affinity mask or container `cgroup` quota. On Linux, physical core counts deduplicate online `package/core` pairs from `sysfs`; if physical topology is unavailable, the call returns `HX_OS_NOT_SUPPORTED` rather than passing off logical cores as physical cores.
  - **Available RAM and disk space**: Available RAM reads Windows available physical memory, Linux `MemAvailable`, or macOS free plus inactive pages (snapshots are point-in-time estimates and do not guarantee that a subsequent allocation will succeed; Linux queries reflect the host OS view rather than a container limit). POSIX available disk space uses `f_bavail` (blocks available to unprivileged users) rather than total free blocks `f_bfree`.
  - **Current-process memory**: Resident bytes report the working set / RSS. Virtual bytes report reserved plus committed virtual address space on Windows, `/proc/self/statm` size on Linux, and Mach virtual size on macOS (virtual address size is not commit charge or physical RAM usage).
- **Process CPU usage sampler (`hx_os_cpu_usage` / `hx::CpuUsage`)**: `hx_os_cpu_usage_create` captures an initial baseline of process CPU time and the monotonic clock. Each subsequent query returns the average CPU percentage since the previous sample: `100 * process_cpu_delta / elapsed_delta / initial_logical_cpu_count` (excluding child processes). A zero-duration interval returns `HX_OS_WOULD_BLOCK` without advancing the baseline. Serialize access to each sampler instance. In C++11, methods return `hx::Result<T>` (see [Result contracts](CPP_RESULTS.md) and [system_info.c](../examples/system_info.c)):

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

## Dynamic libraries

`HX_OS_ENABLE_LIBRARY` defaults to `ON`. `hx_os_library_open`, `hx_os_library_symbol`, and `hx_os_library_close` form the core C API, while `_ex` variants accept an initialized `hx_string` to capture loader diagnostic text. In C++11, `hx::Library` provides move-only RAII ownership and typed symbol lookup via `symbol<Pointer>(name)`:

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

- **Path resolution and loader diagnostics**: Input paths are UTF-8 filesystem paths. Relative filenames are resolved against the current working directory rather than searched as bare POSIX `soname` strings (transitive dependencies of the loaded module still follow the native OS loader's search policy). POSIX uses `RTLD_NOW` with `LOCAL` or `GLOBAL` visibility; Windows ignores visibility flags. Because POSIX `dlerror()` returns text rather than a portable numeric code, failures set the error domain to `HX_OS_ERROR_LOADER` and copy up to `2,047` bytes of diagnostic text into stack scratch before invoking any allocator callback that might re-enter the loader.
- **Direct function-pointer binding (`hx_os_library_bind`)**: C callers can pass the address of a typed function-pointer variable and `sizeof(variable)` to `hx_os_library_bind` or `hx_os_library_bind_ex` to look up and assign the symbol in one step without repetitive pointer casts. On failure, the destination variable is unchanged.
- **Safe unloading**: Serialize symbol lookups with `close`. Before unloading a plugin, ensure all active calls into the module have returned, plugin-allocated objects are destroyed, callbacks are unregistered, and plugin TLS destructors have finished. `close` consumes the handle even if the native unload call reports an error. Follow the [run steps](../examples/README.md#plugin) to build and run [load_plugin.c](../examples/load_plugin.c) and [demo_plugin.c](../examples/demo_plugin.c), which print `Compatible plugin: Example plugin 1.0`.

## File and path conveniences

`HX_OS_ENABLE_FILE_UTILS` defaults to `ON` and provides one-call text and binary file I/O, atomic file publication, safe copy and backup replacement, lexical path views, wildcard `glob`, and bounded directory traversal and removal:

- **One-call text and binary file I/O**:
  - `hx_os_file_read_text(path, &text, &error)` validates strict UTF-8, strips a leading UTF-8 BOM, and caps file size at `16 MiB` by default (explicit `load` variants allow custom size limits and BOM policies). Any encoding, size-limit, allocation, or I/O failure leaves the output string unchanged.
  - `hx_os_file_write_text(path, &text, &error)` writes an `hx_string` to disk (see [text_file.c](../examples/text_file.c)).
  - Binary load functions return caller-owned memory (including a valid empty allocation for a zero-byte file); free it with `hx_os_memory_free`.
- **Atomic save and replace-with-backup**:
  - Atomic save creates an exclusive temporary file in the target directory, writes all bytes, flushes file data to the OS, closes the handle, and atomically renames it over the destination path. Extended results distinguish file publication from parent-directory synchronization (on Windows, directory synchronization reports `HX_OS_NOT_SUPPORTED` in the extended result without turning a completed file publication into a failure).
  - File copy rejects copying a file onto itself; replace-with-backup uses native replacement on Windows and two renames on POSIX (rejecting existing backup filenames to avoid clobbering prior backups).
- **Paths, working directory, and `glob`**:
  - `basename`, `dirname`, and `extension` return allocation-free borrowed string views, recognize both `/` and `\` separators, and remain available even when `HX_OS_ENABLE_FILE_UTILS=OFF` (extensions include their leading `.`, while a leading dot on a dotfile alone does not count as an extension).
  - Changing the current working directory (`cwd`) is process-global; coordinate relative-path operations across threads. Absolute-path resolution requires an existing target and resolves symlinks.
  - `hx_os_path_glob` (requires both `HX_OS_ENABLE_CONTAINERS` and `HX_OS_ENABLE_FILE_UTILS`) returns an owned vector of `hx_string` paths (destroy with `hx_os_sequence_destroy`). It supports case-sensitive UTF-8 `*` and `?` (one Unicode code point) within path components, requires an explicit leading `.` for dotfiles, and does not implement `[...]` character classes or recursive `**` expansion.
- **Directory traversal and recursive removal**:
  - Directory walks and recursive removal default to a maximum depth of `64` (configurable up to `256`), `1,000,000` visited entries, `65,536` matches, and separate `16 MiB` budgets for vector storage and retained strings. Recursive removal **never follows symlinks** (POSIX opens child directories via descriptor-relative `openat` with `O_NOFOLLOW`; Windows opens reparse points themselves and holds open directory handles during traversal). Recursive removal rejects drive-relative dot and dot-dot components on Windows. An explicit empty parent is invalid when creating a temporary file or directory. Pass `NULL` to select the native temporary directory.

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

In C++11 `<hx/Files.hpp>`, `hx::File::open` and `hx::Directory::open` return move-only owners (available even with `HX_OS_ENABLE_FILE_UTILS=OFF`). `File::read` treats end-of-file as a successful `ReadResult{bytes, eof}`, while `readSome` / `readAt` preserve byte progress accessible via `hasValue()` on `HX_OS_EOF` or error. For single-directory callback enumeration, use `hx_os_directory_visit` or `hx::Directory::forEach` (see [visitor and text-view contracts](COMMON_UTILITIES.md)).

## Environment

`HX_OS_ENABLE_ENV` defaults to `ON`. `hx_os_env_get_string` allocates through the output string's `hx_context` (returning `HX_OS_NOT_FOUND` when the variable is unset; see [environment.c](../examples/environment.c)). In C++11, `hx::Environment::get` and `getOr` return `hx::Result<hx::String>`, applying the fallback value only when the variable is completely absent (an existing empty variable remains `""`):

```cpp
#include <hx/Environment.hpp>

hx::Result<hx::String> application_mode() noexcept
{
	return hx::Environment::getOr("APP_MODE", "normal");
}
```

- **Mutating environment variables**: `hx_os_env_set(name, value, replace, &error)` updates the current process environment (`replace == 0` preserves any existing value, including an empty string), and unsetting an already absent variable succeeds. Names cannot be empty or contain `'='`. Changes apply to future child processes rather than the parent or already-running children.
- **Concurrency and OS encoding rules**: Calls through the same loaded library instance serialize internally (if external concurrent growth causes eight consecutive buffer-sizing retries to fall short, getters return `HX_OS_BUSY` and leave the output unchanged). On Windows, the library reads and writes the Win32 Unicode environment block directly; because CRT `getenv` / `_environ` maintain a separate cache that may not reflect Win32 changes, use `hx_os_env_*` getters and setters together. On POSIX, non-UTF-8 native values fail with `HX_OS_INVALID_ENCODING`.

## Special paths

With `HX_OS_ENABLE_PATHS=ON` (default; independent of `ENV`, `FILE_UTILS`, and `LIBRARY`), call `hx_os_path_config_home_string(&path, &error)` in C or `hx::Paths::configHome()` in C++11 (see [special_paths.c](../examples/special_paths.c)). All seven queries offer both buffer and owning-string forms, return absolute UTF-8 paths without trailing separators (except for filesystem roots), and **never create directories on disk or change the working directory**:

| Query | Windows Source | Linux Source | macOS Source |
| --- | --- | --- | --- |
| `home` | Absolute `USERPROFILE`, else known Profile folder | Absolute `HOME`, else current UID passwd entry | Same POSIX lookup as Linux |
| `config_home` | `FOLDERID_RoamingAppData` | Absolute `XDG_CONFIG_HOME`, else `HOME/.config` | `HOME/Library/Application Support` |
| `data_home` | `FOLDERID_LocalAppData` | Absolute `XDG_DATA_HOME`, else `HOME/.local/share` | `HOME/Library/Application Support` |
| `program_data` | `FOLDERID_ProgramData` | `/var/lib` | `/Library/Application Support` |
| `temp` | `GetTempPathW` | Absolute `TMPDIR`, else `/tmp` | Absolute `TMPDIR`, else Darwin user temp, then `/tmp` |
| `executable` | `GetModuleFileNameW` | `/proc/self/exe` | `_NSGetExecutablePath` + `realpath` |
| `module` | Directory of this DLL (or EXE when statically linked) | Directory of absolute `dladdr` module path | Same as Linux |

> [!NOTE]
> - `program_data` represents machine-wide shared application data; append your own application subdirectory name and create it explicitly when needed.
> - Per the [XDG Base Directory Specification](https://specifications.freedesktop.org/basedir/latest/), empty or relative `XDG_*` variables on Linux are ignored in favor of the default fallback (invalid UTF-8 bytes still report an error).
> - Windows queries call `SHGetKnownFolderPath` (`KF_FLAG_DONT_VERIFY`) and `CoTaskMemFree` without requiring `CoInitialize`. On POSIX, if the dynamic loader reports only a relative path for `module`, the query returns `HX_OS_NOT_SUPPORTED` rather than guessing against a potentially changed working directory.

## Thread options, names and semaphores

`HX_OS_ENABLE_THREAD_EX` defaults to `ON` and adds stack-size configuration, UTF-8 thread naming, and bounded counting semaphores:

- **Custom stack reservation (`hx_os_thread_create_with_options`)**: Initialize `hx_os_thread_options` with `HX_OS_THREAD_OPTIONS_INIT` and set `stack_size` in bytes (`0` selects the OS default; see `examples/thread_options.c`). POSIX validates minimum stack size and alignment requirements; Windows uses `_beginthreadex` with `STACK_SIZE_PARAM_IS_A_RESERVATION` (values exceeding its `unsigned` parameter return `HX_OS_RANGE`). When choosing a small stack on embedded targets, leave headroom for callbacks, TLS, and sanitizers.
- **Thread names**: Names must be valid UTF-8. Linux and macOS truncate on a complete Unicode code-point boundary within `15` and `63` bytes respectively (macOS only allows naming the calling thread itself). Windows resolves `SetThreadDescription` dynamically from `KernelBase.dll` (returning `HX_OS_NOT_SUPPORTED` on Windows 7). Naming an already joined thread returns `HX_OS_CLOSED`.
- **Bounded semaphores (`hx_os_semaphore` / `hx::Semaphore`)**: Implemented with a mutex and condition variable. `post(count > 0)` retains tokens when no waiter is present and atomically rejects overflow above the maximum count with `HX_OS_LIMIT`. `wait_until` consumes an available token immediately (even with `HX_OS_DEADLINE_NOW`, which returns `HX_OS_TIMEOUT` if no token is available) or waits until a monotonic deadline (including `HX_OS_DEADLINE_INFINITE`).
- **C++11 `hx::Thread` ownership**: Uses the same explicit caller-owned pointer model as `hx::ThreadPool`—`closeUntil` joins and destroys the thread handle on success, retains the owner pointer on timeout for later retry, and never blocks or forcibly terminates a thread in its destructor.

## Pipes and local I/O loops

Use anonymous synchronous pipes or loop-attached asynchronous pipes for local byte-stream communication (see [pipe_transfer.c](../examples/pipe_transfer.c)):

- **Synchronous anonymous pipes (`hx_os_pipe_pair_anonymous` / `hx::Pipes::anonymous`)**: Returns two blocking `hx_os_file` endpoints that work with standard file `read`, `write`, and `close` calls. Once all write ends close, buffered bytes are drained before `read` returns `HX_OS_EOF`; writing after the read end closes returns `HX_OS_CLOSED`. Linux uses `pipe2` with `O_CLOEXEC`; macOS sets `FD_CLOEXEC` via `fcntl` (serialize concurrent `fork`/`exec` during pipe creation on macOS).
- **Asynchronous pipes and parent/child hybrid pipes**:
  - `hx_os_pipe_pair_async` creates two asynchronous endpoints for use with `hx_os_pipe_read_async` and `hx_os_pipe_write_async` (writes copy the buffer by default and transfer all bytes; extended submission can borrow the caller's buffer).
  - `hx_os_pipe_pair_child` creates an asynchronous parent endpoint paired with a synchronous child `hx_os_file` endpoint in either direction (created non-inheritable and selectively duplicated into a child process during `hx_os_process_spawn`).
- **Local event loop without networking (`hx_os_loop_create_local`)**: Drives posted callbacks, periodic timers, and local asynchronous pipe I/O without requiring `HX_OS_ENABLE_NETWORK` (enabling both `HX_OS_ENABLE_PIPE` and `HX_OS_ENABLE_LOOP` advertises `HX_OS_CAP_ASYNC_PIPE`).
  - Each pipe endpoint accepts one active request at a time on the loop creator thread, sharing the loop's `max_requests` and `max_pending_bytes` budgets.
  - `close_until` stops new submissions, cancels in-flight work, and pumps native retirement; on `HX_OS_TIMEOUT`, the pointer and borrowed buffers remain retained for retry (waiting for unfinished work inside a loop callback or re-entering an active close returns `HX_OS_DEADLOCK`). Close all attached pipes before destroying their loop.
  - **Thread-local `SIGPIPE` suppression on POSIX**: Pipe writes temporarily block `SIGPIPE` on the calling thread, consume any signal generated by a broken write, and restore the original signal mask without installing a process-wide signal handler.

## Socket options and positioned file I/O

For TCP keepalive timing, IPv6 dual-stack listeners, broadcast, multicast group membership, and network interface enumeration, see [socket options and discovery](NETWORK_OPTIONS.md).

- **Common socket options (`HX_OS_ENABLE_NET_OPTIONS`)**: Call `hx_os_socket_set_nodelay`, `hx_os_socket_set_keepalive`, or `hx_os_socket_set_reuseaddr` directly (`SO_REUSEADDR` remains opt-in). Once a socket is attached to an `hx_os_loop`, option setters return `HX_OS_BUSY` even between requests. The borrowed C++11 `hx::SocketOptions` facade provides these setters plus `peerAddress()`. Raw `option_set` / `option_get` functions pass platform-native `level` and `name` constants directly to the OS (C++ `get<T>` requires the returned native byte length to match `sizeof(T)` or returns `HX_OS_RANGE`; on Windows, `TCP_NODELAY` may return a 1-byte result inside an `int` buffer, so C callers should inspect the returned length).
- **Positioned file I/O (`HX_OS_ENABLE_POSITIONED_IO`)**: Independent of `FILE_UTILS`. `hx_os_file_read_at` and `hx_os_file_write_at` (`File::readAt` / `writeAt` in C++11; see [positioned_read.c](../examples/positioned_read.c)) transfer up to `INT_MAX` bytes at an absolute 64-bit byte offset without modifying the file's sequential cursor. Append-mode files reject positioned writes with `HX_OS_NOT_SUPPORTED`, and mismatched open modes return `HX_OS_ACCESS_DENIED`.
  - **Platform implementation**: POSIX calls `pread` / `pwrite` (retrying `EINTR`). On Windows, the first positioned call lazily reopens the existing file via `ReOpenFile` with `FILE_FLAG_OVERLAPPED`, publishes that cached handle atomically, and runs each call with its own event and `OVERLAPPED` structure—preserving the sequential cursor and allowing concurrent positioned operations (closing the `hx_os_file` releases both handles).

## File information, directory kinds and BOM

- **Extended file metadata and directory entry types (`HX_OS_ENABLE_FILE_INFO`, default `ON`)**:
  - `hx_os_path_stat_extended` (initialize with `HX_OS_FILE_INFO_EXTENDED_INIT`) and `hx_os_directory_next_info` (`hx::Files::info` and `directory.next(true)` in C++11; see `examples/file_info.c`) report nanosecond timestamps, permission summaries, and non-following directory entry kinds.
  - **Timestamps and permissions**: Timestamps carry signed Unix seconds and nanoseconds in `[0, 10^9)`. Metadata change time reports POSIX `ctime` or Windows `FILE_BASIC_INFO.ChangeTime` (not creation time). `read_only` reflects the Windows attribute or the absence of the POSIX owner-write bit; POSIX reports known owner-read/write bits, while Windows marks them unknown rather than guessing NTFS ACLs.
  - **Directory entry classification**: Classifies the entry itself without following symlinks (Windows reparse points report `LINK`; POSIX uses `d_type` when provided by the filesystem and falls back to descriptor-relative `fstatat`).
- **Byte-order mark detection and generation (BOM utility, default `ON`)**:
  - `hx_os_text_bom_detect` (`hx::Text::detectBom` in C++11) matches the longest complete `UTF-32LE/BE`, `UTF-8`, or `UTF-16LE/BE` BOM prefix, returning `HX_OS_NOT_FOUND` with `prefix_bytes = 0` when none is present (note: a 2-byte `FF FE` input is a complete `UTF-16LE` BOM; supply at least 4 bytes when probing a stream that could be `UTF-32LE`). `hx_os_text_bom_bytes` emits up to 4 raw BOM bytes.

## Configuration serialization and numeric text

- **Configuration serialization and atomic save (`HX_OS_ENABLE_CONFIG_SERIALIZE`)**: Defaults to `ON` and requires `HX_OS_ENABLE_CONFIG`. `hx_os_config_serialize` formats an immutable config document into an `hx_string`, while `hx_os_config_save` (also requiring `HX_OS_ENABLE_FILE_UTILS`) serializes and atomically publishes the file in one call across all seven supported formats (JSON, INI, `.properties`, YAML, XML, TOML, and `.env`; see [serialization and object mapping](SERIALIZATION.md) and `examples/config_save.c`).
  - JSON output preserves exact numeric spellings without rounding large integers through `double`. Values or structures that cannot be represented losslessly in line-oriented formats such as INI or `.properties` fail explicitly with `HX_OS_UNREPRESENTABLE`.
  - Serialization runs a bounded measurement pass followed by a single output allocation and write pass under `max_work`, `max_nodes`, `max_depth` (up to 128 frames), `max_input_bytes`, and `max_memory_bytes`. In C++11, `hx::ConfigDocument::stringView(pointer)` provides zero-copy string views alongside `string()` and `stringOr()`.
- **Locale-independent numeric text (`HX_OS_ENABLE_TEXT_NUMBER`)**: Exposes `parse_double` and `format_double` (17 significant digits for IEEE 754 `binary64`, canonical `nan`/`inf`, and C numeric locale rules without mutating the process locale; overflow/underflow returns `HX_OS_RANGE`).

## Periodic timers

`HX_OS_ENABLE_TIMER` defaults to `ON` and depends on `HX_OS_ENABLE_LOOP` (independently of `HX_OS_ENABLE_NETWORK`). `hx_os_timer_create(loop, interval_ns, callback, user, &timer, &error)` (`hx::PeriodicTimer::every` in C++11; see `examples/periodic_timer.c`) schedules a repeating timer on the loop thread:

- **Fixed-axis scheduling and coalesced ticks**: The first tick fires one `interval_ns` after creation, and subsequent ticks stay anchored to the original monotonic timeline. If a slow callback or delayed poll misses multiple periods, missed intervals coalesce into a **single tick per poll**.
- **Stopping and closing**: Callbacks run outside locks on the polling thread (`HX_OS_OK` continues; terminal errors stop the timer). `hx_os_timer_stop` is thread-safe, while `close_until` must be called on the loop creator thread to pump cancellation and wait for final callback/cleanup completion (`HX_OS_TIMEOUT` retains the timer for retry; closing a timer from inside its own callback returns `HX_OS_DEADLOCK`). Close all attached timers before calling `hx_os_loop_destroy`, or loop destruction will return `HX_OS_BUSY`.

## Pool worker identity

Call `hx_os_thread_pool_inside(pool, &inside)` (`pool.inside()` in C++11) to check whether the calling thread is currently executing as a worker of `pool` (including inside a task cleanup callback):

```c
int inside;

hx_os_status status = hx_os_thread_pool_inside(pool, &inside);
if (status == HX_OS_OK && !inside)
	status = hx_os_task_wait_until(task, deadline, &work_status, &error);
// Inside a worker, continue asynchronously instead of waiting for new work.
```

This check reads the pool's thread-local identity without locks, allocations, or syscalls. Because a worker thread waiting on an unfinished task in its own pool (or trying to close its own pool) immediately receives `HX_OS_DEADLOCK`, checking `inside` lets shared helper functions avoid self-deadlock.

## Native error text

`hx_os_error_message` formats an `hx_os_error` into a strict UTF-8 diagnostic string, including address-resolution errors (`GAI` domain via `gai_strerror` on POSIX and thread-safe `FormatMessageW` conversion on Windows without requiring `WSAStartup`—avoiding the non-thread-safe storage noted in [Microsoft's `gai_strerrorW` documentation](https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-gai_strerrorw)) and Apple `Mach` errors (`mach_error_string`).

The reported `required` capacity includes the terminating `NUL`. Because native wording varies by OS and locale, use formatted text for logging and diagnostics only—never branch on message strings in control flow.

## Randomness, UUID, CRC32 and secure clearing

- **Cryptographic randomness (`HX_OS_ENABLE_RANDOM`, default `ON`)**:
  - `hx_os_random_bytes` fills a buffer from the OS cryptographic random source and **never falls back to a time-seeded or non-cryptographic PRNG**. On native failure, the entire output buffer is zeroed.
  - **OS backends**: Windows loads `BCryptGenRandom` from the system directory (falling back to `CryptGenRandom` only if unavailable); Linux calls the `getrandom` syscall (falling back on `ENOSYS` to reading 32 seed bytes from `/dev/random` before `/dev/urandom`, per [Linux `random(4)` semantics](https://www.man7.org/linux/man-pages/man4/random.4.html)); Apple uses `arc4random_buf`.
- **UUID v4**: `hx_os_uuid_generate_string(&text, &error)` (`hx::Random::uuid()` in C++11; see `examples/random_uuid.c`) generates a random v4 UUID and formats it as lowercase `8-4-4-4-12` text.
- **CRC32 and secure memory zeroing (always available)**:
  - `hx_os_crc32` computes reflected `CRC-32/ISO-HDLC` (polynomial `0xedb88320`, initial seed `0`) using a 64-byte read-only lookup table with zero heap allocations or locks (for accidental corruption detection, not cryptographic integrity).
  - `hx_os_memory_secure_zero` wipes sensitive buffers using `SecureZeroMemory` (Windows), `explicit_bzero` (Linux), or `volatile` byte writes so the compiler cannot optimize the wipe away.

## Pages and file mappings

`HX_OS_ENABLE_VIRTUAL_MEMORY` defaults to `ON` (independent of `SYSINFO` and `FILE_UTILS`; see [mapped_file.c](../examples/mapped_file.c)):

- **Page-aligned anonymous memory (`hx_os_pages_alloc` / `hx::Pages`)**: Rounds a positive byte size up to the OS page size and allocates zero-initialized anonymous pages. Supports `NONE`, `READ`, `READ|WRITE`, `READ|EXECUTE`, and `READ|WRITE|EXECUTE` protections (enabling `EXECUTE` synchronizes the instruction cache, and OS policy rejections are reported directly without silently weakening permissions).
- **File mappings (`hx_os_file_map_open` / `hx_os_file_map_create` / `hx::MappedFile`)**:
  - Maps an entire regular file or a byte subrange (aligning offsets to OS granularity internally while returning a view starting at the exact requested offset without moving the file cursor). Each mapping duplicates its own native file resource, so the caller's original `hx_os_file` can be closed right after `create`.
  - **Modes and flushing**: `READ` provides a `const` byte view; `SHARED_WRITE` requires a read/write file (rejecting `APPEND`) and supports explicit `flush` via `msync(MS_SYNC)` / `FlushViewOfFile` plus optional `durable = 1` `fsync` / `FlushFileBuffers` (see [Windows flush semantics](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-flushviewoffile) and [POSIX `mmap(2)` behavior](https://www.man7.org/linux/man-pages/man2/mmap.2.html)); `PRIVATE_COPY` creates copy-on-write pages that never write back to the file. Destroying a mapping does not implicitly flush it, and concurrent file truncation or storage faults on a mapped file can raise `SIGBUS` or a native OS exception.

## File range locks, timestamps and permission summaries

`HX_OS_ENABLE_FILE_EXTRA` defaults to `ON` (independent of `FILE_INFO` and `FILE_UTILS`; see `examples/file_attributes.c`):

- **Byte-range locks (`hx_os_file_lock_at` / `hx::FileRangeLock`)**: Acquires a shared (requires `READ`) or exclusive (requires `WRITE`) lock on a positive 64-bit byte range without moving the file cursor, using either `HX_OS_DEADLINE_NOW` or a monotonic deadline.
  - **OS locking semantics**: POSIX `fcntl` record locks are **process-owned advisory locks**—closing *any* file descriptor for that inode in the process (including a duplicate descriptor owned by a file mapping) releases the process's locks! Windows `LockFileEx` locks are **handle-owned mandatory locks** for ordinary I/O (because Windows positioned `read_at`/`write_at` uses a separate internal handle, perform cursor-based reads/writes through the locking handle while holding a Windows range lock). See [POSIX `fcntl` locking](https://www.man7.org/linux/man-pages/man2/fcntl_locking.2.html) and [Windows `LockFileEx`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex).
- **Timestamps and owner permissions (`hx::FileAttributes`)**: Initialize `hx_os_file_times` with `HX_OS_FILE_TIMES_INIT` and select access/modify timestamps via `fields` (POSIX uses `utimensat` with `UTIME_OMIT`; Windows uses `SetFileTime` at 100 ns ticks). Permission updates replace POSIX owner read/write/execute bits while preserving group/other bits; on Windows, `READ` is required, `EXECUTE` is unsupported, and `WRITE` toggles the read-only attribute.

## Directory monitoring and external descriptor readiness

- **Directory change monitoring (`HX_OS_ENABLE_WATCH`, default `ON`)**:
  - `hx_os_watch_create(context, path, recursive, callback, user, &watch, &error)` (`hx::DirectoryWatch` in C++11; see [watch_directory.c](../examples/watch_directory.c)) spawns one joinable worker thread and waits until the OS watch subscription is armed.
  - **Events and overflow reconciliation**: Callbacks run on the worker thread outside locks and receive borrowed slash-separated relative UTF-8 paths (`""` denotes the root). `CHANGED`, `ADDED`, `REMOVED`, and `RENAMED` are best-effort notifications; `ERROR` terminates the watch; `OVERFLOW` indicates that the caller should rescan a fresh directory snapshot.
  - **OS backends and cleanup**: Defaults budget a `64 KiB` native buffer, `32 KiB` max path, `1,024` software watch records, depth `64`, and `4 MiB` variable storage. Windows uses overlapped `ReadDirectoryChangesW`; Linux uses `inotify` + `eventfd` with `O_NOFOLLOW` and `/proc/self/fd`; Apple uses pure-C `FSEvents` / `CoreFoundation` (see the [Apple scheduling contract](https://developer.apple.com/documentation/coreservices/1447824-fseventstreamschedulewithrunloop)). `hx_os_watch_close_until` stops and joins the worker (if creation fails part-way and rollback cannot immediately join the worker, `create` returns the error alongside a non-`NULL` stopping `watch` handle—retry `hx_os_watch_close_until` on that owner).
- **POSIX external descriptor readiness (`HX_OS_ENABLE_LOOP_FD`)**: Attaches a `CLOEXEC` duplicate of a pollable POSIX file descriptor to an `hx_os_loop` without mutating `O_NONBLOCK` (returns `HX_OS_NOT_SUPPORTED` on Windows):

```c
hx_os_loop_watch *watch = NULL;

hx_os_status status = hx_os_loop_attach_fd(loop, fd, HX_OS_WAIT_READ,
	ready_callback, user, &watch, &error);
// The callback can use hx_os_loop_watch_descriptor(watch), then request stop.
// Drive loop; finally check hx_os_loop_detach_fd(&watch, &error).
```

<a id="process-control-capture-and-system-helpers-batch-18"></a>

## Process control, capture and system helpers

`HX_OS_ENABLE_PROCESS` and `HX_OS_ENABLE_SYSTEM_UTILS` default to `ON`:

- **Child process spawning (`hx_os_process_spawn` / `hx::Process`)**:
  - Accepts a literal UTF-8 executable path, an argument array excluding `argv[0]` (supplied automatically), an optional environment array (`NULL` preserves the native snapshot, including entry order and duplicate names; `{NULL}` passes an empty environment), and optional `stdin`/`stdout`/`stderr` redirections. **Never invokes a shell or searches `PATH`.** Arguments and environment are each bounded to `4 MiB` and `65,536` entries.
  - **Pre-exec failures and exit code `127`**: As [POSIX `posix_spawn`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawn.html) specifies, some pre-exec errors (such as a missing binary on Android Bionic) report as child exit code `127` during `wait_until`, so always inspect both spawn and wait results.
  - **Waiting and closing**: `wait_until` polls completion against a monotonic deadline (nonzero exit codes are successful waits; POSIX signal termination is reported distinctly, and Windows checks process completion before reading the exit code so exit code `259` is never confused with `STILL_ACTIVE`). `destroy` returns `HX_OS_BUSY` while the child is still running; `close_until` waits and then destroys. The library never kills or detaches child processes or alters `SIGCHLD`.
- **Concurrent output capture (`hx_os_process_capture_run` / `hx::ProcessCapture`)**:
  - Spawns a child (with `stdin` at `EOF`) and collects `stdout` and `stderr` concurrently on a private owner-thread loop without spawning a worker thread (defaulting to `1 MiB` retained raw bytes per stream; see [capture_process.c](../examples/capture_process.c)):

```c
hx_os_process_capture *capture = NULL;
const char *arguments[] = {"--version", NULL};

hx_os_status status = hx_os_process_capture_run(NULL, executable, arguments,
	NULL, hx_os_deadline_after_ms(2000), &capture, &error);
if (status == HX_OS_OK) {
	hx_string output = HX_STRING_INIT;

	status = hx_os_process_capture_text(capture, HX_OS_PROCESS_STDOUT,
		&output, &error);

	// Use output, then destroy it.
	hx_string_destroy(&output);
}
// Close even after failure; keep a non-NULL capture for another close attempt.
hx_os_status closed = hx_os_process_capture_close_until(
	&capture, hx_os_deadline_after_ms(2000), NULL);
if (closed != HX_OS_OK && capture)
	fprintf(stderr, "Close pending: %s\n", hx_os_status_name(closed));
// Keep capture in application state. Handle limits or retry cleanup later.
```

Once a child starts, even `HX_OS_TIMEOUT`, `HX_OS_LIMIT`, or an I/O error returns an owned `capture` session so you can inspect partial output, call `raise_limit` or `discard`, and retry `close_until` (if startup fails and rollback also fails, a cleanup-only non-`NULL` session is returned on which `wait`/`view`/`text` return `HX_OS_CLOSED` until `close_until` finishes cleanup).
- **System helpers (`HX_OS_ENABLE_SYSTEM_UTILS`)**: Provides `hx_os_process_arguments` (native UTF-8 command-line snapshot including `argv[0]`), hostname, native PID, bounded environment enumeration, usable CPU SIMD feature flags (`SSE`/`SSE2`/`SSE4.1`/`AVX`/`AVX2`/`NEON`), `WOW64` / `Rosetta` translation status, byte-swap and `udiv64` helpers, and explicit POSIX `timespec` deadline conversions.

<a id="scheduling-requests-and-delayed-tasks-batch-19"></a>

## Scheduling requests and delayed tasks

- **Thread priority and CPU affinity (`HX_OS_ENABLE_THREAD_EX`)**:
  - Exposes relative priority requests (`idle`, `normal`, `time-critical`) and CPU affinity masks (`0` is `HX_OS_INVALID`; Linux reads back the applied mask and returns `HX_OS_NOT_SUPPORTED` while attempting to restore the prior mask if `cpuset` restrictions narrowed it; macOS returns `HX_OS_NOT_SUPPORTED` for strict binding). See [Linux affinity](https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html) and [Windows affinity](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setthreadaffinitymask).
- **Delayed thread-pool tasks (`HX_OS_ENABLE_DELAYED_TASKS`, default `ON`)**:
  - Requires `HX_OS_ENABLE_THREAD_POOL` (independent of `LOOP` and `TIMER`; see [delayed_task.c](../examples/delayed_task.c)). `hx_os_thread_pool_submit_delayed` (`pool.submitAfter` / `pool.submitAt` in C++11) schedules a task to become eligible at a monotonic due time:

```c
hx_os_task *task = NULL;

hx_os_status status = hx_os_thread_pool_submit_delayed(pool,
	hx_os_deadline_after_ms(100), work, user, cleanup, &task, &error);
// On acceptance, wait/cancel/release through the ordinary task API.
```

```cpp
#include <hx/ThreadPool.hpp>

hx::Result<hx::Task<int>> delayed_answer(hx::ThreadPool &pool) noexcept
{
	return pool.submitAfter(std::chrono::milliseconds(100),
		[](int value) noexcept { return value * 2; }, 21);
}
```

Future tasks share the pool's queue capacity and worker condition variable without spawning a separate timer thread. Cancelling a queued delayed task promotes it immediately for cleanup without waiting for its due time.

<a id="local-sockets-and-duplex-file-endpoints-batch-20"></a>

## Local sockets and duplex file endpoints

- **Local pathname stream sockets (`HX_OS_ENABLE_LOCAL_SOCKETS`, default `ON`)**:
  - Requires `HX_OS_ENABLE_NETWORK`. Uses `HX_OS_LOCAL` / `HX_OS_LOCAL_STREAM` (`AF_UNIX` stream sockets with strict UTF-8 pathnames up to `108` bytes including `NUL`, supported on Linux, macOS, and Windows 10+; see [Microsoft's `AF_UNIX` overview](https://devblogs.microsoft.com/commandline/af_unix-comes-to-windows/)).
  - `hx_os_socket_local_listen` creates, binds, and listens without unlinking existing files (the caller owns the directory and removes its socket pathname after closing). `hx_os_socket_local_connect` and `finish_connect` handle deadline-bounded connects. Connected local streams support `send`, `receive`, `hx_os_socket_receive_until`, and loop `READ`/`WRITE` operations (use direct `accept_until` and `connect_until`/`finish_connect` rather than loop `ACCEPT`/`CONNECT`).
- **Standalone full-duplex file pairs (`HX_OS_ENABLE_DUPLEX_PIPE`, default `ON`)**:
  - Independent of `NETWORK`, `PIPE`, and `LOOP`. `hx_os_pipe_pair_socket` (`hx::DuplexPipe::pair` in C++11; see [duplex_message.c](../examples/duplex_message.c)) returns two readable and writable `hx_os_file` endpoints (using `socketpair(AF_UNIX, SOCK_STREAM)` on POSIX and a verified private IPv4 TCP loopback pair with self-managed Winsock references on Windows):

```c
hx_os_file *first = NULL, *second = NULL;

hx_os_status status = hx_os_pipe_pair_socket(NULL, &first, &second, &error);
// Use file read/write; shutdown preserves the opposite direction.
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

<a id="registry-power-requests-and-obfuscation-batch-21"></a>

## Registry, power requests and obfuscation

- **Windows Registry (`HX_OS_ENABLE_REGISTRY`)**: `hx_os_registry_open_user` opens a backslash-separated `HKCU` subkey, and `hx_os_registry_read_string` reads literal `REG_SZ` / `REG_EXPAND_SZ` values up to `1 MiB` into an `hx_string` without expanding environment variables. Creation, write access, and WOW64 views are explicit; non-Windows builds return `HX_OS_NOT_SUPPORTED` (`hx::Registry` in C++11).
- **Sleep inhibition and performance requests (`HX_OS_ENABLE_SLEEP_INHIBIT` / `HX_OS_ENABLE_PERFORMANCE_REQUEST`)**: `hx_os_sleep_inhibit_create(NULL, "Exporting project", &request, &error)` (`hx::SleepInhibit` in C++11) creates an isolated, handle-scoped sleep-inhibition request:
  - Windows uses `PowerCreateRequest` (`PowerRequestSystemRequired` on Windows 7+, and `PowerRequestExecutionRequired` for performance requests on Windows 8+); Apple uses pure-C `IOKit` assertions for sleep inhibition and `NSProcessInfo` for performance requests (disable `HX_OS_ENABLE_PERFORMANCE_REQUEST` to omit Foundation/Objective-C); Linux desktop sleep inhibition uses an optional scoped `sd-bus` adapter (see [Linux desktop](LINUX_DESKTOP.md)).
- **Reversible string obfuscation (`HX_OS_ENABLE_OBFUSCATION`)**: Generate C99 byte arrays with `python tools/obfuscate_string.py --name app_label --text "example"` and decode at runtime with `hx_os_obfuscate_string(&app_label, &text, &error)` or C++11 `constexpr auto text = hx::obfuscate<0x5a>("example")` (see `examples/obfuscated_string.c`). **Obfuscation is reversible XOR, not encryption—never use it to protect secrets.**

<a id="calendar-translation-table-and-bit-streams-batch-22"></a>

## Calendar, translation table and bit streams

- **UTC calendar formatting and local offset (`HX_OS_ENABLE_CALENDAR`)**: `hx_os_wall_time_format_utc_string(milliseconds, NULL, &text, &error)` (`hx::Calendar::utc` in C++11) formats Unix milliseconds (floored to whole seconds) across Gregorian years `0001..9999` using `%CCYY`, `%MM`, `%DD`, `%hh`, `%mm`, `%ss`, and `%%` (`NULL` selects `%CCYY-%MM-%DDT%hh:%mm:%ssZ`). `hx_os_wall_time_local_offset_minutes` reports the current local timezone offset east of UTC and DST flag.
- **In-memory translation catalog (`HX_OS_ENABLE_TEXT_LOOKUP`)**: `hx_os_text_lookup_create` (`hx::TextLookup` in C++11; see `examples/localized_time.c`) stores up to `4,096` sorted UTF-8 key-value pairs (`1 MiB` default text budget) for lock-free concurrent binary-search lookup, falling back to the original key string only when a key is absent.
- **MSB-first bit streams (`HX_OS_ENABLE_BITSTREAM`)**: Stack-allocated `hx_os_bit_reader` and `hx_os_bit_writer` (`hx::BitReader` / `hx::BitWriter` in C++11) borrow a byte buffer (initialized in **bytes**) and read or write `0..64` **bits** MSB-first without heap allocations or syscalls. Reading past the end returns `HX_OS_EOF`, writing past capacity returns `HX_OS_OUTPUT_FULL`, and values exceeding the bit width return `HX_OS_RANGE` while leaving the bit cursor unchanged.

<a id="synchronous-signals-and-stack-parameters-batch-23a"></a>

## Synchronous signals and stack parameters

`HX_OS_ENABLE_SIGNALS` defaults to `ON` and provides a synchronous observer dispatcher (`hx_os_signal_handler` / `hx::SignalHandler`) paired with a fixed-layout stack parameter pack (`hx_os_calldata` / `hx::CallData`; see `examples/signal_notifications.c`):

- **Stack parameter pack (`hx_os_calldata`)**: Holds up to `16` named entries and `512` bytes of shared UTF-8 name/string storage on the stack with zero heap allocations (internal string offsets make copying the struct by value safe). Use typed getters/setters or chained `hx_os_calldata_edit_*` / `CallData::edit()` helpers, plus allocation-free `CallDataView::stringView`.
- **Signal connection and emission**: `hx_os_signal_emit` invokes matching callbacks synchronously on the emitting thread outside internal locks in registration order (returning the first non-`HX_OS_OK` status after running all eligible callbacks). Connection IDs include a generation counter; `disconnect_until` waits for running callbacks and cleanup to finish (self-waiting returns `HX_OS_DEADLOCK`), and `close_until` retires all subscriptions before plugin or library unload.

<a id="mutable-settings-and-retained-snapshots-batch-23b"></a>

## Mutable settings and retained snapshots

`HX_OS_ENABLE_SETTINGS` defaults to `ON` and provides an independent, thread-safe hierarchical settings store (`hx_settings` / `hx::Settings`) with reference-counted immutable snapshots (`hx_setting_value` / `hx::SettingValue`; see `examples/settings.c`):

- **Explicit vs. default layers**: Set explicit values with `set_*` and defaults with `set_default_*`. Queries fall back to the default layer **only when the literal key is absent from the explicit layer** (dots `.` are ordinary key characters, not path separators).
- **Copy-on-write roots and zero-copy string leases**: Supports `null`, `bool`, `int64`, finite `double`, UTF-8 string, array, object, and borrowed user pointer values. Readers briefly lock to retain immutable roots without heap allocation; writers prepare updated nodes outside the lock and retry up to 8 times before returning `HX_OS_BUSY`. Retained snapshots and zero-copy string references (`hx_settings_get_string_ref`, `SettingValue::asStringView`, `Settings::stringView`; see [common-call ownership](COMMON_UTILITIES.md)) remain valid even after the mutable settings object is updated or closed.
- **Observers and config serialization**: Optional synchronous observers run outside locks after each commit. When `HX_OS_ENABLE_CONFIG` and `HX_OS_ENABLE_CONFIG_SERIALIZE` are enabled, `to_config`, `to_json`, `load_json`, and `save_json` bridge settings with immutable config documents.

<a id="bounded-interval-profiler-batch-23c"></a>

## Bounded interval profiler

For owned structured snapshots, means, standard deviations, and opt-in bounded `P50`/`P95`/`P99` percentiles, see the dedicated [profiler tutorial](PROFILER.md). The core interval tree API (`hx_os_profiler_create`, `start_root`, `reach`, `leave`, and `dump_string`, plus C++11 `hx::Profiler` and `hx::ProfileScope`; see `examples/profile_intervals.c`) records monotonic inclusive and exclusive nanosecond durations across threads without background workers or stack unwinding:

- **Balanced scopes and warmup**: Every successful `start_root` or `reach` must be balanced by a matching `leave` on the same thread or fiber. Defaults cap records at `1,024`, participating thread/fiber caches at `64`, and nesting depth at `32`. Once a path and thread cache are warmed up, entering and leaving scopes performs zero `hx_context` heap allocations.
- **Safe shutdown**: `stop` rejects new scopes while allowing active scopes to `leave` and snapshots/dumps to run. `close` waits for active calls and foreign thread caches to detach, returning `HX_OS_DEADLOCK` if the calling thread still has an open scope. Long-lived host threads must call `restart_thread` or exit before unloading a plugin.
