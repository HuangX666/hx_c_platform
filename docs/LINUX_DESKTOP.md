# Scoped Linux desktop sleep inhibition

[简体中文](LINUX_DESKTOP.zh-CN.md) · [README](../README.md)

When a desktop application is running a long file export, firmware flash, or batch job and wants to prevent the desktop environment from automatically suspending the machine due to user inactivity, use the scoped sleep-inhibition API (`hx_os_sleep_inhibit` / `hx::SleepInhibit`).

To see it in action on your desktop, build and run [sleep_inhibit.c](../examples/sleep_inhibit.c). The program calls `hx_os_sleep_inhibit_create` to acquire a scoped sleep-inhibition lease, performs simulated work, and calls `hx_os_sleep_inhibit_destroy` to release the lease. Follow the [example run instructions](../examples/README.md#run) to enable `HX_OS_BUILD_EXAMPLES=ON` and build `hx_os_sleep_inhibit` (no command-line arguments or input files are needed):

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --target hx_os_sleep_inhibit
./build/bin/hx_os_sleep_inhibit
```

On a Linux desktop running a supported session power manager (such as GNOME or KDE), the program prints confirmation messages as it acquires and releases the inhibitor. On headless servers, embedded boards, or WSL environments without a desktop session bus, it explicitly reports `not supported` or the native bus error. You can also drop its source into the [minimal project](USAGE.md#c-project) template.

## 1. The same ordinary C entry

Linux desktop builds use the exact same C99 entry points as Windows and macOS. Create a request when your operation begins and destroy it when the work finishes:

```c
hx_os_sleep_inhibit *request = NULL;
hx_os_status status = hx_os_sleep_inhibit_create(
	NULL, "Finishing a file export", &request, NULL);
if (status != HX_OS_OK)
	return status;

// Perform the application's work while request remains alive.
return hx_os_sleep_inhibit_destroy(&request, NULL);
```

In [sleep_inhibit.c](../examples/sleep_inhibit.c), a 20 ms sleep stands in for the application work:

- **Per-handle isolation**: Each `hx_os_sleep_inhibit` instance owns its own private D-Bus session-bus connection. Multiple subsystems or plugins in the same process can hold independent requests concurrently—destroying one request never cancels another consumer's active inhibitor.
- **Reason string and threading**: The UTF-8 `reason` string (`1–4,096` bytes) is copied and shown by the desktop environment if the user checks what is preventing sleep. You may transfer an instance across threads with external synchronization, provided only one thread operates on a given instance at a time. No background worker or auto-reconnect thread is spawned.

```mermaid
flowchart LR
    A[Application creates a request] --> B[Private session bus connection]
    B --> C[Desktop grants a scoped inhibitor]
    C --> D[Application performs its work]
    D --> E[Close this connection]
    E --> F[Desktop releases this request]
```

## 2. Actual platform behavior

- **Service discovery order and GNOME suspend flag**:
  - The Linux adapter tries active session-bus services in order: `org.freedesktop.PowerManagement`, its `org.freedesktop.PowerManagement.Inhibit` alias, and finally `org.gnome.SessionManager`.
  - The first two use the freedesktop PowerManagement `Inhibit` method; the GNOME request passes **`flags = 4` (inhibit suspend only)** rather than `flags = 8` (idle), so it does not block automatic screen locking (see the [GNOME method contract](https://gnome.pages.gitlab.gnome.org/gnome-session/re04.html) and [KDE interface](https://github.com/KDE/powerdevil/blob/master/daemon/dbus/org.freedesktop.PowerManagement.Inhibit.xml)). Explicit user actions and system power policies can still override an inhibitor.
- **Connection policy and timeout budget**:
  - Requests never autostart a missing desktop daemon or trigger interactive PolicyKit authorization prompts.
  - If `DBUS_SESSION_BUS_ADDRESS` is set, it must contain only local `unix:` socket addresses (up to `16 KiB`); remote TCP buses and `unixexec:` helpers are rejected. When unset, the native user-bus default is used (do not mutate the process environment concurrently while creating or reactivating a request).
  - Method calls share a `1-second` monotonic time budget. On timeout, permission failure, or a malformed reply, messages and the private connection are closed and the error is returned (`HX_OS_NOT_SUPPORTED` when the runtime or desktop service is absent).
- **Release via private connection close**: Deactivating (`set_active(false)`) or destroying (`destroy`) a request closes its private bus connection immediately without blocking on an `Uninhibit` reply. Because desktop session managers scope inhibitors to the bus connection owner, disconnecting automatically releases that inhibitor while leaving other consumers' connections untouched (see [Native connection ownership](https://github.com/systemd/systemd/blob/main/man/sd_bus_default.xml)).

## 3. Requested state and restart

- **Idempotent activation**: Calling `hx_os_sleep_inhibit_set_active(request, 1, &error)` on an already active request is a no-op; calling `set_active(request, 0, &error)` closes the private connection, and a subsequent `set_active(request, 1, &error)` opens a fresh connection and acquires a new lease.
- **Session-bus restarts**: `hx_os_sleep_inhibit_active()` returns the last locally requested state rather than polling the desktop daemon. If the desktop session or D-Bus daemon restarts while your application is running, the remote lease may be lost while `active()` remains true; deactivate and reactivate (or recreate) the request if you detect a session restart.
- **`fork` and unload rules**: Never use an inherited active request in a child process after `fork()`. Keep `hx_c_platform`, its `hx_context`, and host resources alive until all sleep-inhibition requests have been destroyed.

## 4. Optional operating-system runtime

`HX_OS_ENABLE_LINUX_DESKTOP` defaults to `ON` and requires `HX_OS_ENABLE_SLEEP_INHIBIT` (applying to desktop Linux builds, excluding Android):

- **Dynamic `sd-bus` loading**: At runtime, the adapter dynamically loads the OS-installed `libsystemd.so.0` or `libelogind.so.0` via the public `sd-bus` C ABI (using `RTLD_NODELETE` so internal library TLS never points to unmapped code). No `systemd` / `elogind` headers or sources are bundled or statically linked, and no Qt or GLib dependency is added. Missing or incompatible runtimes cleanly return `HX_OS_NOT_SUPPORTED`.
- **Headless / embedded builds**: Set `-DHX_OS_ENABLE_LINUX_DESKTOP=OFF` or `-DHX_OS_ENABLE_SLEEP_INHIBIT=OFF` when targeting headless or embedded Linux systems. The adapter does not depend on `NETWORK`, `LOOP`, `LIBRARY`, or `PATHS` (static CMake and `pkg-config` consumers automatically receive the required `-ldl` linkage). Note that `hx_os_performance_request` remains `HX_OS_NOT_SUPPORTED` on Linux.

## 5. C++11 and validation

Include `<hx/Power.hpp>` in C++11 to use the move-only `hx::SleepInhibit` wrapper returning `hx::Result` without exceptions:

```cpp
auto request = hx::SleepInhibit::create("Finishing a file export");
if (!request)
	return request.status();

// Perform application work while request.value() remains alive.
return request.value().close().status();
```

Calling `close()` after deactivation is valid and never affects another instance's inhibitor. Because physical sleep prevention depends on the active desktop environment (GNOME/KDE), `logind`/`elogind`, session permissions, and hardware policy, always verify physical sleep behavior on your target desktop system. See [platform support](PLATFORMS.md) for general OS boundaries.
