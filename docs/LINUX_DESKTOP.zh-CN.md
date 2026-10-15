# Linux 桌面作用域防睡眠

[English](LINUX_DESKTOP.md) · [README](../README.zh-CN.md)

**先运行一个完整例子。** 执行文件导出等短期工作，希望桌面系统暂时不自动休眠时，申请作用域内的防睡眠请求。

打开 [sleep_inhibit.c](../examples/sleep_inhibit.c)。sleep_inhibit_create → 执行业务工作 → sleep_inhibit_destroy 释放请求。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 examples，并只构建 `hx_os_sleep_inhibit`。此例无命令行参数、无需输入文件。

**会得到什么：**服务可用时输出申请和释放两条提示。无相应桌面／登录服务时会报告不支持或原生错误；WSL 和无桌面的嵌入式系统不保证存在这种服务。

下面的短片段用于解释接口；复制运行时以完整示例为准。也可按[最小工程配置](USAGE.zh-CN.md#c-project)把完整源码作为自己的 main。

## 1. 继续使用同一个 C 入口

较长的文件导出可以请求桌面在工作期间保持机器唤醒。工作开始时创建请求，结束后销毁：

```c
hx_os_sleep_inhibit *request = NULL;
hx_os_status status = hx_os_sleep_inhibit_create(
	NULL, "Finishing a file export", &request, NULL);
if (status != HX_OS_OK)
	return status;

// Perform the application's work while request remains alive.
return hx_os_sleep_inhibit_destroy(&request, NULL);
```

这段代码位于返回 hx_os_status 的函数内。完整程序见 [sleep_inhibit.c](../examples/sleep_inhibit.c)，构建目标为 hx_os_sleep_inhibit；其中的 20 ms 延时只是演示业务动作放在哪里。在 Linux 桌面运行 `build/bin/hx_os_sleep_inhibit`：服务支持时创建请求，完成后报告释放；缺少后端或服务时明确打印 not supported。示例不更改电源计划或桌面设置。

每个对象只持有自己的请求，一个消费者释放后，另一个消费者的请求仍可继续。reason 按 UTF-8 复制，允许 1～4,096 字节。调用方同步后可以转交其他线程，不能并发操作同一个对象。不启动工作线程或自动重连线程。

```mermaid
flowchart LR
    A[应用创建请求] --> B[独立的会话总线连接]
    B --> C[桌面授予作用域请求]
    C --> D[应用执行工作]
    D --> E[关闭自己的连接]
    E --> F[桌面释放该请求]
```

## 2. 平台实际行为

Linux 适配依次尝试已有的 org.freedesktop.PowerManagement、org.freedesktop.PowerManagement.Inhibit 别名和 org.gnome.SessionManager。前两者使用 PowerManagement Inhibit 接口；GNOME 使用 **flag 4，即休眠请求**，不使用代表 idle 的 flag 8，因此不会同时请求禁止自动锁屏。系统和用户策略仍可覆盖请求；用户手动休眠时，GNOME 可能显示这个阻止休眠的请求。[GNOME 方法契约](https://gnome.pages.gitlab.gnome.org/gnome-session/re04.html)、[KDE 接口](https://github.com/KDE/powerdevil/blob/master/daemon/dbus/org.freedesktop.PowerManagement.Inhibit.xml)。

请求不会自动启动缺失的桌面服务，也不请求弹出交互授权。显式的 DBUS_SESSION_BUS_ADDRESS 只允许本机 unix: 地址，总长最多 16 KiB；未设置时使用原生用户总线默认地址。创建或重新激活期间，不要并发更改进程环境。适配不请求远程总线、unixexec 辅助程序或自动启动守护进程。

各次方法调用共用一秒的单调时间预算；原生连接准备和操作系统调度不承诺硬截止时间。超时或错误时释放消息和独立连接，只有收到合法 uint32 cookie 回复才交付对象。保留原生 errno 风格错误；缺少运行库／服务返回 NOT_SUPPORTED，权限、超时和回复损坏不会伪装为成功。

停用或销毁时直接关闭自己的连接，不等待 Uninhibit 回复。桌面移除与断开连接所有者关联的请求；库报告本地资源已经释放，桌面异步观察断开事件。其他消费者的连接不受影响。[原生连接所有权说明](https://github.com/systemd/systemd/blob/main/man/sd_bus_default.xml)。

## 3. 请求状态与服务重启

重复 set_active(true) 不增加引用；set_active(false) 释放连接，之后的 true 新建连接并重新请求。active() 记录最近一次请求状态，不是桌面服务的健康检查。桌面或会话总线重启可能丢失原生请求，而这个标志仍为 true；发现重启后，先停用再激活，或重建对象。不在后台轮询或自动恢复。

不要在 fork 后的子进程继续操作继承来的活跃对象。销毁所有请求之前，保持 hx_c_platform、context 和相关宿主资源有效。destroy 消耗对象，与既有 Windows／Apple 契约一致。

## 4. 可选的操作系统运行库

HX_OS_ENABLE_LINUX_DESKTOP 默认开启，还需要 SLEEP_INHIBIT；只应用于 Linux 桌面构建，不包括 Android。它通过公开的 sd-bus C ABI 动态使用操作系统安装的 libsystemd.so.0 或 libelogind.so.0。项目没有复制实现代码／SDK 头，不构建或静态合入这个原生运行库，也不新增 Qt／GLib 依赖；装载时检查所有必需符号，缺失或不兼容的旧运行库返回 NOT_SUPPORTED。这是可选的系统运行库依赖；项目随附第三方源码继续放在 deps/。

原生提供方以 RTLD_NODELETE 保留映射，避免它自己的 TLS 指向已经卸载的代码。对象持有的 dlopen 引用、消息、连接和自有分配会释放，但提供方映射及其内部进程状态可能保留到进程结束。自有分配器注入不替换原生提供方的分配器，也不修改进程级分配器或共享总线配置。

不需要桌面功能的嵌入式／无桌面构建，可以关闭 LINUX_DESKTOP 或 SLEEP_INHIBIT。这项适配不依赖 NETWORK、LOOP、LIBRARY 或 PATHS；静态 CMake／pkg-config 消费会携带必需的系统 dl 链接项。能力位表示编入适配，不代表当前桌面一定提供服务。Linux 性能请求仍返回 NOT_SUPPORTED。

## 5. C++11 与验证

包含 `<hx/Power.hpp>`，在返回 hx_os_status 的函数中：

```cpp
auto request = hx::SleepInhibit::create("Finishing a file export");
if (!request)
	return request.status();

// Perform application work while request.value() remains alive.
return request.value().close().status();
```

沿用只可移动的同步便利类，以 Result 返回错误，不使用异常。停用后再 close 合法，也不会撤销别的对象持有的请求。

运行公开的 [sleep_inhibit.c](../examples/sleep_inhibit.c)，可以向当前桌面服务申请临时防睡眠。先确认服务可用，再检查返回状态。

申请能否阻止机器休眠，取决于 GNOME／KDE、logind／elogind、会话权限及机器电源策略。需要在实际桌面环境确认效果；WSL 协议通信不能证明物理机器不会休眠。其他边界见[平台支持](PLATFORMS.zh-CN.md)。
