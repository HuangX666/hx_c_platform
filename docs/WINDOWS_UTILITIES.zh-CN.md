# Windows 版本、窗口与 GPU 查询

[English](WINDOWS_UTILITIES.md) · [README](../README.zh-CN.md)

## 1. 读取 DLL 或可执行文件版本

传入 UTF-8 文件路径即可。库内部相对当前工作目录解析路径、转换 Windows 编码、限制资源分配大小，并在返回前释放临时资源。查询只读取资源，不执行目标文件，也不沿 DLL 搜索目录查找。

```c
hx_os_windows_version_info version;
hx_os_error error;

hx_os_status status = hx_os_windows_version_query(
	"plugin.dll", &version, sizeof(version), &error);
if (status == HX_OS_OK) {
	printf("%u.%u.%u.%u\n", (unsigned)version.file[0],
		(unsigned)version.file[1], (unsigned)version.file[2],
		(unsigned)version.file[3]);
}
```

包含 `<hx_os.h>` 和 `<stdio.h>`。四段依次是主版本、次版本、构建号、修订号；`product` 保存相应的产品版本。flags、type 和 subtype 保留 Windows 资源含义。size 和 version 描述写入的 v1 记录。所有结构体查询都检查调用方容量，只写当前记录；失败保持输出不变，也不触碰容量中多余的尾部字节。

查询 `FileDescription`、`CompanyName`、`ProductName` 或 `FileVersion` 等文字时，直接写入字符串：

```c
hx_string description = HX_STRING_INIT;

hx_os_status status = hx_os_windows_version_string(
	"plugin.dll", "FileDescription", &description, NULL);
if (status == HX_OS_OK)
	puts(hx_string_c_str(&description));

hx_string_destroy(&description);
```

默认按资源中翻译表的顺序，返回第一个包含该字段的值，不修改进程 locale。`_string_ex` 可指定语言／代码页，例如 `0x0804, 1200` 选择资源存在的简体中文 Unicode 项；`0, 0` 使用默认查找。缺失字段或版本资源返回 `NOT_FOUND`；资源损坏、权限问题和分配失败分别报告。资源上限 16 MiB，翻译项上限 256。字段名为 1～64 个 ASCII 字母、数字或下划线；`hx_os_error` 保留原生错误。

## 2. 查询已有窗口

库不创建 GUI，也不依赖 Qt。把已有 Windows `HWND` 转成 `uintptr_t` 即可查询。句柄仅借用，窗口生命周期仍由调用方负责；Windows 可能复用已销毁的句柄值。

```c
hx_os_windows_window_info info;
hx_string title = HX_STRING_INIT;
uintptr_t window = (uintptr_t)existing_hwnd;

hx_os_status status = hx_os_windows_window_query(
	window, &info, sizeof(info), NULL);
if (status == HX_OS_OK)
	printf("Process: %u; thread: %u\n", info.process_id, info.thread_id);

status = hx_os_windows_window_title(window, &title, NULL);
if (status == HX_OS_OK)
	puts(hx_string_c_str(&title));

hx_string_destroy(&title);
```

`existing_hwnd` 由宿主程序提供。连接原生对象的代码包含 Windows 头，库的公共头本身不暴露这些类型；Qt 程序在 Windows 上可传入自己的窗口 ID。本进程的窗口标题须在该窗口所属线程查询，其他线程在发送消息前返回 `WOULD_BLOCK`。所属线程读取可能重入窗口过程，不承诺硬截止时间。对于其他进程，原生 API 读取的是窗口标题，不能用来取得任意远程控件内容。[Microsoft 对标题读取行为的说明](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowtextw)。

窗口记录包含进程／线程 ID、最近的显示器、边界和可见／最小化／最大化标志。有可见样式不代表未被遮挡。边界沿用 Windows 屏幕坐标，右／下边界不包含在内；原生 DPI 虚拟化和不可见的缩放边框可能影响数值。库不改变 DPI 模式。这些查询反映随时变化的 UI 状态，不构成整个桌面的原子快照。

`hx_os_windows_window_class` 返回 UTF-8 类名；`hx_os_windows_window_process_path` 返回所属进程的可执行文件路径，遵守普通访问权限且不提权。文本缓冲从小容量开始按需增长，限制为 32,767 个 UTF-16 单元；可能截断时返回 `LIMIT`，不会将缩短字符串作为成功结果。输出使用字符串自己的 context，失败保持不变；同一输出对象由调用方同步访问。

## 3. 识别显示器与 GPU

将 `info.monitor` 传给 `hx_os_windows_monitor_query`，取得显示器／工作区边界、原生设备名和主屏标志；传给 `hx_os_windows_monitor_gpu_query`，则查找拥有这个显示输出的适配器。在混合显卡系统里，这不一定是渲染某个应用内容的 GPU。

`hx_os_windows_gpu_query(index, &gpu, sizeof(gpu), &error)` 从 0 开始查询当前适配器，`NOT_FOUND` 表示枚举结束。结果包含 UTF-8 名称、LUID、厂商／设备／子系统／修订 ID、内存容量及软件适配器标志。名称不保证唯一；内存是原生容量，不是当前剩余内存。LUID 可在本次系统会话内比较，不是跨重启的永久编号；热插拔也可能改变枚举序号。每次调用创建新的查询工厂，连续按序号查询并不是原子快照。

适配使用 Windows 7 已提供的 DXGI 1.1，不创建渲染设备或 COM apartment，不启动工作线程，返回前释放全部 DXGI 引用。不要从 `DllMain` 调用。原生显卡操作不承诺硬截止时间。查找限制为 256 个适配器、每个适配器 256 个输出。无桌面、服务或远程环境可能明确返回不支持、暂不可用或未找到。[CreateDXGIFactory1 契约](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-createdxgifactory1)。

## 4. C++11 与可运行示例

包含 `<hx/Windows.hpp>`，在返回 `hx_os_status` 的函数中：

```cpp
auto version = hx::Windows::fileVersion("plugin.dll");
if (!version)
	return version.status();

auto description = hx::Windows::versionString("plugin.dll", "FileDescription");
if (!description)
	return description.status();

std::puts(description.value().c_str());
return HX_OS_OK;
```

`std::puts` 还需包含 `<cstdio>`。便利层另有 window、windowTitle、windowClass、windowProcessPath、monitor、gpu 和 monitorGpu，均返回 Result、不使用异常，复用 C 实现并保留原生错误。

构建 `hx_os_windows_info` 后，在 Visual Studio 生成器下运行 `build/bin/Release/hx_os_windows_info.exe build/bin/Release/hx_os_test_windows_util.exe`。[windows_info.c](../examples/windows_info.c) 是完整程序；测试文件的版本号为 `1.2.345.678`，描述为 `Window helper fixture`，GPU 输出以机器实际情况为准。命令行示例通过 SYSTEM_UTILS 取得 UTF-8 参数，中文路径不依赖 Windows ANSI 代码页；查询模块本身不依赖 SYSTEM_UTILS。

## 5. 开关与平台限制

`HX_OS_ENABLE_WINDOWS_VERSION` 和 `HX_OS_ENABLE_WINDOWS_DESKTOP` 默认开启，可分别关闭。版本查询链接系统 Version；桌面查询链接 User32／DXGI／DXGUID。CMake 导出与 pkg-config 的静态依赖包含这些系统库。其他系统保留声明并返回 `NOT_SUPPORTED`。available 查询表示已编译适配，不代表某个窗口、显示器、资源或 GPU 一定存在。

目标平台的编译、部署要求及限制见[平台支持](PLATFORMS.zh-CN.md)。公开示例独立于维护者的本地回归。
