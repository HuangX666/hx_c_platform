# Linux 桌面作用域防睡眠

[English](LINUX_DESKTOP.md) · [README](../README.zh-CN.md)

## 1. 继续使用同一个 C 入口

较长的文件导出可以请求桌面在工作期间保持机器唤醒。工作开始时创建请求，结束后销毁：

```c
hx_os_sleep_inhibit *request = NULL;
hx_os_status status = hx_os_sleep_inhibit_create(
	NULL, "Finishing a file export", &request, NULL);
if (status != HX_OS_OK)
	return status;

/* Perform the application's work while request remains alive. */
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

维护者使用本机隔离 D-Bus 回归验证服务发现、Unicode、权限、超时与清理。客户可编译并运行 [sleep_inhibit.c](../examples/sleep_inhibit.c)，在有对应桌面服务的环境体验防睡眠申请；该示例实际申请临时桌面租约。历史运行结果见[验证记录](VALIDATION.zh-CN.md)。

以上证明的是协议适配和资源生命周期。真实 GNOME／KDE 桌面的电源策略／休眠效果、libelogind、旧发行版、musl 与嵌入式目标，仍需分别进行目标验证，见[验证记录](VALIDATION.zh-CN.md)；WSL 协议测试不等于验证了物理机器不会休眠。
