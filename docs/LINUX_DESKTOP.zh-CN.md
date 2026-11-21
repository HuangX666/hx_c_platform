# Linux 桌面作用域防睡眠

[English](LINUX_DESKTOP.md) · [README](../README.zh-CN.md)

当桌面应用正在执行文件导出、固件烧录或长耗时批处理任务，希望在此期间暂时阻止桌面环境因空闲而自动进入系统休眠（Suspend）时，可以使用作用域防睡眠接口（`hx_os_sleep_inhibit` / `hx::SleepInhibit`）。

你可以直接编译并运行完整示例 [sleep_inhibit.c](../examples/sleep_inhibit.c) 来验证当前桌面环境的效果：它调用 `hx_os_sleep_inhibit_create` 申请防睡眠租约，模拟一段业务工作，最后调用 `hx_os_sleep_inhibit_destroy` 释放租约。按[示例运行方法](../examples/README.zh-CN.md#run)启用 `HX_OS_BUILD_EXAMPLES=ON` 并构建 `hx_os_sleep_inhibit`（无需命令行参数或输入文件）：

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --target hx_os_sleep_inhibit
./build/bin/hx_os_sleep_inhibit
```

在运行有 GNOME 或 KDE 等会话电源管理服务的 Linux 桌面上，程序会输出申请成功与释放完成两条提示；如果当前环境没有桌面会话总线或未安装对应的电源管理服务（例如在无桌面的嵌入式板卡、纯字符终端或未启动桌面服务的 WSL 会话中），程序会明确打印 `not supported` 或原生错误原因。你也可以参考[最小工程配置](USAGE.zh-CN.md#c-project)将该示例融入自己的应用。

## 1. 继续使用同一个 C 入口

在 Linux 桌面、Windows 和 macOS 上，防睡眠请求使用完全相同的 C99 接口。在任务开始时创建请求对象，任务结束时销毁它即可：

```c
hx_os_sleep_inhibit *request = NULL;
hx_os_status status = hx_os_sleep_inhibit_create(
	NULL, "Finishing a file export", &request, NULL);
if (status != HX_OS_OK)
	return status;

// Perform the application's work while request remains alive.
return hx_os_sleep_inhibit_destroy(&request, NULL);
```

在完整示例 [sleep_inhibit.c](../examples/sleep_inhibit.c) 中，中间的 20 毫秒延时代表实际业务工作的位置。该接口的设计确保了良好的组件隔离性：

- **按句柄独立持有**：每个 `hx_os_sleep_inhibit` 对象在 Linux 下各自建立一条私有的 D-Bus 会话总线连接（Session Bus Connection）。因此，同一个进程内的不同模块或插件可以各自申请防睡眠请求——当其中一个模块销毁自己的请求时，其他模块持有的防睡眠请求依然保持有效。
- **参数与并发约束**：传入的 `reason` 原因字符串会按 UTF-8 复制保存（允许 `1～4,096` 字节），用于在桌面环境提示用户“哪个应用正在阻止系统休眠”。对象在调用方做好外部同步后可以跨线程移交，但不能由多个线程同时并发操作同一个对象；内部不会启动任何后台工作线程或自动重连线程。

```mermaid
flowchart LR
    A[应用创建请求] --> B[独立的会话总线连接]
    B --> C[桌面授予作用域请求]
    C --> D[应用执行工作]
    D --> E[关闭自己的连接]
    E --> F[桌面释放该请求]
```

## 2. 平台实际行为

- **服务探测顺序与 GNOME 休眠标志**：
  - Linux 桌面适配器按顺序依次尝试连接当前会话总线上已运行的 `org.freedesktop.PowerManagement`、`org.freedesktop.PowerManagement.Inhibit` 别名以及 `org.gnome.SessionManager`。
  - 前两个服务使用标准的 PowerManagement `Inhibit` 接口；对于 GNOME `SessionManager`，适配器固定传入 **`flags = 4`（仅阻止系统挂起休眠 Suspend）**，而**不使用**代表阻止空闲的 `flags = 8`，因此不会阻止用户的屏幕保护程序或自动锁屏（详见 [GNOME SessionManager 方法契约](https://gnome.pages.gitlab.gnome.org/gnome-session/re04.html) 与 [KDE PowerDevil 接口定义](https://github.com/KDE/powerdevil/blob/master/daemon/dbus/org.freedesktop.PowerManagement.Inhibit.xml)）。请注意，操作系统策略或用户强制合盖/手动点击休眠仍可覆盖应用的防睡眠请求。
- **连接安全与超时限制**：
  - 请求**不会**尝试自动拉起未运行的桌面守护进程（No Autostart），也不会请求弹窗交互鉴权。
  - 如果环境变量显式设置了 `DBUS_SESSION_BUS_ADDRESS`，则只允许本机 `unix:` 域套接字地址（总长度不超过 `16 KiB`），拒绝远程 TCP 总线或 `unixexec:` 辅助程序；若未设置该变量，则使用系统默认的用户会话总线地址（在创建或重新激活请求期间，请勿在其他线程并发修改进程环境变量）。
  - 单次申请过程中的总线方法调用共享 `1 秒` 的单调时钟预算；若超时、权限被拒或未收到合法的 `uint32` Cookie 回复，适配器会立即关闭私有连接并原样返回错误状态码（若缺少底层运行库或桌面服务则返回 `HX_OS_NOT_SUPPORTED`）。
- **通过关闭私有连接完成释放**：停用（`set_active(false)`）或销毁（`destroy`）请求时，适配器会直接关闭该对象独占的私有会话总线连接，而不会阻塞等待远端 `Uninhibit` 响应。由于桌面电源管理服务绑定的是连接所有者（Bus Peer），一旦观察到连接断开就会自动撤销对应的防睡眠 Cookie，同时完全不影响进程内其他连接（详见 [`sd-bus` 连接所有权说明](https://github.com/systemd/systemd/blob/main/man/sd_bus_default.xml)）。

## 3. 请求状态与服务重启

- **幂等激活与状态查询**：对已处于激活状态的对象重复调用 `hx_os_sleep_inhibit_set_active(request, 1, &error)` 是幂等的，不会叠加引用计数；调用 `set_active(request, 0, &error)` 会关闭当前私有连接并释放租约，此后若再次传入 `1` 则会建立新连接并重新申请。
- **桌面服务重启后的恢复**：`hx_os_sleep_inhibit_active()` 返回的是本地记录的最近一次请求状态，而非实时轮询桌面守护进程的健康检查。如果桌面会话服务在运行中途发生重启，远端租约可能已经丢失而本地 `active()` 仍显示为真；当应用感知到桌面服务重启后，只需先调用 `set_active(false)` 再调用 `set_active(true)`（或直接销毁重建对象）即可恢复。
- **进程 `fork` 与生命周期约束**：切勿在 `fork()` 产生的子进程中继续使用从父进程继承来的活跃防睡眠对象。在销毁所有防睡眠请求之前，必须保持 `hx_c_platform` 动态库、关联的 `hx_context` 以及相关宿主资源有效。

## 4. 可选的操作系统运行库

Linux 桌面防睡眠适配由 `HX_OS_ENABLE_LINUX_DESKTOP` 与 `HX_OS_ENABLE_SLEEP_INHIBIT` 共同控制（默认均开启，仅作用于桌面 Linux 构建，不包含 Android）：

- **按需动态加载系统 `sd-bus`**：适配器在运行时通过公开的 `sd-bus` C ABI 动态加载系统已安装的 `libsystemd.so.0` 或 `libelogind.so.0`（使用 `RTLD_NODELETE` 保持符号映射，防止库卸载后内部 TLS 悬空）。本仓库不拷贝或静态打包 `systemd` / `elogind` 源码，也不引入 Qt 或 GLib 依赖；如果目标机器未安装上述运行库或缺失必要符号，调用会安全返回 `HX_OS_NOT_SUPPORTED`。
- **无桌面/嵌入式裁剪**：在不需要桌面集成的服务器或嵌入式 Linux 构建中，可将 `-DHX_OS_ENABLE_LINUX_DESKTOP=OFF` 或 `-DHX_OS_ENABLE_SLEEP_INHIBIT=OFF` 关闭。该模块不依赖 `NETWORK`、`LOOP`、`LIBRARY` 或 `PATHS`（静态链接时 CMake 与 `pkg-config` 会自动携带系统 `dl` 库依赖）。此外，Linux 下的性能请求（`hx_os_performance_request`）仍固定返回 `HX_OS_NOT_SUPPORTED`。

## 5. C++11 与验证

在 C++11 中包含 `<hx/Power.hpp>`，即可使用只可移动的 `hx::SleepInhibit` 包装类（通过 `hx::Result` 报告状态，不使用异常）：

```cpp
auto request = hx::SleepInhibit::create("Finishing a file export");
if (!request)
	return request.status();

// Perform application work while request.value() remains alive.
return request.value().close().status();
```

先停用再调用 `close()` 是完全合法的，且绝不会释放其他实例持有的防睡眠请求。由于请求能否最终阻止物理机器休眠取决于具体桌面环境（GNOME / KDE）、`logind` / `elogind` 配置、会话权限与硬件电源策略，请在真实目标桌面环境中验证实际防休眠效果。其他平台边界详见[平台支持](PLATFORMS.zh-CN.md)。
