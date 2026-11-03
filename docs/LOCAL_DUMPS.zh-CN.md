# Windows 崩溃现场保存：WER LocalDumps

[English](LOCAL_DUMPS.md) · [Windows 工具](WINDOWS_UTILITIES.zh-CN.md) · [分析工具](ANALYSIS.zh-CN.md) · [README](../README.zh-CN.md)

**这个模块用于明确告诉 Windows：某个程序崩溃以后，请在本机保存一个 `.dmp` 现场文件。** 它只负责配置 Windows，不自己捕获异常、写现场文件、检查内存损坏、恢复已经损坏的程序、监控卡死或上传客户数据。编译、链接或加载这个模块，都不会自动修改 Windows 设置。

C99 接口位于 `<hx_os_local_dumps.h>`，也由 `<hx_os.h>` 包含；不使用异常的 C++11 封装位于 `<hx/LocalDumps.hpp>`。`HX_OS_ENABLE_LOCAL_DUMPS` 默认 ON，独立于 REGISTRY、PROCESS 和 SYSTEM_UTILS。Windows 链接系统 Advapi32 库。其他平台或关闭模块时，接口仍可链接，但返回 `HX_OS_NOT_SUPPORTED`；`hx_os_local_dumps_available()` 只表示这个构建包含 Windows 实现。

## 1. 安装软件时，为客户程序设置一次

建议由获得授权、具有管理员权限的安装程序调用。客户以后正常权限运行业务程序即可，不应为了保存现场，每次都要求客户以管理员身份运行软件。设置在安装程序／进程退出后继续保留，不会因为某个 C++ 对象析构而撤销。

下面是完整 C 程序，它配置的是 **MyApp.exe**，不是安装程序本身。运行之前替换成真实业务程序名。构建方式见 [C 工程配置](USAGE.zh-CN.md#c-project)。

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

	puts("Configured. Verify actual crash collection on the target system.");
	return 0;
}
```

`application=NULL` 表示当前运行的**宿主 EXE**，即使函数是在 DLL 中调用，也不是配置 DLL。安装程序通常应明确传入客户业务 EXE 名。只接受以 `.exe` 结尾的单个 UTF-8 文件名，不接受路径、通配符或 DLL 名；上限为 255 个 UTF-16 单元。Windows 的程序名匹配不区分大小写；不同目录中同名的 EXE，会共享所选注册表视图里的设置。

`options=NULL` 的默认值是：完整现场、最多 3 份、调用进程的注册表视图、字面目录 `%LOCALAPPDATA%\CrashDumps`。这个目录由 Windows 按崩溃程序的运行账号展开，而不是安装程序提前展开成管理员的目录。这里为了排查问题，使用比 Windows 自身“较小现场、10 份”更明确的默认值。完整现场文件可能很大，也可能包含密码、文档和其他敏感数据。数量上限不等于磁盘空间上限。

函数只修改 `HKLM\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps` 下属于这个 EXE 的子项，不修改全局 WER 值，不调整目录权限，不自行提权，也不创建文件夹。返回 `ACCESS_DENIED` 表示当前权限不足，原生错误保留在 `hx_os_error`。不要从 `DllMain`、信号处理函数或异常处理函数中调用。

## 2. 修改目录、份数、现场类型和注册表视图

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

先初始化 options，再修改需要的字段。版本 1 支持 MINI 或 FULL、正数份数，以及 DEFAULT／VIEW32／VIEW64；不提供自定义现场位标志。自定义目录使用严格 UTF-8，必须是 Windows 绝对路径，或以 `%变量名%` 开头、最终展开为绝对路径的形式。目录以 `REG_EXPAND_SZ` 保存，配置时不展开；上限为 32,767 个 UTF-16 单元。自定义目录由安装程序或运维准备，并确保运行程序的账号可以写入，不要为了省事把客户现场开放给所有人读取。

DEFAULT 跟随调用配置函数的进程，而不是自动识别目标 EXE 的架构。安装程序如果配置另一种架构的程序，应明确选择对应视图。在 64 位 Windows 上需要覆盖 x86／x64 收集路径时，可以明确分别配置 VIEW32 和 VIEW64，再对各目标验证；这是两次独立操作，不是两边一起成功或一起撤销的事务。实现不硬编码 `Wow6432Node`，也不修改进程的全局视图。查询和移除时使用相同的视图。

## 3. 不覆盖已有设置，移除时也先检查

模块不会静默覆盖已经存在的应用子项。新建时写入三个 WER 值：`DumpFolder`、`DumpCount`、`DumpType`，并以私有 DWORD `HxLocalDumpsVersion=1` 标识本库管理的布局。重复开启只有在完整配置与请求完全相同、没有额外值和子项时才成功。其他已有配置一律返回 `EXISTS` 并保持原样，包括别人手动建立、数值恰好相同但没有本库标识的配置。

```c
hx_os_status status = hx_os_local_dumps_remove("MyApp.exe", NULL, &error);
```

这会移除本库建立且仍符合默认值的配置；应用子项已经不存在时也成功。如果之前使用自定义配置，应传回原来的 options，而不是 NULL。遇到其他工具创建、不完整、已修改或增加内容的配置，返回 `BUSY`，不删除。目录按字面字符串匹配，不是展开后指向同一位置就算相同。移除不递归删除，不删除已有 `.dmp` 文件，不删除父项或全局设置。移除应用的单独设置以后，可能重新使用全局 WER 设置，因此**不等于保证彻底关闭所有现场收集**。

安装／移除期间，调用方必须排除其他进程或线程同时修改这个应用的注册表项。多个值的写入不是事务，安装期间不应让目标程序同时进行崩溃测试。写入失败时，实现尝试删除自己这次新建的子项；清理失败会把 Windows 错误码放在 `error.cause_code`，重试前应检查现场设置。进程在配置中途被中断，也可能留下不完整设置。接口没有隐藏的“强制覆盖／强制删除”模式。原生关闭失败可能在设置已经改变后才返回，不能仅凭非零返回值就认定没有变化，应先查询。

## 4. 只查询，不修改 Windows

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

`fields` 中的 HAS_TYPE／HAS_COUNT／HAS_FOLDER 表示哪些值确实存在。缺失的数字字段为零，缺失目录为空字符串。目录原样返回、不展开；为了检查其他配置，查询也接受 REG_SZ／REG_EXPAND_SZ。值类型错误、编码错误或超过大小限制时明确失败。失败保持两份输出不变；成功只写已知大小的 info，字符串使用自己的 context。

`managed` 表示配置带有本库标识、布局完整且没有额外值或子项。它不是安全凭据，也不证明已知值从未被修改。移除时还会对照调用方传入的期望值。查询只是当时的观察，不保证和其他注册表编辑形成原子快照。

`NOT_FOUND` 只表示这个程序在所选视图中没有单独设置，**不表示 Windows 已经停止收集现场**：这个查询不合并全局 WER 设置和 Windows 默认值。`available()`、配置成功或查询成功，都不能证明目录权限、程序的崩溃处理方式以及 Windows 最终落盘一定正常。

## 5. C++11 和不会默认改设置的命令行示例

包含 `<hx/LocalDumps.hpp>` 后，在返回 `hx_os_status` 的函数中调用：

```cpp
auto enabled = hx::LocalDumps::enable("MyApp.exe");
if (!enabled)
	return enabled.status();

auto settings = hx::LocalDumps::query("MyApp.exe");
if (!settings)
	return settings.status();

return HX_OS_OK;
```

`LocalDumps::options()` 返回初始化好的 C options。enable、query、remove 复用 C 实现，通过 Result 返回结果；query 的结果自己持有目录字符串，析构不会撤销 Windows 中的持久设置。

完整公开示例 [local_dumps.c](../examples/local_dumps.c) 在不带参数时只显示帮助，不改设置，也不故意制造崩溃。现有 MSVC 构建开启 examples 后：

```powershell
cmake --build build-shared-msvc --config Release --target hx_os_local_dumps
.\build-shared-msvc\bin\Release\hx_os_local_dumps.exe
.\build-shared-msvc\bin\Release\hx_os_local_dumps.exe query MyApp.exe 64
```

只有明确执行 enable 或 remove 才写设置。对你有权管理的程序，在获得授权的管理员终端中执行：

```powershell
.\build-shared-msvc\bin\Release\hx_os_local_dumps.exe enable MyApp.exe 64
.\build-shared-msvc\bin\Release\hx_os_local_dumps.exe remove MyApp.exe 64
```

最后可以额外传目录参数，enable／remove 都使用完整现场、3 份。程序名传 `-` 是明确选择示例自身的 EXE。命令行示例通过 SYSTEM_UTILS 取得 UTF-8 参数，LocalDumps 库接口本身不依赖该模块。

## 6. 客户出问题后，收集什么

开发者保存每次实际发布的 EXE／DLL／PDB 和对应源码。客户通常只需要运行文件，Windows 保存现场不要求客户安装 PDB。发生崩溃后，收集 `.dmp`、完整软件版本、发生时间、日志和复现步骤。默认文件通常在运行程序的那个用户的 `%LOCALAPPDATA%\CrashDumps`，服务账号使用自己的用户目录。现场文件走私有传递渠道，并制定保留、清理规则。

在 Visual Studio 或 WinDbg 中使用匹配的 PDB 查看出错位置。空指针访问可能较直观，内存损坏却可能在第一次写错很久之后才导致崩溃。现场文件是一次快照，不是完整历史；仍需结合内存检查构建和复现测试。

程序卡死通常需要另行手动保存现场，例如在强制关闭前，使用任务管理器“详细信息”中的创建转储命令。没有崩溃的内存写坏、泄漏、正常退出、强制终止和断电，不会由这个模块自动诊断。程序自己的崩溃报告处理、自动启动调试器等情况，可能阻止 LocalDumps 收集。LocalDumps 与普通 WER 报告提交相互独立，本接口不会开启向 Microsoft 上传报告。

## 7. 验证范围要分开记录

“能正确配置”与“客户电脑确实产生文件”是两种验证。维护者回归只在独立测试进程内，把 HKLM 指向自己独占创建的临时 HKCU 测试树，使用真正的 Windows 注册表调用检查中文、冲突、清理、权限、视图和失败时输出保留，不配置实际业务程序。静态构建测试还注入写入／回滚失败；非 Windows 和关闭模块时验证 `NOT_SUPPORTED`。这些回归不等于客户电脑已经通过真实崩溃落盘验收。

发布前，还要在目标 Windows 版本、程序架构、运行账号和目录权限下，用可丢弃的测试程序以及明确授权的系统配置，单独验证真实崩溃收集。测试文件保持私有，只撤销本次测试创建的设置。不要拿带有未保存工作的生产程序故意制造崩溃。

参考：[Microsoft LocalDumps 配置说明](https://learn.microsoft.com/en-us/windows/win32/wer/collecting-user-mode-dumps)、[注册表视图访问说明](https://learn.microsoft.com/en-us/windows/win32/winprog64/accessing-an-alternate-registry-view)。
