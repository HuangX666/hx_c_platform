# 平台支持与部署

[English](PLATFORMS.md) · [README](../README.zh-CN.md)

## 1. 选择目标平台

核心采用 C99 和 C ABI，可选便利层使用关闭异常的 C++11；C++20 无栈语法按需开启。使用 CMake 3.16 或更新版本，依赖 SDK 的架构、运行库和配置需要与消费者匹配。

| 目标 | 兼容基线 | 部署要求 |
| --- | --- | --- |
| Windows | Windows 7 SP1；MSVC x86/x64 | 部署 `bin/` 下匹配的 DLL；选用的第三方 SDK 也需要兼容旧版 Windows |
| Linux 桌面／服务器 | GCC 或 Clang，POSIX／Linux 适配 | 匹配 libc 和工具链；桌面服务按需使用 |
| 嵌入式 Linux | ARMv7、AArch64 适配 | 匹配 sysroot、浮点 ABI 和设备权限；可从 `cmake/minimal.cmake` 开始 |
| Android | API 21；arm64-v8a、armeabi-v7a | 匹配 NDK ABI/API 和应用权限，见 [Android](ANDROID.zh-CN.md) |
| macOS | Intel 10.13、ARM64 11.0 设计基线 | 已有 Apple 适配，目标构建及运行验收仍待完成 |

这些描述的是实现和兼容边界，不代表所有系统版本或设备都经过验收。Windows 7/8.1、Android 5.0、Android ARMv7 运行、实体 ARM 板卡及其他 libc／工具链组合仍需目标环境验收。模拟器、native bridge 转译和 QEMU 不能证明真实设备行为或性能。BSD、iOS、FreeRTOS 和裸机不作为已经验证的移植目标宣传。

## 2. 查询能力并处理限制

使用模块能力查询并检查返回状态。模块已开启不等于所有平台扩展都可用；不支持的操作返回 `HX_OS_NOT_SUPPORTED`，原生权限和 I/O 错误保留诊断信息。

常见边界包括：

- Linux 防睡眠需要可用的桌面 D-Bus 服务，嵌入式／无桌面部署可以关闭，见[桌面电源请求](LINUX_DESKTOP.zh-CN.md)。
- Android 应用目录由宿主提供；进程能力取决于构建最低 API，本地套接字受 SELinux 与 UID 权限限制。
- 有栈协程需要已实现的 CPU／上下文后端；Linux 已开启影子栈时的限制会明确报告，见[协程](COROUTINES.zh-CN.md)。
- 时钟、超时、文件同步及设备访问保留各自的原生限制。截止时间不保证能够强行打断任意原生 I/O。
- 普通 UDP 可能丢失、乱序或重复；IPv4 分片还可能受 WSL 镜像网络影响。按路径容量控制数据报，或按需要使用 TCP／KCP，见 [IPC 传输选择](IPC.zh-CN.md)。

## 3. 构建、安装与运行示例

新配置默认只构建库。开启 `HX_OS_BUILD_EXAMPLES=ON` 后可以编译公开示例，无需维护者的私有文件。每种工具链／配置复用对应构建目录。

操作见[依赖准备](DEPENDENCIES.zh-CN.md)、[构建选项与安装](REFERENCE.zh-CN.md#section-27)、[底层 SDK 调用](NATIVE_DEPENDENCIES.zh-CN.md)。共享库部署需要带上所选运行库；静态链接本库不等于所有依赖都已静态链接。

先运行与业务相关的小示例，再在部署目标检查真实权限、路径、输入容量和关闭行为。详细回归日志及历史审查记录由维护者本地保存，不进入公开仓库和安装 SDK。远端 CI 保持关闭。
