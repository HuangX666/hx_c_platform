# Scoped Linux desktop sleep inhibition

[简体中文](LINUX_DESKTOP.zh-CN.md) · [README](../README.md)

## 1. The same ordinary C entry

A long file export may ask the desktop to keep the computer awake while that work runs. Create a request at the beginning and destroy it afterward:

```c
hx_os_sleep_inhibit *request = NULL;
hx_os_status status = hx_os_sleep_inhibit_create(
	NULL, "Finishing a file export", &request, NULL);
if (status != HX_OS_OK)
	return status;

// Perform the application's work while request remains alive.
return hx_os_sleep_inhibit_destroy(&request, NULL);
```

This fragment belongs inside a function returning hx_os_status. The complete [sleep_inhibit.c](../examples/sleep_inhibit.c) builds as `hx_os_sleep_inhibit`; its 20 ms interval demonstrates where application work belongs. Run `build/bin/hx_os_sleep_inhibit` on a Linux desktop. A supported service grants the request and the program reports its release. Without an available backend/service it explicitly prints `not supported`. The example does not change a power plan or desktop setting.

Each object owns its own request, so one library consumer can release its request while another remains active. The reason is copied as UTF-8 and can be 1–4,096 bytes. Caller serialization permits transfer to another thread; concurrent operations on the same object are excluded. No worker or reconnect thread is started.

```mermaid
flowchart LR
    A[Application creates a request] --> B[Private session bus connection]
    B --> C[Desktop grants a scoped inhibitor]
    C --> D[Application performs its work]
    D --> E[Close this connection]
    E --> F[Desktop releases this request]
```

## 2. Actual platform behavior

The Linux adapter tries existing services in this order: org.freedesktop.PowerManagement, its org.freedesktop.PowerManagement.Inhibit alias, then org.gnome.SessionManager. The first two use the PowerManagement Inhibit interface. The GNOME request uses flag **4 for suspend**, not flag 8 for idle; it does not request inhibition of automatic screen locking. User actions and desktop policy can override requests. GNOME may expose the request when a user asks to suspend. [GNOME method contract](https://gnome.pages.gitlab.gnome.org/gnome-session/re04.html), [KDE interface](https://github.com/KDE/powerdevil/blob/master/daemon/dbus/org.freedesktop.PowerManagement.Inhibit.xml).

Requests do not autostart a missing service or ask for interactive authorization. An explicit DBUS_SESSION_BUS_ADDRESS must contain only local `unix:` addresses, at most 16 KiB. When it is absent the native user-bus default is used. Do not change process environment concurrently with creation/reactivation. No remote bus, unixexec helper or automatic daemon launch is requested by this adapter.

Method calls share a one-second monotonic budget. Native connection setup and scheduling have no hard deadline. Timeout/error releases messages and the private connection; an accepted object is returned only after a valid uint32 cookie reply. Native errno-style failures are retained. Missing runtime/service returns NOT_SUPPORTED; permission, timeout and malformed replies are not turned into success.

Deactivation or destruction closes the private connection without waiting for an Uninhibit reply. The desktop removes inhibitors associated with the disconnected owner. The library reports local resource release; the desktop observes disconnection asynchronously. Other consumers' connections are unaffected. [Native connection ownership](https://github.com/systemd/systemd/blob/main/man/sd_bus_default.xml).

## 3. Requested state and restart

set_active(true) is idempotent. set_active(false) releases the connection; a later true creates a new connection and request. active() reports the last requested state, not live desktop health. A desktop/session-bus restart can discard the native lease while this flag remains true. Deactivate/reactivate, or recreate the object, after such a restart. There is no background polling or automatic recovery.

Do not use an inherited active object after fork. Keep hx_c_platform, its context and borrowed host resources loaded/alive until all corresponding requests are destroyed. Destroy consumes the object as in the existing Windows/Apple contract.

## 4. Optional operating-system runtime

`HX_OS_ENABLE_LINUX_DESKTOP` defaults to ON and also requires SLEEP_INHIBIT. It applies to Linux desktop builds, excluding Android. It dynamically uses the operating system's installed libsystemd.so.0 or libelogind.so.0 through the published sd-bus C ABI. No implementation/SDK header is copied into the project, no native runtime is built or statically included, and no Qt/GLib dependency is added. The loader checks every required symbol; absent/older incompatible runtimes report NOT_SUPPORTED. This is an optional system-runtime dependency; bundled third-party source continues to live in deps/.

The native provider mapping is held with RTLD_NODELETE so its own TLS cannot reference unloaded code. Per-object dlopen references, messages, connections and first-party allocations are released; the provider mapping and its internal process state may remain until process exit. First-party allocation injection does not replace the native provider's allocator. No per-process allocator or shared bus configuration is changed.

Turn LINUX_DESKTOP or SLEEP_INHIBIT OFF for an embedded/headless build that does not need this adapter. It requires neither NETWORK, LOOP, LIBRARY nor PATHS. Static CMake/pkg-config consumption carries the required system dl linkage. The capability bit means an adapter was compiled, not that the current desktop has a service. Linux performance requests remain NOT_SUPPORTED.

## 5. C++11 and validation

Include `<hx/Power.hpp>`. In a function returning hx_os_status:

```cpp
auto request = hx::SleepInhibit::create("Finishing a file export");
if (!request)
	return request.status();

// Perform application work while request.value() remains alive.
return request.value().close().status();
```

The existing move-only facade releases synchronously and uses Result, without exceptions. Closing after deactivation is valid; it does not release another object's inhibitor.

Run the public [sleep_inhibit.c](../examples/sleep_inhibit.c) to request temporary sleep inhibition from the current desktop service. Check service availability and the returned status.

Whether a request prevents physical sleep depends on GNOME/KDE, logind/elogind, session permissions and machine policy. Verify the effect in the actual desktop environment; WSL protocol traffic cannot establish physical sleep prevention. See [platform support](PLATFORMS.md) for other boundaries.
