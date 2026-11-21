# Windows 崩溃现场保存：WER LocalDumps

[English](LOCAL_DUMPS.md) · [Windows 工具](WINDOWS_UTILITIES.zh-CN.md) · [分析工具](ANALYSIS.zh-CN.md) · [README](../README.zh-CN.md)

当部署在客户机器上的 Windows 应用程序发生未处理异常崩溃时，排查问题最直接的凭据就是系统生成的 `.dmp` 进程转储文件（Dump）。`WER LocalDumps` 模块（`<hx_os_local_dumps.h>` / `<hx/LocalDumps.hpp>`）提供了一套安全的配置接口，用于向 Windows 错误报告（Windows Error Reporting, WER）注册应用的本地转储规则，告诉操作系统：“当指定 `.exe` 崩溃时，请自动在本机指定目录保存一份 `.dmp` 现场文件。”

请注意本模块的职责边界：**它仅负责在管理员授权下配置、查询或移除 Windows 注册表中的 `LocalDumps` 策略**——它本身不在进程内安装异常捕获器、不自己写出 `.dmp` 文件、不监控死锁卡死，也不会向微软或外部服务器上传任何客户数据。仅仅编译、链接或加载本模块绝不会自动修改系统的任何设置。

`HX_OS_ENABLE_LOCAL_DUMPS` 默认开启（`ON`），独立于 `REGISTRY`、`PROCESS` 和 `SYSTEM_UTILS` 模块（在 Windows 上自动链接系统 `Advapi32.lib`）。在非 Windows 平台或关闭该开关的构建中，函数声明依然保留并固定返回 `HX_OS_NOT_SUPPORTED`；运行时可通过 `hx_os_local_dumps_available()` 检查当前构建是否包含该实现。

## 1. 安装软件时，为客户程序设置一次

由于 Windows `LocalDumps` 策略存储在 `HKEY_LOCAL_MACHINE`（`HKLM`）下，配置操作需要管理员权限。最佳实践是在**拥有管理员权限的安装程序（Installer）或部署脚本**中为目标业务程序配置一次；配置落盘后长期有效，客户日常只需以普通标准用户权限运行业务程序即可，无需每次启动软件都请求管理员提权。

下面的完整 C 程序演示如何为名为 `MyApp.exe` 的业务程序开启默认本地转储配置（运行前请将 `"MyApp.exe"` 替换为你自己的可执行文件名，编译方式见[C 工程配置](USAGE.zh-CN.md#c-project)）：

```c
#include <hx_os_local_dumps.h>
#include <stdio.h>

int main(void)
{
	hx_os_error error;
	hx_os_status status =
		hx_os_local_dumps_enable("MyApp.exe", NULL, &error);

	if (status != HX_OS_OK) {
		fprintf(stderr, "%s; native=%lld; cleanup=%lld\n",
			hx_os_status_name(status), (long long)error.native_code,
			(long long)error.cause_code);
		return 1;
	}

	puts("LocalDumps configured for MyApp.exe.");
	return 0;
}
```

- **目标程序名（`application`）规则**：
  - 仅接受以 `.exe` 结尾的单个 UTF-8 文件名（如 `"MyApp.exe"`，最长 `255` 个 UTF-16 码元），**不接受包含目录分隔符的路径、通配符或 `.dll` 名称**。
  - 若传入 `application = NULL`，则自动选取当前正在运行的**宿主 `.exe` 文件名**（即使该调用发生在某个 DLL 内部，配置的也是宿主 EXE 而非 DLL；因此如果是独立的安装程序 `setup.exe`，请务必显式传入客户业务程序名 `"MyApp.exe"`）。
  - Windows 按文件名不区分大小写匹配崩溃进程；不同安装目录下同名的 `.exe` 在同一注册表视图中共享该项配置。
- **默认配置项（`options = NULL`）**：
  - **转储类型**：完整转储（`HX_OS_LOCAL_DUMPS_FULL`，包含进程全部内存数据，比 Windows 系统自带默认的紧凑小转储更利于离线还原复杂现场）。
  - **保留份数**：最多保留 `3` 份（超过后由 Windows 自动滚动覆盖最旧的转储文件）。
  - **注册表视图**：跟随当前调用进程的位数视图（`HX_OS_LOCAL_DUMPS_VIEW_DEFAULT`）。
  - **保存目录**：字面量 `"%LOCALAPPDATA%\\CrashDumps"`（以 `REG_EXPAND_SZ` 写入注册表，待实际发生崩溃时由 Windows 按**崩溃进程所属运行账号**动态展开，而非在安装阶段展开成管理员自己的目录）。
- **权限与安全边界**：
  - 函数仅修改 `HKLM\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps\<MyApp.exe>` 子项，不触碰全局 WER 配置，不自动创建磁盘文件夹或修改目录 ACL，也不会自行弹窗请求提权。若当前进程没有管理员权限，调用会明确返回 `HX_OS_ACCESS_DENIED` 并在 `hx_os_error` 中保留原生 Win32 错误码。
  - **隐私提醒**：完整内存转储（`FULL`）体积较大，且可能包含崩溃时刻内存中的口令、密钥或用户文档内容；同时，文件份数上限不等于磁盘字节配额上限，请在产品隐私说明与运维清理策略中妥善规划。切勿在 `DllMain`、信号处理函数或崩溃异常处理函数内部调用本配置接口。

## 2. 修改目录、份数、现场类型和注册表视图

如果需要自定义转储目录、保留份数、转储类型（`HX_OS_LOCAL_DUMPS_MINI` 或 `HX_OS_LOCAL_DUMPS_FULL`）或显式指定 32/64 位注册表视图，请先用 `hx_os_local_dumps_options_init` 初始化选项结构体：

```c
hx_os_local_dumps_options options;
hx_os_error error;

hx_os_status status =
	hx_os_local_dumps_options_init(&options, sizeof(options));
if (status == HX_OS_OK) {
	options.dump_count = 5;
	options.dump_type = HX_OS_LOCAL_DUMPS_FULL;
	options.registry_view = HX_OS_LOCAL_DUMPS_VIEW64;
	options.dump_folder = "%LOCALAPPDATA%\\MyApp\\CrashDumps";
	status = hx_os_local_dumps_enable("MyApp.exe", &options, &error);
}
```

- **自定义目录要求**：`dump_folder` 必须为严格 UTF-8 编码的 Windows 绝对路径，或以 `%环境变量%` 开头且展开后为绝对路径的字符串（最长 `32,767` 个 UTF-16 码元，按原样写入 `REG_EXPAND_SZ`，配置时不提前展开）。若指定了固定自定义目录，安装程序或管理员需提前创建该文件夹并赋予业务运行账号写权限（**切勿为了省事将崩溃转储目录设为所有用户可读**）。
- **32 位与 64 位注册表视图选择**：`HX_OS_LOCAL_DUMPS_VIEW_DEFAULT` 使用调用进程自身的注册表视图，并不会去探测磁盘上 `MyApp.exe` 的 PE 架构。如果使用 32 位安装程序去配置 64 位业务程序（或反之），请显式指定 `HX_OS_LOCAL_DUMPS_VIEW64` 或 `HX_OS_LOCAL_DUMPS_VIEW32`。在 64 位 Windows 上若需同时覆盖 32 位与 64 位子程序，可分别对两个视图各调用一次 `enable`（后续执行 `query` 和 `remove` 时也请传入相同的视图枚举）。

## 3. 不覆盖已有设置，移除时也先检查

为防止破坏系统管理员或其他运维工具手动配置好的策略，本模块遵循**绝不静默覆盖外部已有设置**的原则：

- **管理标记与幂等开启**：新建应用子项时，库会写入三个标准 WER 键值（`DumpFolder`、`DumpCount`、`DumpType`）以及一个私有的标记值 `HxLocalDumpsVersion = 1`（`REG_DWORD`）。如果目标子项已存在，只有当它带有 `HxLocalDumpsVersion = 1` 标记、三个配置值与本次请求完全一致、且不包含任何额外键值或嵌套子项时，重复调用 `enable` 才会幂等返回 `HX_OS_OK`；否则一律返回 `HX_OS_EXISTS` 并保持注册表原样不变。
- **安全移除（`hx_os_local_dumps_remove`）**：

```c
hx_os_status status = hx_os_local_dumps_remove("MyApp.exe", NULL, &error);
```

调用 `remove` 时，若传入 `options = NULL`，则仅移除由本库创建且仍保持默认值的配置（若之前使用的是自定义 `options`，移除时也必须传入相同的 `options`；若该应用子项本来就不存在，也直接返回 `HX_OS_OK`）。如果检测到注册表项是由外部工具创建的、缺少本库标记、值被手工修改过（其中 `DumpFolder` 按未展开的字面字符串严格比对）或包含额外键值/子键，函数会拒绝删除并返回 `HX_OS_BUSY`。

> [!NOTE]
> **移除与回滚边界**
> - `remove` 只删除注册表中的 `<MyApp.exe>` 策略子键，**不会**删除磁盘上已经生成的 `.dmp` 文件，也不会删除父级 `LocalDumps` 键或全局 WER 设置。请注意：移除针对单个应用的设置后，如果机器上配置了全局 WER `LocalDumps` 策略，Windows 仍可能按全局策略收集转储。
> - 注册表多值写入不是原子事务。在安装或卸载期间，请避免多个进程并发修改同一应用的注册表项。若写入中途出错，库会尝试回滚删除刚刚新建的子项（若回滚清理本身也失败，会将清理阶段的 Win32 错误码记录在 `error.cause_code` 中；重试前建议先调用 `query` 检查当前状态）。

## 4. 只查询，不修改 Windows

使用 `hx_os_local_dumps_query` 可以无副作用地检查某个应用在指定注册表视图下的当前配置：

```c
hx_os_local_dumps_info info;
hx_string folder = HX_STRING_INIT;
hx_os_error error;

hx_os_status status = hx_os_local_dumps_query("MyApp.exe",
	HX_OS_LOCAL_DUMPS_VIEW_DEFAULT, &info, sizeof(info), &folder, &error);
if (status == HX_OS_OK)
	printf("fields=%u type=%u count=%u managed=%u folder=%s\n",
		(unsigned)info.fields, (unsigned)info.dump_type,
		(unsigned)info.dump_count, (unsigned)info.managed,
		hx_string_c_str(&folder));

hx_string_destroy(&folder);
```

- **字段存在性掩码（`info.fields`）**：通过位掩码 `HX_OS_LOCAL_DUMPS_HAS_TYPE`、`HX_OS_LOCAL_DUMPS_HAS_COUNT` 和 `HX_OS_LOCAL_DUMPS_HAS_FOLDER` 标明注册表子项中实际存在哪些键值（缺失的数值字段填 `0`，缺失的目录填为空字符串 `""`）。为方便检视外部配置，查询接口同时接受 `REG_EXPAND_SZ` 和 `REG_SZ` 类型的目录值并按字面原样返回。
- **`info.managed` 与 `HX_OS_NOT_FOUND` 的含义**：`info.managed` 为真表示该子项包含完整的本库标记且无多余键值/子键。当该应用在所选视图下没有专属子键时，查询返回 `HX_OS_NOT_FOUND`（这仅代表没有应用级单独配置，不代表全局 WER 转储也处于关闭状态）。

## 5. C++11 和不会默认改设置的命令行示例

在 C++11 中包含 `<hx/LocalDumps.hpp>`，即可通过 `hx::LocalDumps` 命名空间调用 `enable`、`query` 和 `remove`（均返回 `hx::Result`，且 `query` 返回的结果对象离开作用域析构时不会撤销系统注册表中的持久化设置）：

```cpp
auto enabled = hx::LocalDumps::enable("MyApp.exe");
if (!enabled)
	return enabled.status();

auto settings = hx::LocalDumps::query("MyApp.exe");
if (!settings)
	return settings.status();

return HX_OS_OK;
```

仓库提供的公开命令行工具 [local_dumps.c](../examples/local_dumps.c) 在不带参数运行时仅打印用法说明，绝不会自动修改系统设置或故意触发崩溃。在开启了 `HX_OS_BUILD_EXAMPLES=ON` 的 MSVC 构建目录中，可以先以普通权限编译并执行只读查询：

```powershell
cmake --build build-shared-msvc --config Release --target hx_os_local_dumps
.\build-shared-msvc\bin\Release\hx_os_local_dumps.exe
.\build-shared-msvc\bin\Release\hx_os_local_dumps.exe query MyApp.exe 64
```

只有在**管理员权限的终端**中显式传入 `enable` 或 `remove` 子命令时，示例才会写入或移除注册表配置（程序名传入 `-` 表示选择 `hx_os_local_dumps.exe` 自身；末尾还可追加可选的自定义目录参数）：

```powershell
.\build-shared-msvc\bin\Release\hx_os_local_dumps.exe enable MyApp.exe 64
.\build-shared-msvc\bin\Release\hx_os_local_dumps.exe remove MyApp.exe 64
```

## 6. 客户出问题后，收集什么

1. **构建端归档符号（PDB）**：每次发布正式版本时，开发团队应在内部归档好完全匹配的 `.exe`、`.dll`、`.pdb` 符号文件及对应源码提交号。Windows 生成 `.dmp` 文件并不要求客户电脑上安装 `.pdb` 符号，因此只需向客户分发运行二进制即可。
2. **故障后收集信息**：当客户反馈闪退时，请客户从默认目录 `%LOCALAPPDATA%\CrashDumps`（若为 Windows 服务进程则位于对应服务账号的配置目录下）取回 `.dmp` 文件，并附上软件版本号、发生时间、运行日志与复现步骤。
3. **使用 Visual Studio 或 WinDbg 定位**：在开发机上用 Visual Studio 或 WinDbg 打开 `.dmp` 并配置好匹配的 `.pdb` 路径，即可直接看到崩溃线程的调用栈与局部变量。如果崩溃是由早先的堆内存越界破坏引起的，崩溃点可能滞后于第一案发现场，此时可结合[可选的 CPU 与内存分析](ANALYSIS.zh-CN.md)中的内存检查工具定位根因。
4. **卡死（Hang）与拦截注意事项**：进程死锁或界面卡死不会自动触发崩溃转储，通常需要在强行结束进程前通过任务管理器“详细信息 -> 创建转储文件”手动抓取。此外，如果应用程序自身安装了吞掉异常的顶层异常过滤器，或系统注册了抢先接管的 JIT 调试器（`AeDebug`），可能会阻止 WER `LocalDumps` 生成文件。

## 7. 验证范围要分开记录

在工程质量保障中，建议将“注册表配置逻辑单元测试”与“目标机器真实崩溃落盘验收”分开执行：
- 库自带的自动化回归测试仅在隔离的测试进程内将句柄重定向到独享的临时 `HKCU` 测试子树，验证 Unicode 路径、冲突保护、权限拒绝、32/64 位视图切换与回滚清理，不触碰宿主机的真实 `HKLM` 设置。
- 在产品正式发版前，请在目标 Windows 版本、目标架构（x86/x64）与实际运行账号权限下，使用专门编写的可丢弃测试小工具验证一次真实的崩溃 `.dmp` 落盘，测试完毕后立即用 `remove` 清理测试配置（切勿拿存有未保存业务数据的生产程序做强制崩溃试验）。

更多底层细节可参阅微软官方文档：[Collecting User-Mode Dumps (LocalDumps)](https://learn.microsoft.com/en-us/windows/win32/wer/collecting-user-mode-dumps) 与 [Accessing an Alternate Registry View](https://learn.microsoft.com/en-us/windows/win32/winprog64/accessing-an-alternate-registry-view)。
