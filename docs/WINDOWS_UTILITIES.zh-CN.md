# Windows 版本、窗口与 GPU 查询

[English](WINDOWS_UTILITIES.md) · [WER LocalDumps](LOCAL_DUMPS.zh-CN.md) · [README](../README.zh-CN.md)

当应用在 Windows 平台上需要读取 `.dll` / `.exe` 文件的版本资源信息（如四段式版本号、`FileDescription`、`ProductName`），或者需要查询已有窗口（`HWND`）的几何边界、所属显示器以及系统中的物理/核显 GPU 适配器信息时，可以使用 Windows 平台专用查询模块（`<hx_os.h>` / `<hx/Windows.hpp>`）。

你可以先编译并运行完整示例 [windows_info.c](../examples/windows_info.c) 来查看实际效果。按[运行说明](../examples/README.zh-CN.md#run)构建 `hx_os_windows_info`：如果不带参数直接运行，程序会枚举当前机器上的全部 DXGI 显示适配器（输出 GPU 名称、LUID 与显存容量）；如果额外传入一个带有版本资源的 `.dll` 或 `.exe` 路径，程序还会先读取并打印该文件的版本信息。在非 Windows 平台上调用这些接口会明确返回 `HX_OS_NOT_SUPPORTED`。

## 1. 读取 DLL 或可执行文件版本

向 `hx_os_windows_version_query` 传入 UTF-8 文件路径即可读取 PE 版本资源。库在内部将相对路径按当前工作目录解析（不会沿系统的 DLL 搜索路径查找）、转换为宽字符路径、在有界内存预算内读取版本资源块并在返回前释放临时缓冲，**全程只读取静态文件资源，绝不会加载或执行目标文件**：

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

包含 `<hx_os.h>` 和 `<stdio.h>` 即可：

- **四段式版本号与结构校验**：`version.file` 数组的四个元素依次为主版本号（Major）、次版本号（Minor）、构建号（Build）和修订号（Revision）；`version.product` 保存对应的四段式产品版本。`flags`、`type` 和 `subtype` 保留 Windows `VS_FIXEDFILEINFO` 的原始语义。所有结构体查询都会校验调用方传入的缓冲区大小，失败时保持输出结构体与多余尾部字节不变。
- **读取版本描述字符串（`hx_os_windows_version_string`）**：若需读取 `FileDescription`、`CompanyName`、`ProductName` 或 `FileVersion` 等命名文本字段，可直接输出到 `hx_string`：

```c
hx_string description = HX_STRING_INIT;

hx_os_status status = hx_os_windows_version_string(
	"plugin.dll", "FileDescription", &description, NULL);
if (status == HX_OS_OK)
	puts(hx_string_c_str(&description));

hx_string_destroy(&description);
```

默认情况下，`hx_os_windows_version_string` 按文件资源中翻译表（`VarFileInfo\Translation`）的排列顺序逐一查找，返回第一个包含该字段的非空值，不会修改当前进程的 `locale`。如果你需要指定特定的语言与代码页组合（例如传入 `0x0804, 1200` 精确读取简体中文 Unicode 版本项，或传 `0, 0` 使用默认查找），可调用扩展入口 `hx_os_windows_version_string_ex`。

若目标文件没有版本资源或不存在所请求的字段名，函数返回 `HX_OS_NOT_FOUND`；资源块大小上限为 `16 MiB`，翻译表项上限为 `256` 项，字段名由 `1～64` 个 ASCII 字母、数字或下划线组成。

## 2. 查询已有窗口

本库不负责创建 GUI 窗口，也不依赖 Qt 等界面框架。宿主程序只需将自己已有的原生 Windows `HWND` 强转为 `uintptr_t` 传入，即可查询窗口属性、标题、类名或所属进程路径（句柄仅在调用期间借用，窗口生命周期仍由宿主程序管理）：

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

- **窗口几何与状态属性（`hx_os_windows_window_query`）**：返回的记录包含窗口所属的进程 ID（`process_id`）、线程 ID（`thread_id`）、距离最近的显示器句柄（`monitor`）、屏幕坐标边界矩形（右/下边界为开区间，受系统 DPI 虚拟化及 Windows 10/11 不可见缩放阴影边框影响，本库不会主动更改进程的 DPI 感知模式）以及可见、最小化、最大化标志（注：带有 `WS_VISIBLE` 样式不代表窗口未被其他窗口遮挡）。
- **跨线程查询窗口标题的防卡死规则（`hx_os_windows_window_title`）**：
  - 如[微软 `GetWindowTextW` 文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowtextw)所述，读取**当前进程内**窗口的标题会在底层发送 `WM_GETTEXT` 消息。为防止跨线程同步发消息导致界面死锁，**对于属于本进程的窗口，必须由创建该窗口的所属线程调用 `hx_os_windows_window_title`**；若从本进程的其他线程调用，函数会在发送消息前直接拦截并返回 `HX_OS_WOULD_BLOCK`。
  - 对于属于**其他进程**的窗口，Windows 原生 API 直接读取内核缓存的窗口标题栏文本（不发送跨进程消息，但也不能用于读取远程进程内部的子控件文本）。
- **窗口类名与所属进程路径**：`hx_os_windows_window_class` 读取 UTF-8 窗口类名；`hx_os_windows_window_process_path` 查询窗口所属进程的可执行文件完整路径（按当前进程常规权限打开查询，不自动提权）。文本缓冲区上限为 `32,767` 个 UTF-16 码元；若达到上限可能发生截断，函数会返回 `HX_OS_LIMIT`，绝不将截断后的半截字符串当作成功返回。

## 3. 识别显示器与 GPU

- **通过窗口关联显示器与输出显卡**：将上一节查到的 `info.monitor` 传给 `hx_os_windows_monitor_query`，可获取该显示器的屏幕矩形、工作区矩形（扣除任务栏）、原生设备名及主显示器（Primary）标志；将同一个 `info.monitor` 传给 `hx_os_windows_monitor_gpu_query`，即可查出当前驱动该显示输出端口的 GPU 适配器信息（注：在双显卡/混合架构笔记本上，负责驱动屏幕显示输出的核显不一定是实际渲染某个 3D 应用的独立显卡）。
- **按序号枚举全部 GPU（`hx_os_windows_gpu_query`）**：
  - 从下标 `0` 开始递增调用 `hx_os_windows_gpu_query(index, &gpu, sizeof(gpu), &error)`，直到返回 `HX_OS_NOT_FOUND` 表示枚举结束。
  - 返回的 `hx_os_windows_gpu_info` 包含 UTF-8 适配器名称、本地唯一标识符（`LUID`，在同一系统启动会话内唯一，可用于跨 API 精确比对同一块显卡，但重启后会重新分配）、PCI 厂商/设备/子系统/修订 ID、专用显存/系统共享显存的总容量字节数（注意是硬件/驱动报告的容量上限，而非当前实时剩余空闲显存）以及软件适配器标志。
  - **DXGI 1.1 实现边界**：底层基于自 Windows 7 起即内置的 DXGI 1.1（[CreateDXGIFactory1](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-createdxgifactory1)），每次查询创建独立的工厂并在返回前释放全部 COM 引用，不创建 D3D 渲染设备、不修改线程 COM Apartment 状态、也不启动后台线程（最多枚举 `256` 个适配器、每个适配器 `256` 个显示输出）。**切勿在 `DllMain` 中调用 GPU 查询**；在无显卡会话、Windows 服务或部分远程桌面环境中，DXGI 可能明确返回 `HX_OS_NOT_SUPPORTED` 或 `HX_OS_NOT_FOUND`。

## 4. C++11 与可运行示例

在 C++11 中包含 `<hx/Windows.hpp>`，所有查询均通过 `hx::Result<T>` 返回结果与原生错误详情，不使用 C++ 异常：

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

（使用 `std::puts` 需包含 `<cstdio>`。）除 `fileVersion` 和 `versionString` 外，`hx::Windows` 命名空间还提供 `window`、`windowTitle`、`windowClass`、`windowProcessPath`、`monitor`、`gpu` 和 `monitorGpu`。

编译公开示例 `hx_os_windows_info` 后，在 Visual Studio 生成器构建目录下可直接运行：

```powershell
.\build\bin\Release\hx_os_windows_info.exe
```

你也可以向它传入任意带有版本资源的 `.exe` 或 `.dll` 文件路径（例如系统文件或本地构建产物），程序会先打印该文件的版本号与描述字符串，随后列出本机所有 GPU（注：示例程序本身通过 `HX_OS_ENABLE_SYSTEM_UTILS` 获取原生 UTF-8 命令行参数以支持中文路径，但 Windows 查询模块自身并不依赖 `SYSTEM_UTILS`）。

## 5. 开关与平台限制

本页功能由两个独立的 CMake 开关控制，默认均开启（`ON`）：
- `HX_OS_ENABLE_WINDOWS_VERSION`：控制 PE 版本资源查询，自动链接系统 `Version.lib`。
- `HX_OS_ENABLE_WINDOWS_DESKTOP`：控制窗口、显示器与 DXGI GPU 查询，自动链接系统 `User32.lib`、`DXGI.lib` 和 `DXGUID.lib`（CMake 导出目标与静态 `pkg-config` 元数据会自动携带这些系统库依赖）。

在非 Windows 操作系统或关闭对应开关时，所有函数声明保持可用并返回 `HX_OS_NOT_SUPPORTED`。各目标平台的编译器与系统基线详见[平台支持](PLATFORMS.zh-CN.md)。
