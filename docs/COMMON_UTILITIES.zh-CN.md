# 常用工具与简洁调用

[English](COMMON_UTILITIES.md) · [返回首页](../README.zh-CN.md)

在日常开发中，像去空白与字符串切分、128 位精确乘除换算（`a * b / c`）、按名称注册并调用模块内过程（Procedure）、加载多语言翻译词条、生成安全的时间戳文件名以及单层目录遍历等高频任务，都可以通过本库提供的单函数直接入口完成，而把高级内存预算与扩展控制留在 `_ex` 重载中。C99 核心接口与可选的 C++11 封装共用同一份底层实现，不引入任何额外第三方依赖。

完整可运行的综合示例位于 [common_tasks.c](../examples/common_tasks.c)：程序先对字符串执行原地 ASCII 去空白与逗号切分（`hx_string_trim_ascii_in_place` + `hx_string_split`），接着用 `hx_os_muldiv_u64` 将 `1001 / 30000` 秒帧率精确换算为纳秒帧间隔，随后注册并按名字调用 `twice` 过程读取返回参数，最后用 `hx_os_repeat` 在当前线程执行两次周期回调并释放全部资源。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 `-DHX_OS_BUILD_EXAMPLES=ON` 并构建 `hx_os_common_tasks` 目标即可直接运行（无需命令行参数或输入文件）。在默认全模块配置下，预期输出包含：

```text
pieces: 3
frame interval: 33366667 ns
procedure result: 42
```

（以及两次周期 `tick` 输出。）你也可以参照[最小工程配置](USAGE.zh-CN.md#c-project)将该示例源码作为自己工程的 `main.c`。

## 从要完成的动作选择入口

| 常见开发任务 | C99 接口 | C++11 接口 |
| --- | --- | --- |
| 原地 ASCII 去空白／大小写转换、替换、分割、拼接 | `hx_string_trim_ascii_in_place` / `lower_ascii_in_place` / `replace_all` / `split` / `join` | `hx::String` 原地方法；`hx::Strings` 返回新副本 |
| 按指定编码读取文本文件并转为 UTF-8 | `hx_os_file_read_text_encoded` | `hx::Files::readText(path, encoding)` |
| 拼接应用配置文件路径 | `hx_os_path_config_file` | `hx::Paths::configFile` |
| 加载多语言翻译表并叠加覆盖词条 | `hx_os_text_lookup_load` / `add_file` / `add_text` | `hx::TextLookup::load` / `addFile` / `addText` |
| 128 位中间精度计算 `a × b ÷ c` | `hx_os_muldiv_u64` | `hx::Wide::multiplyDivide` |
| 按名称注册并同步调用过程 | `hx_os_proc_register` / `hx_os_proc_call` | `hx::ProcedureHandler::add` / `call` |
| 生成跨平台合法的 UTC／本地时间文件名 | `hx_os_filename_generate` / `format_string` | `hx::Calendar::filename` / `localFilename` |
| 睡眠至截止时间前再短暂自旋等待 | `hx_os_sleep_until_precise` / `_ex` | `hx::Wait::precise` |
| 在当前线程同步执行有限次周期动作 | `hx_os_repeat` / `hx_os_repeat_until` | `hx::Wait::repeat` |
| 词法分词并获取位置或转义解码后的文本 | `hx_os_lexer_next` / `hx_os_token_string` | `hx::Lexer` / `hx::Token::value` |
| 声明类型化 POD 容器包装 | `HX_OS_DECLARE_VECTOR` / `HX_OS_DECLARE_QUEUE` | 原有 C 容器接口，或标准 C++ 容器 |
| 一步关闭并释放线程池 | `hx_os_thread_pool_shutdown` / `_ex` | `hx::ThreadPool::shutdownUntil` |
| 按指定线程数和队列容量创建线程池 | 既有 `hx_os_thread_pool_options` | `hx::ThreadPool::withThreads` |

配套可编译示例：[direct_calls.cpp](../examples/direct_calls.cpp)、[settings.c](../examples/settings.c)、[common_tasks.c](../examples/common_tasks.c)、[thread_pool.cpp](../examples/thread_pool.cpp)。

## 字符串与编码

在循环中反复组装文本时，建议复用同一个 `hx_string` 实例：调用 `hx_string_clear()` 只清空内容而保留已分配容量，直到最后调用 `hx_string_destroy()` 才真正释放堆缓冲区；如果提前知道大致长度，可以先调用一次 `hx_string_reserve()`。
- 当已知字节长度时，优先使用 `hx_string_view` 配合 `hx_string_append()`；若只有以 `NUL` 结尾的 C 字符串，则直接调用 `hx_string_append_cstr()`。
- 两种追加方式都会校验严格 UTF-8 编码、在失败时保持原字符串不变，并且允许传入借用自目标 `hx_string` 自身的切片（即便触发扩容也能安全追加）。注意 `hx_string_view` 允许包含内嵌 `NUL` 字节，打印此类数据时请按 `view.size` 使用 `fwrite()` 或 `%.*s`，不要直接用 `puts()`。

```c
hx_string text = HX_STRING_INIT;
hx_string_parts *parts = NULL;

hx_os_status status = hx_string_assign_cstr(&text, " one,two,中文 ", NULL);
if (status == HX_OS_OK)
	hx_string_trim_ascii_in_place(&text);

if (status == HX_OS_OK)
	status = hx_string_split(&text, hx_string_view_from_cstr(","),
		&parts, NULL);

// Check status before inspecting borrowed parts_at views.
hx_string_parts_destroy(&parts);
hx_string_destroy(&text);
```

核心字符串与文件编码规则：
- **ASCII 原地处理与替换**：`trim`、`lower` 和 `upper` 仅处理 ASCII 字符，不触碰多字节 UTF-8 码点（不做 Unicode 归一化或完整大小写折叠）。带 `_in_place` 后缀的 C 函数要求传入非 `NULL` 的已初始化字符串，返回 `void` 且零内存分配（C++ `String::trimAscii` / `lowerAscii` / `upperAscii` / `clear` 返回当前对象引用以支持链式调用）。`hx_string_replace_all` 从左到右执行非重叠替换（拒绝空查找串，失败时保持原值不变）。
- **字符串分割（`hx_string_split`）**：`hx_string_parts` 内部独立持有一份输入文本副本，因此即使原 `hx_string` 已经销毁，`hx_string_parts_at` 返回的视图在调用 `hx_string_parts_destroy` 之前依然有效。默认保留空片段、最多切分 `4096` 项；`_ex` 版本允许在 `1..65536` 之间配置上限，超出上限时返回 `HX_OS_LIMIT` 而不会静默截断。
- **指定编码读取文件（`hx_os_file_read_text_encoded`）**：默认读取上限为 `16 MiB`，完整读入后严格转换为 UTF-8。当 `HX_OS_ENABLE_BOM=ON` 时会自动剥离匹配的字节序标记（BOM），若实际 BOM 与显式指定的编码冲突则返回 `HX_OS_INVALID_ENCODING`；当 `HX_OS_ENABLE_BOM=OFF` 时不做 BOM 剥离，保留解码后的 `U+FEFF`（当 UTF-16LE 的 BOM 后紧跟 `0x0000` 与 UTF-32LE 产生歧义时，以调用方显式指定的编码为准）。
- **类型化 POD 容器宏**：`HX_OS_DECLARE_VECTOR(name, type)` 和 `HX_OS_DECLARE_QUEUE(name, type)` 为已有有界容器生成类型安全的薄包装（`name` 使用小写下划线标识符）。仅适用于可按字节平凡复制（POD）的元素类型；像拥有堆内存的 `hx_string`、`hx_variant` 或非平凡 C++ 对象，请继续使用带 `copy`/`drop` 回调的标准容器接口。

## 借用文本与持有设置字符串

为了在高频读取配置、参数包或翻译词条时省去字符串深拷贝与堆分配，可以使用以下借用或引用计数视图接口：

| 读取接口 | 生命周期与内存开销 |
| --- | --- |
| `ConfigDocument::stringView()` | 零分配，直接借用不可变配置文档的内部字节（在文档对象销毁前有效） |
| `CallDataView::stringView()` | 零分配，借用参数包内部字节（在参数包被修改或销毁前有效） |
| `hx_os_text_lookup_get_view` / `TextLookup::getView()` | 零分配；命中时借用翻译表（在表被修改或销毁前有效），未命中时直接返回传入的 `fallback` 视图本身 |
| `hx_settings_get_string_ref` / `Settings::stringView()` | 零堆分配，通过原子引用计数持有一份不可变字符串快照；即使所属 `Settings` 随后更新了该键或已关闭，只要尚未释放 C 引用（或 C++ `SettingStringView` 尚未析构），文本就始终有效 |

> [!WARNING]
> 从 C++ `SettingStringView` 中提取出的普通 `hx::StringView` 只是借用了该引用持有者（Lease），不能比所属的 `SettingStringView` 活得更久。此外，`ConfigDocument` 和 `TextLookup` 的 C++ 借用方法在编译期拒绝从右值临时对象上借用视图，以防止悬空指针。

## 连续设置与直接目录遍历

- **批量连续更新设置与参数包**：
  - 对运行时设置（Settings），可使用 `hx_settings_update_begin`、一系列类型化 setter 和 `hx_settings_update_finish`（C++ 对应 `settings.update().setInt(...).setString(...).finish()`）。每个成功的 setter 都会立即通过底层实现发布并触发监听通知；一旦某一步失败，游标会锁定该错误并跳过后续 setter，此前已成功的修改保留（`finish` 汇报最终状态与已完成步数，不提供回滚事务或通知合并）。
  - 对固定参数包（CallData），同样提供 `hx_os_calldata_edit_*` 与 `CallData::edit()` 链式编辑游标。
- **单层目录同步遍历（`hx_os_directory_visit` / `Directory::forEach`）**：
  - `hx_os_directory_visit(path, callback, user, error)` 在当前调用线程内一次性完成目录打开、逐项回调与关闭（内部复用单个按需增长的缓冲区，每个文件名含结尾 `NUL` 最多 `4096` 字节，默认最多触发 `65,536` 次回调；`_ex` 版本可指定自定义 `context`、正整数上限与已完成次数输出）。
  - 回调收到的文件名是未排序的借用 UTF-8 视图，**仅在当前这次回调执行期间有效**。回调返回 `HX_OS_OK` 表示继续遍历，返回 `HX_OS_EOF` 表示主动正常停止（外层函数返回 `HX_OS_OK`），返回其他状态码则中止遍历并原样向上传播错误；若目录项数量超过上限则返回 `HX_OS_LIMIT`。所有退出路径都会自动关闭目录句柄并释放内部缓冲区。该接口不依赖 `HX_OS_ENABLE_FILE_UTILS` 或 `HX_OS_ENABLE_FILE_INFO`。
  - C++ `Directory::forEach(path, callback, maxEntries, context)` 返回 `hx::Result<size_t>`（值为已执行的回调次数）。传入的 `noexcept` lambda 或函数对象可返回 `void`（继续）、`bool`（`true` 继续 / `false` 正常停止）、`hx_os_status` 或 `hx::Result<void>`。
  - 如果需要递归搜索子目录并按通配符筛选文件，请使用 `Directory::findFirstFile` 或 `Directory::findFiles` / `hx_os_directory_find_files`，详见[递归查找](FILE_SEARCH.zh-CN.md)。

## 翻译文件

`hx_os_text_lookup`（C++11 对应 `<hx/TextLookup.hpp>` 中的 `hx::TextLookup`）用于加载扁平的 `key=value` 多语言词条文件，并支持用自定义翻译文件叠加覆盖部分词条：

```ini
# zh-CN.ini
hello="你好"
window.title="主窗口"
shortcut="第一行\n第二行"
url=https://example.invalid/
```

```cpp
#include <hx/TextLookup.hpp>

hx::Result<hx::String> window_title() noexcept
{
	auto language = hx::TextLookup::load("language.ini");
	if (!language)
		return hx::Failure(language.error());

	auto added = language.value().addFile("custom.ini");
	if (!added)
		return hx::Failure(added.error());

	return language.value().get("window.title");
}
```

语法与叠加规则：
- 支持 UTF-8 BOM、`CRLF`/`LF` 换行、`#` 行注释、带双引号的键或值（支持标准 JSON 转义序列与 UTF-16 代理对解码）。未加引号的值按原文读取并裁去首尾空格与 Tab（未加引号时遇到 `#` 即视为注释开始；双引号内部的 `#` 则作为普通字符保留）。不包含 `[section]` 节、文件 include 或变量展开。
- 后加载的同名键会覆盖先前的值；查询不存在的键时，默认回退返回键名本身。
- 默认上限为 `4096` 个词条、键值文本总计 `1 MiB`。执行叠加加载（`add_file` / `add_text`）时采用候选表事务替换：若遇到语法错误、文件读取失败、内存不足或超出容量上限，原有的翻译表内容保持完整不变。无修改时允许多个线程并发只读查询。

## 宽位换算与时序

```c
uint64_t interval_ns;

hx_os_status status = hx_os_muldiv_u64(1001, 1000000000, 30000,
	HX_OS_ROUND_NEAREST, &interval_ns, NULL);
// On HX_OS_OK: 33366667 ns.
```

- **128 位精确乘除（`hx_os_muldiv_u64` / `hx::Wide::multiplyDivide`）**：在内部保留完整的 128 位中间乘积 `a * b`，再除以 `c`，避免先乘后除的 64 位溢出或先除后乘的精度丢失。支持向下取整（`HX_OS_ROUND_DOWN`）、向上取整（`HX_OS_ROUND_UP`）和四舍五入（`HX_OS_ROUND_NEAREST`，恰好处于半值时向上取整）。除数为 `0` 返回 `HX_OS_INVALID`，商超出 `uint64_t` 范围返回 `HX_OS_RANGE`（失败时保持输出变量不变）。同时提供直接操作 `hx_os_uint128`（`low`/`high`）的底层算术函数，全程零堆分配、无锁且不依赖浮点数。
- **睡眠加尾端自旋等待（`hx_os_sleep_until_precise` / `hx::Wait::precise`）**：接收本库单调时钟的绝对截止时间，先执行常规系统睡眠，在距离截止时间最后 `200 us`（默认窗口；`_ex` 版本允许在 `0..10 ms` 之间调整，`0` 表示纯睡眠）时切换为 CPU 自旋，以减小操作系统调度器唤醒抖动。传入 `HX_OS_DEADLINE_NOW` 立即返回 `HX_OS_OK`，传入 `HX_OS_DEADLINE_INFINITE` 返回 `HX_OS_INVALID`。该接口不会修改进程全局定时器分辨率或线程优先级；在 Windows 上使用独立的高精度性能计数器（QPC）约束自旋时长，防止系统时钟粒度导致超时自旋（参见 [Windows 时钟说明](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-queryunbiasedinterrupttime)）。
- **当前线程有限周期执行（`hx_os_repeat` / `hx::Wait::repeat`）**：按给定的正整数毫秒间隔与最大次数上限，在当前调用线程上同步执行回调（首次回调在第一个间隔到达后触发；若某次回调耗时跨过了多个周期，错过的周期会自动合并而不会连续补发）。回调返回 `HX_OS_OK` 继续下一次，返回 `HX_OS_EOF` 正常提前结束，返回其他错误码则立即停止并向上传播。`_until` 版本可额外指定总截止时间并返回实际已启动的回调次数。如需后台异步定时器，请使用事件循环（Timer / Loop）模块。

## 命名过程与关闭

`hx_os_proc_handler`（C++11 对应 `<hx/Procedure.hpp>` 中的 `hx::ProcedureHandler`）允许模块按字符串名称注册过程回调，并由调用方按名称同步调用，实现模块间的解耦交互（注意这是进程内当前线程的同步调用，不是跨进程 RPC）：

```cpp
#include <hx/Procedure.hpp>

hx::Result<hx_os_proc_registration>
register_double(hx::ProcedureHandler &procedures) noexcept
{
	return procedures.add("double",
		[](hx::CallData &data) noexcept -> hx::Result<void> {
			auto value = data.view().integer("value");
			if (!value)
				return hx::Failure(value.error());
			if (value.value() > INT64_MAX / 2 ||
				value.value() < INT64_MIN / 2)
				return hx::Failure(HX_OS_RANGE);
			return data.setInt("answer", value.value() * 2);
		});
}
```

- **名称与参数语义**：过程名称必须是长度为 `1..127` 字节的严格 UTF-8 字符串，区分大小写且唯一（重复注册同名过程返回 `HX_OS_EXISTS`，调用未注册的名称返回 `HX_OS_NOT_FOUND`）。调用时参数包按值隔离，只有当回调成功返回 `HX_OS_OK` 时，对参数包的修改才会提交给调用方。
- **并发与容量上限**：回调在内部锁之外同步执行，支持多线程并发调用同一过程表（默认最多注册 `256` 个过程、同时进行 `64` 个并发调用、最大嵌套调用深度 `16` 层，均可通过 `options` 配置）。注册成功后，附带的 `cleanup` 回调归过程表接管，并在该过程的最后一个在途调用退出后、在锁外准确执行一次。
- **安全注销与关闭**：`unregister_until` 会先阻止新的调用进入该过程，然后等待正在执行的在途回调结束；若等待超时，注册 ID 仍然保留以便调用方重试（在回调内部尝试等待自身或外层祖先调用注销会立即检测到死锁并返回 `HX_OS_DEADLOCK`）。在卸载动态插件（DLL / `.so`）之前，宿主必须先完成所有相关过程的注销或关闭过程表，确保插件代码与借用数据不再被任何在途调用访问。

## 文件名与词法分析

- **安全的时间戳文件名生成**：
  - `hx_os_filename_generate` / `hx_os_filename_format_string`（C++ `hx::Calendar::filename` / `filenameNow`）使用 UTC 时间格式化文件名（默认格式 `%CCYY-%MM-%DD_%hh-%mm-%ss`）。
  - `hx_os_filename_generate_local` / `hx_os_filename_format_local_string`（C++ `hx::Calendar::localFilename` / `localFilenameNow`）使用系统本地时区时间。
  - 所有文件名生成函数都会自动修复 Windows 非法文件名字符、控制字符、末尾空格／点号以及保留设备名（如 `CON`、`NUL`），同时保留合法的 Unicode 字符。文件扩展名允许带一个可选的前导点，最多 32 个 ASCII 字母、数字、连字符或下划线，最终文件名长度上限为 240 个 UTF-8 字节（函数只负责生成文件名字符串，不会在磁盘上创建目录或检查重名文件）。
- **本地时间展示与时区偏移**：
  - 若用于界面展示而非作为文件名，请调用不做文件名字符替换的 `hx_os_wall_time_format_local_string`（C++ `hx::Calendar::local`），默认输出带明确时区偏移的 ISO 风格文本（如 `2024-03-10T03:00:00-04:00`）。
  - `hx_os_wall_time_local_offset_at` / `Calendar::localOffsetAt` 查询指定 Unix 毫秒时间点的本地时区偏移（返回单位为**秒**，并同时输出夏令时标志 `0`/`1`/`-1`；注意原有的“当前偏移”接口仍以**分钟**为单位）。

| 格式占位符 | 输出含义 |
| --- | --- |
| `%CCYY`、`%Y`；`%YY`、`%y` | 四位完整年份；年份后两位 |
| `%MM`、`%m`；`%DD`、`%d` | 两位月份（`01..12`）；两位月内日期（`01..31`） |
| `%hh`、`%H`；`%I`、`%p` | 24 小时制小时（`00..23`）；12 小时制小时（`01..12`）与固定英文 `AM`/`PM` |
| `%mm`、`%M`；`%ss`、`%S` | 两位分钟（`00..59`）；两位秒（`00..59`） |
| `%a`、`%A`；`%b`、`%B` | 固定英文星期缩写／全名；固定英文月份缩写／全名 |
| `%j`；`%u`、`%w` | 年内第几天（`001..366`）；星期几（`%u` 周一为 `1`..周日为 `7`；`%w` 周日为 `0`） |
| `%L`；`%s` | 三位毫秒（`000..999`）；向下取整的 Unix 秒数（与所选时区无关） |
| `%z`、`%:z` | 带符号时区偏移 `+HHMM` 或 `+HH:MM`（存在非整分偏移时自动附加秒） |
| `%Z` | 明确的数值时区标签 `UTC` 或 `UTC+HH:MM[:SS]`（不使用有歧义的英文时区缩写） |
| `%%` | 字面百分号 `%` |

未知格式占位符会返回 `HX_OS_SYNTAX` 并保持输出字符串不变。UTC 格式化支持公历 `0001..9999` 年；本地时区转换则遵循宿主 C 运行库支持的年份范围（例如 Windows `_localtime64_s` 保守支持 `1970..3000` 年，参见 [Microsoft 本地时间契约](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/localtime-s-localtime32-s-localtime64-s)）。

```cpp
// Include <hx/Calendar.hpp>; check both Result<String> values.
auto display = hx::Calendar::local(milliseconds);
auto name = hx::Calendar::localFilename(
	milliseconds, "dat", "%Y-%m-%d_%H-%M-%S_%L");
```

- **轻量词法分析器（`hx_os_lexer` / `hx::Lexer`）**：
  - 复制并持有严格 UTF-8 输入文本（接受开头 BOM，拒绝内嵌 `NUL`），识别标识符、数字（十进制、小数、科学计数法、十六进制）、双引号／单引号转义字符串、单字符标点与换行符，自动跳过空白与注释（块注释不嵌套）。
  - C 接口返回的 Token 视图借用自词法器内部，在 `hx_os_lexer_destroy` 前有效；C++ `hx::Token` 则独立持有解码后的字符串。到达末尾时返回结束标记（`HX_OS_LEXER_TOKEN_END`）。
  - 遇到词法错误后错误状态会保持，可通过 `position` 获取出错的起始行号与列号（均从 `1` 开始，列号按 Unicode 码点计数），调用 `reset` 可重新开始扫描。默认输入上限 `16 MiB`、单 Token 上限 `1 MiB`、最多 `1,048,576` 个 Token。

## 裁剪与成本

- **独立构建开关**：`HX_OS_ENABLE_WIDE_MATH`、`HX_OS_ENABLE_PRECISE_WAIT`、`HX_OS_ENABLE_LEXER` 和 `HX_OS_ENABLE_PROCEDURES` 默认均为 `ON`，可按需独立关闭。时间戳文件名依赖 `HX_OS_ENABLE_CALENDAR`；翻译表文本加载依赖 `HX_OS_ENABLE_TEXT_LOOKUP` 与 `HX_OS_ENABLE_LEXER`（从文件加载还需 `HX_OS_ENABLE_FILE_UTILS`）；开启 `HX_OS_ENABLE_SIGNALS` 或 `HX_OS_ENABLE_PROCEDURES` 任一开关即可使用固定参数包 `hx_os_calldata`。模块关闭后保留导出符号、清除对应能力位并返回 `HX_OS_NOT_SUPPORTED`。
- **内存与性能特征**：原地 ASCII 修改与 128 位宽整数乘除均为零堆内存分配；`replace_all` 和 `join` 使用临时字符串以保证失败时原值不变；`split` 为结果分配一份共享的输入文本副本。命名过程在完成注册后，调用路径不再分配上下文内存，但仍包含槽表查找、加锁与间接回调开销，仅用于需要按名动态解耦的边界，不应替代已知目标的普通 C 函数直接调用。

## 显式调试断点

`<hx_os_util.h>` 中的 `hx_os_debug_break()` 用于在当前调用线程上显式触发原生调试器断点（在 Release 构建下同样有效：Windows 调用 `DebugBreak()`，POSIX 发送 `SIGTRAP`）。
- 如果当前进程已附加调试器或安装了信号／异常处理器，断住后可以继续单步或恢复执行；若未附加调试器且无处理器，操作系统会终止当前进程。
- 库内部绝不会在普通错误路径上自动调用 `hx_os_debug_break()`，仅在业务代码明确希望中断到调试器时由开发者主动调用。
