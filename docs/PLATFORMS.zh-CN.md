# 平台支持与部署指南

[English](PLATFORMS.md) · [返回首页](../README.zh-CN.md)

## 1. 目标平台与工具链基线

`hx_c_platform` 核心采用标准 C99 编写并导出稳定的 C ABI；可选的 C++ 封装层基于关闭异常的 C++11（Header-Only），C++20 无栈协程支持按需开启。构建系统要求 **CMake 3.16+**，且预编译依赖 SDK 的目标架构、CRT 运行库和 Debug/Release 配置必须与宿主程序保持一致。

| 目标平台 | 兼容基线 | 部署与集成要求 |
| --- | --- | --- |
| **Windows** | Windows 7 SP1 及以上；MSVC x86 / x64 | 动态部署需附带 `bin/` 下匹配的 DLL；若面向旧版 Windows 部署，所选第三方原生 SDK 也需以对应系统版本为目标编译 |
| **Linux 桌面 / 服务器** | GCC 或 Clang；基于 POSIX / Linux 原生 API | 匹配目标系统的 libc 与工具链版本；桌面电源管理（D-Bus）为可选运行库 |
| **嵌入式 Linux** | ARMv7-A、AArch64 | 匹配目标板卡的 sysroot、浮点 ABI（如 hard-float）和设备权限；裁剪部署可从 `cmake/minimal.cmake` 起步 |
| **Android** | API 21+；`arm64-v8a`、`armeabi-v7a` | 匹配 NDK ABI/API 等级与应用沙盒权限，详见 [Android 构建指南](ANDROID.zh-CN.md) |
| **macOS** | Intel 10.13、Apple Silicon (ARM64) 11.0 | 已包含 Apple（kqueue / Mach）适配层；Apple 真机环境构建与运行验收待完成 |

> [!NOTE]
> **关于旧系统与交叉编译环境的验收说明**：
> 上表列出的是代码设计的系统 API 基线。如果在 Windows 7/8.1、Android 5.0、实体 ARM 开发板或特定定制 libc 工具链上投产，仍需在真实目标硬件上执行完整功能与性能验收（QEMU 用户态模拟、模拟器或 Native Bridge 转译不能替代真实硬件测试）。BSD、iOS、FreeRTOS 及裸机环境目前不在支持范围内。

## 2. 运行时能力查询与平台差异边界

在业务代码中，可以通过 `hx_os_capabilities()` 查询当前二进制已启用的模块能力，并始终检查接口返回的 `hx_os_status`。即使某个模块在编译期已启用，个别特定于操作系统的扩展操作在当前宿主上不可用时也会明确返回 `HX_OS_NOT_SUPPORTED`，而权限不足或底层 I/O 失败则会保留对应的原生操作系统错误码。

常见跨平台行为差异如下：

- **Linux 桌面防休眠**：依赖宿主运行环境中可用的桌面 D-Bus 服务（`systemd-logind` 或 `elogind`）。在无桌面的服务器或嵌入式 Linux 上可直接在构建时关闭该模块，详见 [Linux 桌面电源管理](LINUX_DESKTOP.zh-CN.md)。
- **Android 沙盒与 API 等级限制**：应用私有目录需由宿主 App 传入；子进程创建能力要求最低构建 API 达到 Android API 28+；本地 Unix 域套接字与网卡状态监听受 Android SELinux 策略与 UID 权限约束。
- **有栈协程（Stackful Coroutines）**：依赖针对具体 CPU 架构实现的上下文切换后端。在启用了硬件影子栈（Shadow Stack / CET）的 Linux 环境上，受限操作会明确返回不支持状态，详见 [协程指南](COROUTINES.zh-CN.md)。
- **时钟、截止时间与同步文件 I/O**：各平台的时钟分辨率、定时器唤醒精度与磁盘刷盘（`fsync` / `FlushFileBuffers`）受底层内核与硬件限制。传入截止时间（Deadline）用于控制等待上限，并不代表能够强行中断任意正在内核态阻塞的底层文件 I/O。
- **UDP 分片与网络环境**：标准 UDP 不保证报文到达顺序或去重；在 WSL 镜像网络等虚拟化环境中，超长分片的 IPv4 UDP 报文可能被丢弃。建议将 UDP 报文控制在路径 MTU 以内，或根据场景改用 TCP / KCP，详见 [IPC 与传输协议选择](IPC.zh-CN.md)。

## 3. 构建、安装与部署清单

默认 CMake 配置仅编译核心库本身。传入 `-DHX_OS_BUILD_EXAMPLES=ON` 即可编译 `examples/` 下的公开示例程序。建议为不同的编译器、架构或 Debug/Release 配置分别维护独立的构建目录。

- **准备第三方依赖 SDK**：见 [依赖构建指南](DEPENDENCIES.zh-CN.md)。
- **CMake 构建开关与安装导出**：见 [构建与安装参考](REFERENCE.zh-CN.md#section-27)。
- **直接复用底层原生 SDK（OpenSSL / SQLite / libcurl）**：见 [底层依赖调用](NATIVE_DEPENDENCIES.zh-CN.md)。

> [!IMPORTANT]
> **动态与静态链接部署注意**：
> 即使将 `hx_c_platform` 自身编译为静态库，默认开启的 OpenSSL、libcurl、utf8proc 等外部 SDK 仍可能是动态链接库（DLL / `.so`）。发布程序时，请确保将这些运行库一并打包部署，并先在目标环境中跑通对应的最小示例以核对权限、路径与关闭行为。

## 4. 高级网络特性的平台边界

UDP 报文元数据读取、分散/聚集向量 I/O（Scatter/Gather）、带限流的 TCP 拨号器、c-ares 可取消 DNS 解析、TLS 安全配置模板（Profile）、HTTP 流式文件传输以及协程网络桥接均面向 **Windows、Linux 与 Android** 设计（Linux ARMv7/AArch64 复用相同的 POSIX/Linux 实现）：

- **网卡变化监听**：基于操作系统原生通知机制。在 Android 上可能因沙盒 SELinux 权限被拒绝；收到网卡变动通知后应重新查询网卡快照（通知机制不保证提供完整的历史事件流）。
- **操作系统专有 Socket 选项**：TCP User Timeout 仅适用于 Linux / Android；UDP ICMP 连接重置抑制（`SIO_UDP_CONNRESET`）仅适用于 Windows；命名 IPv6 Zone 解析需启用 `HX_OS_ENABLE_NET_OPTIONS`。
- **国际化域名（IDNA）与可取消 DNS**：URL 的 IDNA 转换取决于所链接的 `libcurl` 后端，未启用时返回 `HX_OS_NOT_SUPPORTED`。`c-ares` 解析器支持按需裁剪，但它遵循直接 DNS 报文解析逻辑，不会替代企业 VPN 或宿主系统的全部专有名字解析策略。库内所有网络适配层均不会修改系统防火墙、WSL 网络模式或全局网络配置。
