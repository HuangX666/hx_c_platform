# 递归查找文件与后台查询

[English](FILE_SEARCH.md) · [首页](../README.zh-CN.md) · [文件接口](../include/hx_os_file.h)

- [1. 选择入口](#entry)
- [2. 直接取得第一个匹配文件](#first)
- [3. 找到一个就处理一个](#stream)
- [4. 在线程池中查询](#threads)
- [5. 深度、数量和内存限制](#limits)
- [6. 匹配和失败行为](#behavior)
- [7. 性能与验证范围](#evidence)

<a id="entry"></a>

## 1. 选择入口

本模块做一次实时扫描，从根目录向下访问子目录，使用文件名规则查找普通文件。不建立文件索引、不保存整个结果列表，也不自动启动线程。它适合偶尔查询、首次扫描以及收到一个结果就立即处理的工作。

| 需要什么 | C99 | C++11 |
| --- | --- | --- |
| 第一个匹配文件的路径 | hx_os_directory_find_first_file | Directory::findFirstFile |
| 对每个匹配文件执行回调 | hx_os_directory_find_files | Directory::findFiles |
| 设置限制、获取已启动回调次数 | hx_os_directory_find_files_ex | findFiles 的 options 参数和 Result<size_t> |
| 只列出当前一层名称 | hx_os_directory_visit | Directory::forEach |
| 按已知路径层级匹配 | hx_os_path_glob | 使用对应 C 入口 |

findFiles 与 forEach 都是函数，当前没有统一的 C foreach 宏。已有容器可以通过 begin/next/value 或 map_entry 遍历；循环宏本身不会减少磁盘读取或改变查找复杂度。

<a id="first"></a>

## 2. 直接取得第一个匹配文件

准备以下目录，可在更深的位置放置 config.json；内容不参与搜索：

```text
assets/
└── nested/
    └── config.json
```

C：保存为 main.c，采用[教程的 C 构建配置](USAGE.zh-CN.md#c-project)：

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

C++11：保存为 main.cpp，采用[关闭异常的 C++ 构建配置](USAGE.zh-CN.md#cpp-project)：

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

两者输出 `assets/nested/config.json`。库完成目录打开、逐层搜索、路径复制及句柄关闭。第一个匹配路径有多个候选时，返回原生遍历顺序中的第一个，不保证字母顺序。没有匹配返回 NOT_FOUND；失败保持 C 的输出字符串不变。C 输出使用字符串自己的 context；C++ 成功结果拥有路径，离开作用域时自动释放。

<a id="stream"></a>

## 3. 找到一个就处理一个

下面两个完整程序递归查找 assets 下所有匹配 *.json 的普通文件。目录中有多个匹配时，路径会逐个交给回调；路径顺序没有保证。

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

只有上述一个配置文件时，C 输出它的路径；C++ 还输出 `Files: 1`。回调中的 path 是借用视图，只在本次回调期间有效。要放进列表或发送给其他线程，先复制为自己的 hx_string/hx::String，不能保存这个视图供稍后使用。

C 回调返回 OK 继续，EOF 表示正常提前停止，其他状态表示失败并保留错误详情。C++ bool 回调返回 true 继续、false 正常停止，也支持 void、hx_os_status 和 Result<void>。所有回调同步运行在当前扫描线程，C++ 回调遵守 noexcept。

没有匹配时，遍历入口成功返回零次回调。超限或原生错误可能发生在已有回调之后；这些外部副作用不会回滚。C _ex 的 completed 和 C++ Result<size_t> 记录已启动回调次数，包含主动停止或失败的那个回调。C++ 出错后先检查 hasValue()，再读取进度。

<a id="threads"></a>

## 4. 在线程池中查询

界面程序从现有线程池提交一个查询任务，主线程继续处理事件；命令行工具可直接同步调用。默认一个任务顺序扫描目录，不为每个子目录建线程。并行扫描是否更快取决于磁盘、目录分布与文件系统，需要单独测量。

```mermaid
flowchart LR
    A["调用线程"] -->|"直接调用"| C["有界目录扫描"]
    A -->|"提交任务"| B["现有线程池"]
    B --> C
    C --> D["扫描线程中的结果回调"]
    D -.->|"由应用复制并投递"| E["界面线程"]
```

假设 pool 已经成功创建，下面片段放在返回 hx::Result<void> 的提交函数中。路径先复制再移交任务，因此提交函数返回后任务仍持有有效参数：

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

接下来检查 submitted：失败则传播 submitted.error()；成功后把 submitted.value() 移到长期存活的任务状态中，再从提交函数返回成功。界面事件循环可以检测 task.ready()，完成后 getUntil(HX_OS_DEADLINE_NOW) 取得结果。不要在按钮处理函数里马上阻塞等待。回调直接修改界面是不安全的；应用负责按自己的事件机制投递拥有独立存储的数据，并检查接收者生命周期。大量结果投递需要有界批量处理，避免每个文件都制造一条无界界面消息。

完整可运行示例：[C 同步查询](../examples/find_files.c)、[C++ 线程池查询](../examples/find_files_async.cpp)。示例从原生 UTF-8 参数快照读取根目录，在 Windows 不依赖窄字符 CRT argv；参数示例额外需要 SYSTEM_UTILS，后台示例再需要 THREAD_POOL。默认构建后在源码根目录运行：

```powershell
.\build\bin\Release\hx_os_find_files.exe assets "*.json"
.\build\bin\Release\hx_os_cpp_find_files.exe assets "*.json"
```

Linux 单配置构建：

```sh
./build/bin/hx_os_find_files assets '*.json'
./build/bin/hx_os_cpp_find_files assets '*.json'
```

后台示例是命令行演示，在主线程等待只是为了输出结果并退出，不能原样作为界面事件处理代码。它使用调用方选定的 2 秒等待和关闭预算，这不是搜索接口固定的时限。等待超时不代表搜索已停止，长期运行的程序须保留 task、owner 和回调数据，再次等待或重试关闭；库不会强杀或分离扫描线程。

当前扫描只在匹配回调中接受停止/错误返回，没有逐目录项取消令牌入口。取消尚未开始的线程池任务沿用线程池行为；已开始的扫描不会自动轮询 task.cancel()。需要协作停止时可在匹配回调检查状态，但一直没有匹配时不能保证及时响应。同步原生目录调用和用户回调也没有硬截止时间保证。

<a id="limits"></a>

## 5. 深度、数量和内存限制

默认值由 HX_OS_WALK_OPTIONS_INIT 提供：

| 项目 | 默认值 | 含义 |
| --- | ---: | --- |
| max_depth | 64 | 同时打开的目录层数，根目录算第 1 层；最多可设置 256 |
| max_entries | 1,000,000 | 处理的目录项数，包含目录和未匹配项 |
| max_results | 65,536 | 最多启动的匹配回调次数 |
| max_bytes | 16 MiB | 扫描栈、路径和名称缓冲的分配预算，包含元数据及扩容时的新旧缓冲 |

选项是正数限制，达到深度或超过处理/结果数量时返回 LIMIT，不会把截断结果报告为完整成功。恰好达到数量上限且后面没有更多对应项时仍可成功。原生目录对象、libc/系统目录缓冲、Windows 临时路径转换和回调自己的分配不计入 scratch 预算；原生目录数量由 max_depth 限制。输入根路径和拼接结果最多 32768 个 UTF-8 字节；文件名规则最多 4096 字节，操作系统还可能有更低限制。

例如需要 96 层目录时，保留有限上限并调整：

```c
hx_os_walk_options options = HX_OS_WALK_OPTIONS_INIT;
options.max_depth = 128;
options.max_entries = 2000000;
options.max_results = 200000;
options.max_bytes = 4 * 1024 * 1024;
```

将 &options 传给 C _ex 或 C++ findFiles/findFirstFile 的可选参数。Options、根路径、规则、回调、user 和自定义 context 在同步调用期间保持有效；后台任务应拥有或复制必要数据，并保证 context 存活。后台任务建议传绝对根路径；使用相对路径时，应用必须避免其他线程在扫描期间改变工作目录。

<a id="behavior"></a>

## 6. 匹配和失败行为

- 规则匹配文件名，不匹配完整相对路径。使用根目录 assets 加 *.json 即可递归，不能把 **/*.json 当文件名规则传入。
- `*` 匹配零到多个字符，`?` 匹配一个 Unicode 码点，所有平台均区分大小写。`[]`、路径分隔符和反斜杠转义不支持。
- 文件名以点开头时，规则也必须显式以点开头；隐藏目录仍会遍历。这里按名字处理前导点，没有把 Windows 隐藏属性映射成同一规则。
- 只返回普通文件。枚举时识别出的链接、重解析点和特殊文件会跳过，不主动递归进入这些子项；根目录按普通路径打开。
- 结果路径是传入根目录加子路径，根目录相对时结果也相对。它不是目录快照或安全沙箱；并发改名、删除或替换会影响结果，并可能在部分回调后失败。
- 权限、I/O、无效 UTF-8 和资源失败会明确返回，不静默忽略后宣称扫描完整。原生错误继续通过 hx_os_error/Result 保留。

搜索需要 FILE_UTILS；关闭时清除 HX_OS_CAP_FILE_SEARCH 并返回 NOT_SUPPORTED。CONTAINERS、CONFIG、SETTINGS 和线程池都不是同步搜索依赖。FILE_INFO 开启时复用目录项类型，未知类型才由底层补查；关闭时用普通名称枚举加不跟随链接的 stat 回退。

<a id="evidence"></a>

## 7. 性能与验证范围

扫描采用显式栈，复用名称和路径缓冲，正常模式下不对每个结果创建字符串或列表节点。不匹配的普通文件无需先拼接完整路径。读目录、UTF-8 检查、匹配和用户回调仍然需要时间；本模块没有文件索引，首次完整查找通常仍需访问相关目录项。

Windows/WSL 的控制测试覆盖 2,052 个匹配文件、96 层嵌套、Unicode、空结果、首次命中、停止/错误回调、限额、分配失败和资源回收；POSIX 另覆盖循环/外部链接及无效字节文件名。文件数量增长对照的查询 context 分配次数和峰值不随匹配数量增长；这不包含系统内部或回调另行分配。WSL 测试使用原生 /tmp，避免把 Windows 挂载层的名称转换当作 POSIX 字节文件名语义。

功能、后台线程身份、安装后调用、sanitizer、模块关闭及 Android 的实际结果见[验证记录](VALIDATION.zh-CN.md)。实时扫描、预先建立文件索引、通用 foreach 宏、红黑树和 STL 算法是不同能力；当前有序 map 使用 AVL，更多容器/算法并未因为这次文件搜索而自动加入。
