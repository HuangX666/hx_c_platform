# 递归查找文件与后台查询

[English](FILE_SEARCH.md) · [首页](../README.zh-CN.md) · [文件接口](../include/hx_os_file.h)

当应用需要按文件名通配规则（如 `*.json` 或 `config.json`）向下扫描目录树，并在找到匹配文件时立即处理（或直接取回第一个匹配路径）时，可以使用文件递归查找接口。该模块采用流式实时扫描设计，不维护常驻内存的文件索引，也不会一次性把所有结果堆积在列表里，适合启动期配置发现、资源目录扫描以及配合线程池执行的后台文件查询。

你可以先运行完整示例 [find_files.c](../examples/find_files.c) 来观察扫描效果。按[示例运行目录](../examples/README.zh-CN.md#run)说明构建 `hx_os_find_files` 并进入 `build` 目录（注意为通配模式加上引号，防止被 Shell 提前展开）：

```sh
# Linux / macOS：
./bin/hx_os_find_files ../examples "*.c"

# Windows PowerShell：
.\bin\Release\hx_os_find_files.exe ..\examples "*.c"
```

程序会逐行打印 `examples/` 目录下所有匹配的 `.c` 文件路径，并在末尾输出 `Files: <数量>`。在自己的工程中，只需将 [find_files.c](../examples/find_files.c) 里 `print_file` 回调的打印语句替换成实际业务处理逻辑即可。如果希望在扫描大目录时保持 GUI 界面响应，可以参阅第 4 节的线程池投递方式。

- [1. 选择入口](#entry)
- [2. 直接取得第一个匹配文件](#first)
- [3. 找到一个就处理一个](#stream)
- [4. 在线程池中查询](#threads)
- [5. 深度、数量和内存限制](#limits)
- [6. 匹配和失败行为](#behavior)
- [7. 性能与验证范围](#evidence)

<a id="entry"></a>

## 1. 选择入口

本模块从指定根目录开始向下遍历子目录，按文件名规则筛选普通文件。它不建立持久化索引、不缓存完整结果集，也不会在内部隐式启动后台线程。请根据具体场景选择对应的 C99 或 C++11 入口：

| 需要什么 | C99 入口 | C++11 入口 |
| --- | --- | --- |
| 获取第一个匹配文件的完整路径 | `hx_os_directory_find_first_file` | `hx::Directory::findFirstFile` |
| 对每个匹配文件逐一执行回调 | `hx_os_directory_find_files` | `hx::Directory::findFiles` |
| 自定义深度/数量限制并获取已触发的回调次数 | `hx_os_directory_find_files_ex` | 带 `options` 参数的 `findFiles`（返回 `hx::Result<size_t>`） |
| 只枚举当前单层目录中的直接子项 | `hx_os_directory_visit` | `hx::Directory::forEach` |
| 按固定路径层级展开通配符（如 `logs/*/*.log`） | `hx_os_path_glob` | 直接调用对应 C 接口 |

> [!NOTE]
> `findFiles` 与 `forEach` 均为基于回调的遍历函数，库不额外封装 C 语言的 `foreach` 语法宏。对于内存中的已有容器，可直接使用容器自身的 `begin` / `next` / `value` 或 `map_entry` 迭代接口。

<a id="first"></a>

## 2. 直接取得第一个匹配文件

如果只需要定位某个特定文件（例如在资源目录树中寻找 `config.json`），可以使用 `hx_os_directory_find_first_file` 或 `hx::Directory::findFirstFile`。

假设当前工作目录下有以下目录结构（文件内容不影响文件名匹配）：

```text
assets/
└── nested/
    └── config.json
```

**C99 示例**（保存为 `main.c`，编译配置可参考[教程的 C 构建配置](USAGE.zh-CN.md#c-project)）：

```c
#include <hx_os_file.h>
#include <hx_string.h>
#include <stdio.h>

int main(void)
{
	hx_string path = HX_STRING_INIT;

	hx_os_status status = hx_os_directory_find_first_file(
		"assets", "config.json", &path, NULL);
	if (status == HX_OS_OK)
		puts(hx_string_c_str(&path));
	else
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	hx_string_destroy(&path);

	return status == HX_OS_OK ? 0 : 1;
}
```

**C++11 示例**（保存为 `main.cpp`，编译配置可参考[关闭异常的 C++ 构建配置](USAGE.zh-CN.md#cpp-project)）：

```cpp
#include <hx/Files.hpp>
#include <cstdio>

int main()
{
	auto path = hx::Directory::findFirstFile("assets", "config.json");
	if (!path) {
		std::fprintf(stderr, "%s\n", hx_os_status_name(path.status()));

		return 1;
	}

	std::puts(path.value().c_str());
}
```

两段程序均输出 `assets/nested/config.json`。库在内部自动完成目录打开、逐层搜索、命中路径复制以及各级目录句柄的关闭清理：

- **遍历顺序**：当目录树中存在多个同名候选文件时，返回底层文件系统原生遍历顺序遇到的第一个匹配项，不保证按字典序排列。
- **未命中与错误处理**：若整个目录树中都没有匹配文件，函数返回 `HX_OS_NOT_FOUND`。查询失败时，C 接口传入的 `path` 字符串保持原状不变。
- **内存所有权**：C 接口使用 `path` 自身绑定的 `hx_context` 分配缓冲区，用完后需调用 `hx_string_destroy(&path)`；C++ 接口返回的 `hx::Result<hx::String>` 直接持有路径字符串，离开作用域时自动释放。

<a id="stream"></a>

## 3. 找到一个就处理一个

当需要处理所有匹配文件时，使用 `hx_os_directory_find_files` 或 `hx::Directory::findFiles`。每找到一个匹配文件，库就会在当前扫描线程上同步调用一次你提供的回调函数。

下面两个完整程序递归查找 `assets` 下所有匹配 `*.json` 的普通文件：

```c
#include <hx_os_file.h>
#include <stdio.h>

static hx_os_status HX_OS_CALL show_path(
	hx_string_view path, void *user, hx_os_error *error)
{
	(void)user;
	(void)error;
	printf("%.*s\n", (int)path.size, path.data);

	return HX_OS_OK;
}

int main(void)
{
	hx_os_status status = hx_os_directory_find_files(
		"assets", "*.json", show_path, NULL, NULL);
	if (status != HX_OS_OK)
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	return status == HX_OS_OK ? 0 : 1;
}
```

```cpp
#include <hx/Files.hpp>
#include <cstdio>

int main()
{
	auto found = hx::Directory::findFiles("assets", "*.json",
		[](hx::StringView path) noexcept {
			std::printf("%.*s\n", static_cast<int>(path.size()),
				path.data());
		});

	if (!found) {
		std::fprintf(stderr, "%s\n", hx_os_status_name(found.status()));

		return 1;
	}

	std::printf("Files: %zu\n", found.value());
}
```

对于上一节准备的单文件目录，C 程序会打印该文件路径，C++ 程序还会额外打印 `Files: 1`。

> [!IMPORTANT]
> **回调路径生命周期与返回值约定**
> - **借用视图（Borrowed View）**：回调参数 `path`（`hx_string_view` / `hx::StringView`）指向扫描器内部复用的临时路径缓冲区，**仅在当次回调执行期间有效**。如果需要将路径存入容器或跨线程投递，必须在回调内将其复制为独立的 `hx_string` 或 `hx::String`。
> - **控制继续或提前停止**：
>   - **C 回调**：返回 `HX_OS_OK` 继续扫描；返回 `HX_OS_EOF` 表示主动提前结束扫描且整体调用视作成功（返回 `HX_OS_OK`）；返回其他状态码则立即中止扫描并将该错误状态及 `error` 详情向上返回。
>   - **C++ 回调**：支持返回 `void`、`bool`（`true` 继续、`false` 正常提前停止）、`hx_os_status` 或 `hx::Result<void>`。所有 C++ 回调必须满足 `noexcept`。
> - **部分完成进度**：如果没有任何文件匹配，遍历函数会成功返回 `HX_OS_OK` 且回调次数为 `0`。若扫描中途因触达配额上限或遇到底层 I/O 错误而中止，此前已经执行过的回调副作用不会自动回滚。C 接口 `hx_os_directory_find_files_ex` 的 `completed` 输出参数与 C++ 的 `hx::Result<size_t>` 会记录**已启动的回调总次数**（包含主动停止或返回错误的那一次回调）；在 C++ 中即使结果失败，也可先检查 `found.hasValue()` 再读取已完成次数。

<a id="threads"></a>

## 4. 在线程池中查询

命令行工具通常可以直接在当前线程同步扫描；而桌面 GUI 或交互式服务可以将扫描任务提交到现有的线程池中执行，让主线程继续响应界面事件。默认情况下，单次查询任务在单个工作线程内按顺序扫描目录树，不会为每个子目录额外派生线程（多线程并发扫盘是否更快取决于底层存储介质、目录扇出结构与文件系统锁开销，需结合实际硬件基准测试评估）。

```mermaid
flowchart LR
    A["调用线程"] -->|"直接调用"| C["有界目录扫描"]
    A -->|"提交任务"| B["现有线程池"]
    B --> C
    C --> D["扫描线程中的结果回调"]
    D -.->|"由应用复制并投递"| E["界面线程"]
```

假设线程池 `pool` 已经创建完毕，下面的代码片段展示如何在返回 `hx::Result<void>` 的提交函数中发起异步查找。注意我们先将根目录路径复制为独立的 `hx::String` 再移动进任务闭包，确保提交函数返回后工作线程仍持有合法的路径内存：

```cpp
auto root = hx::String::copy("assets");
if (!root)
	return hx::Failure(root.error());

auto submitted = pool.submit(
	[](hx::String folder) noexcept {
		return hx::Directory::findFirstFile(
			folder.c_str(), "config.json");
	}, std::move(root).value());
```

提交后先检查 `submitted`：若提交失败则直接向上传播 `submitted.error()`；若成功则将 `submitted.value()`（任务句柄）保存到界面控制器或长生命周期状态对象中。界面事件循环可以定期检查 `task.ready()`，待任务就绪后调用 `task.getUntil(HX_OS_DEADLINE_NOW)` 无阻塞地取回结果，切勿在按钮点击回调里直接阻塞等待。

> [!WARNING]
> **跨线程 UI 更新与生命周期边界**
> - **切勿在扫描回调中直接操作 GUI 控件**：扫描回调运行在线程池工作线程上。应用应先将路径复制为独立拥有的字符串，再通过 GUI 框架的异步事件队列投递给界面线程，并在界面线程消费时检查目标窗口或接收者是否仍然存活。当匹配结果较多时，建议在工作线程内分批聚合后再投递，避免单次扫描制造海量界面消息把事件队列撑满。
> - **等待超时不等于扫描已停止**：完整可运行示例见 [C 同步查询](../examples/find_files.c) 和 [C++ 线程池查询](../examples/find_files_async.cpp)（这两个示例使用原生 UTF-8 参数快照读取命令行，不依赖 Windows 窄字符 CRT `argv`，因此额外启用了 `HX_OS_ENABLE_SYSTEM_UTILS`，后台示例还需启用 `HX_OS_ENABLE_THREAD_POOL`）。示例中的 2 秒等待和关闭时间只是演示代码自行选择的超时参数，并非搜索 API 的固定时限。一旦等待超时，底层扫描线程可能仍在运行——长期运行的程序必须继续保留 `task`、线程池 `owner` 以及回调所引用的数据，稍后再次等待或重试关闭；本库绝不会强行杀死或分离正在执行扫描的线程。
> - **协作式停止**：当前扫描器仅在**命中匹配文件并触发回调时**根据回调返回值停止扫描，未在每个目录项枚举处轮询取消令牌。尚未开始执行的排队任务可按线程池规则取消；已经开始运行的扫描不会自动轮询 `task.cancel()`，若目录树中一直没有命中文件，则无法在半途提前中断。

构建默认示例后，可在仓库根目录直接运行：

```powershell
# Windows PowerShell：
.\build\bin\Release\hx_os_find_files.exe assets "*.json"
.\build\bin\Release\hx_os_cpp_find_files.exe assets "*.json"
```

```sh
# Linux / macOS 单配置构建：
./build/bin/hx_os_find_files assets '*.json'
./build/bin/hx_os_cpp_find_files assets '*.json'
```

<a id="limits"></a>

## 5. 深度、数量和内存限制

为防止深层嵌套目录或超大目录树耗尽内存，扫描器内置了有界配额控制。默认配额由 `HX_OS_WALK_OPTIONS_INIT` 初始化：

| 字段 | 默认值 | 含义 |
| --- | ---: | --- |
| `max_depth` | `64` | 最多同时打开的目录层级数（根目录计为第 1 层，最大允许设为 `256`） |
| `max_entries` | `1,000,000` | 最多处理的目录项总数（包含子目录本身和未匹配的文件项） |
| `max_results` | `65,536` | 最多触发的匹配回调次数 |
| `max_bytes` | `16 MiB` | 显式扫描栈、路径与名称缓冲区的总内存预算（含元数据及扩容过渡期的新旧缓冲区） |

所有配额字段均须为正数。当目录深度超过 `max_depth`，或实际需要处理的目录项/匹配数超出上限时，扫描会明确返回 `HX_OS_LIMIT`，绝不会将截断后的不完整结果伪装成正常扫描完成（如果刚好在处理完最后一个目录项或最后一个匹配文件时达到计数上限，且后续已无更多对应项，则依然返回 `HX_OS_OK`）。

如果需要扫描更深或更大的目录树（例如需要支持 96 层嵌套目录），可以在保留有界上限的前提下按需调整 `hx_os_walk_options`：

```c
hx_os_walk_options options = HX_OS_WALK_OPTIONS_INIT;

options.max_depth = 128;
options.max_entries = 2000000;
options.max_results = 200000;
options.max_bytes = 4 * 1024 * 1024;
```

将 `&options` 传给 C 接口 `hx_os_directory_find_files_ex` 或 C++ `findFiles` / `findFirstFile` 的可选参数即可。

> [!NOTE]
> **预算统计范围与路径长度上限**
> - `max_bytes` 统计的是扫描器通过查询 `hx_context` 分配的显式栈和路径缓冲区；操作系统底层的原生目录句柄（`DIR*` / `HANDLE`）、libc 内部目录缓冲、Windows 宽字符路径临时转换以及用户回调自己申请的内存不计入该预算（同时打开的原生目录句柄数量已由 `max_depth` 严格限制）。
> - 输入根路径及拼接后的完整文件路径最长支持 `32,768` 个 UTF-8 字节，文件名匹配模式最长支持 `4,096` 字节（具体操作系统可能施加更低的路径长度限制）。
> - 在同步调用期间，`options`、根路径、通配模式、回调函数、`user` 指针及自定义 `hx_context` 必须保持有效；若在后台线程中运行，任务必须独立持有或复制这些数据，并推荐使用**绝对路径**作为根目录（若使用相对路径，应用需确保扫描期间其他线程不会修改进程当前工作目录）。

<a id="behavior"></a>

## 6. 匹配和失败行为

- **仅匹配文件名（Basename）**：模式串只匹配不含目录前缀的文件名本身。传入根目录 `"assets"` 和模式 `"*.json"` 就会自动递归搜索所有子目录，**不要**传入 `"**/*.json"` 这类包含路径分隔符的模式。
- **通配符语法与大小写**：`*` 匹配零个或多个字符，`?` 匹配单个 Unicode 码点；不支持字符集 `[...]`、路径分隔符或反斜杠转义。为保证跨平台行为一致，**所有平台上均严格区分大小写**。
- **隐藏文件（前导点 `.`）**：如果文件名以 `.` 开头（如 `.gitignore`），模式串也必须显式以 `.` 开头才能匹配（如 `.*` 或 `.gitignore`）。以 `.` 开头的隐藏子目录仍会被正常递归进入。该规则仅按文件名前导字符判定，不会将 Windows 文件系统的 `FILE_ATTRIBUTE_HIDDEN` 属性映射为前导点规则。
- **仅返回普通文件，不跟随符号链接**：扫描结果只包含普通文件（Regular Files）。遍历过程中识别出的符号链接（Symlinks）、Windows 重解析点（Reparse Points / Junctions）以及设备/管道等特殊文件都会被直接跳过，不会递归进入链接指向的子目录（传入的起始根目录本身按普通路径打开）。
- **路径拼接与并发修改**：回调收到的路径由“传入的根目录 + 相对子路径”拼接而成（传入相对根目录即得到相对路径）。实时扫描不是文件系统快照或安全沙箱；若扫描期间其他进程并发重命名、删除或替换目录项，可能影响本次结果甚至在中途引发错误返回。
- **显式报告错误**：遇到权限不足（`HX_OS_ACCESS_DENIED`）、I/O 故障、非法 UTF-8 字节序列或资源耗尽时，扫描会立即停止并明确返回对应状态码与原生错误详情（通过 `hx_os_error` 或 `hx::Result`），不会静默吞掉错误后谎称全盘扫描完毕。

**CMake 编译开关关系**：递归查找功能由 `HX_OS_ENABLE_FILE_UTILS` 控制（开启时提供 `HX_OS_CAP_FILE_SEARCH` 能力位；关闭时调用查找接口返回 `HX_OS_NOT_SUPPORTED`）。同步文件查找不依赖 `HX_OS_ENABLE_CONTAINERS`、`HX_OS_ENABLE_CONFIG`、`HX_OS_ENABLE_SETTINGS` 或线程池模块。当 `HX_OS_ENABLE_FILE_INFO` 开启时，扫描器会优先复用操作系统目录项自带的文件类型标记，仅在文件类型未知时按需补充查询；若关闭 `HX_OS_ENABLE_FILE_INFO`，则会回退为名称枚举加不跟随链接的 `lstat`/属性查询。

<a id="evidence"></a>

## 7. 性能与验证范围

扫描器内部采用显式堆叠栈（Explicit Stack）代替函数递归，并在整个扫描过程中原地复用路径与文件名缓冲区。在常规扫描模式下，不会为每个命中的文件分配独立的字符串对象或链表节点，对于未命中的普通文件也不会浪费时间去拼接完整路径。当然，底层系统的目录读取、UTF-8 合法性校验、通配符匹配以及用户回调本身仍需消耗时间；由于不维护持久化文件索引，首次全量查询依然需要按需访问目录树中的各个节点。

在测试验证方面，Windows 与 WSL 环境下的回归测试覆盖了 2,052 个匹配文件、96 层深层嵌套目录、Unicode 文件名、空结果集、首个匹配提前返回、回调主动停止/报错、配额超限拦截、内存分配失败注入以及资源完整回收；POSIX 环境额外覆盖了循环符号链接、指向外部的符号链接以及含非法 UTF-8 字节序列的文件名（WSL 测试在原生 `/tmp` 文件系统上执行，以确保测试的是真实的 POSIX 字节文件名语义，而非 Windows 挂载层的路径转换行为）。在固定目录深度、仅增加匹配文件数量的对照测试中，查询上下文（`hx_context`）的分配次数与内存峰值保持恒定，不随命中文件数量线性增长。

如果你需要对收集到内存中的结果做进一步排序、二分查找或放入有序集合，请结合使用[通用算法](ALGORITHMS.zh-CN.md)与[容器](CONTAINERS.zh-CN.md)；各目标操作系统的编译与运行要求详见[平台支持](PLATFORMS.zh-CN.md)。
