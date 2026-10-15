# 常用工具与简洁调用

[English](COMMON_UTILITIES.md) · [返回首页](../README.zh-CN.md)

**先运行一个完整例子。** 需要拆分文字、按名字调用一段处理逻辑，或精确换算时间单位时，先用这个小程序观察每个结果。

打开 [common_tasks.c](../examples/common_tasks.c)。trim／split → muldiv_u64 换算帧间隔 → 注册并调用 twice → 读取参数包 output → repeat 执行两次 tick → 释放。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 examples，并只构建 `hx_os_common_tasks`。此例无命令行参数、无需输入文件。

**会得到什么：**完整模块配置下会看到 `pieces: 3`、`frame interval: 33366667 ns`、`procedure result: 42` 和两次 tick。命名过程在当前线程调用，不是远程 RPC。

下面的短片段用于解释接口；复制运行时以完整示例为准。也可按[最小工程配置](USAGE.zh-CN.md#c-project)把完整源码作为自己的 main。

这批接口沿用 OBS 的思路：常见动作由一个入口完成，复杂控制留在扩展入口。实现独立于 OBS，没有复制 OBS 源码或增加第三方依赖。C99 与 C++11 使用同一份平台实现；旧公共结构体布局和既有函数保持不变。

## 从要完成的动作选择入口

| 动作 | C99 | C++11 |
| --- | --- | --- |
| 原地 ASCII 处理、替换、分割、拼接 | hx_string_trim_ascii_in_place / lower_ascii_in_place / replace_all / split / join | hx::String 原地处理；hx::Strings 副本 |
| 读取指定编码并转为 UTF-8 | hx_os_file_read_text_encoded | hx::Files::readText(path, encoding) |
| 得到应用配置文件路径 | hx_os_path_config_file | hx::Paths::configFile |
| 加载翻译并覆盖部分词条 | hx_os_text_lookup_load / add_file / add_text | hx::TextLookup::load / addFile / addText |
| 精确计算 a×b÷c | hx_os_muldiv_u64 | hx::Wide::multiplyDivide |
| 按名称调用一个过程 | hx_os_proc_register / call | hx::ProcedureHandler::add / call |
| 生成合法的时间文件名 | hx_os_filename_generate / format_string | hx::Calendar::filename |
| 显式选择睡眠加短暂自旋 | hx_os_sleep_until_precise / _ex | hx::Wait::precise |
| 在当前线程执行有限次周期动作 | hx_os_repeat / repeat_until | hx::Wait::repeat |
| 分词并取得位置或解码后的字符串 | hx_os_lexer_next / hx_os_token_string | hx::Lexer / hx::Token::value |
| 类型化 POD 容器 | HX_OS_DECLARE_VECTOR / HX_OS_DECLARE_QUEUE | 原有 C 容器接口，或标准 C++ 容器 |
| 关闭并释放线程池 | hx_os_thread_pool_shutdown / _ex | hx::ThreadPool::shutdownUntil |
| 指定线程数和队列容量 | 既有 pool options | hx::ThreadPool::withThreads |

可编译示例：[direct_calls.cpp](../examples/direct_calls.cpp)、[settings.c](../examples/settings.c)、[common_tasks.c](../examples/common_tasks.c)、[thread_pool.cpp](../examples/thread_pool.cpp)。

## 字符串与编码

~~~c
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
~~~

字符串以 UTF-8 为默认编码，长度按字节计算，允许内嵌 NUL。 trim/lower/upper 只处理 ASCII，不做 Unicode 归一化或完整大小写折叠。 replace_all 从左到右非重叠替换，允许输入视图借用目标字符串；空查找串无效。替换、拼接失败保持原值，使用目标字符串自己的 context。

ASCII 的 `_in_place` 入口要求非 NULL、已初始化的字符串，返回 void，不分配内存。既有返回状态码的去空白／大小写接口共用实现并保留参数检查。C++ `String::trimAscii/lowerAscii/upperAscii/clear` 返回同一对象的引用，assign/append/reserve 继续报告 Result。

split 的结果拥有输入副本；原字符串销毁后，结果视图仍有效，直到 parts_destroy。默认保留空片段、最多 4096 项；_ex 可设 1..65536，超限返回 LIMIT，不截断。join 支持借用目标的片段。对象并发修改需外部同步。

指定编码的文件读取默认最多 16 MiB，完整读取后严格转为 UTF-8。 BOM 模块开启时去掉匹配的 BOM，编码与 BOM 冲突报 INVALID_ENCODING； BOM 模块关闭时不做 BOM 识别，转换器保留解码后的 U+FEFF。 UTF-16LE BOM 后紧跟 NUL 与 UTF-32LE 标记存在歧义时，以显式编码为准。不进行编码猜测。旧 UTF-8 读取接口及可选 libiconv 适配保持原行为。

POD 宏仅包裹已有有界容器，不引入另一套实现。宏参数 name 应为小写下划线名称。元素按字节复制，不能用于拥有资源的 hx_string 或非平凡 C++ 对象；这类元素继续使用已有 copy/drop 回调。元素对齐来自 C99 offsetof 探测，不使用 pack 或未对齐访问。视图在容器修改后失效。

## 借用文本与持有设置字符串

| getter | 生命周期与开销 |
| --- | --- |
| `ConfigDocument::stringView()` | 借用不可变文档字节，保持文档有效 |
| `CallDataView::stringView()` | 借用参数包，修改或销毁后失效 |
| `hx_os_text_lookup_get_view`／`TextLookup::getView()` | 命中时借用表，修改或销毁后失效；缺失时借用输入 fallback 本身 |
| `hx_settings_get_string_ref`／`Settings::stringView()` | 持有不可变值引用，设置更新或关闭后仍有效；释放 C 引用或让 `SettingStringView` 析构 |

这四种读取均不复制字符串字节。普通借用 getter 不分配内存。设置字符串引用的取得与复制不分配，但会保留／释放原子引用；从可变设置读取时仍使用既有根锁。从 `SettingStringView` 取出的普通 `StringView` 借用该持有对象，不能晚于它销毁。需要独立可变字符串时继续使用原有复制 getter。文档与翻译表的 C++ 借用 getter 拒绝持有者临时对象。

## 连续设置与直接目录遍历

需要进入子目录时，使用 Directory::findFirstFile 取得一个拥有的路径，或 Directory::findFiles / hx_os_directory_find_files 流式返回匹配文件。这些入口同步执行；界面程序使用现有线程池，再把复制后的结果投递到界面线程。限制、原生类型复用、错误/所有权契约和完整后台示例见[递归查找](FILE_SEARCH.zh-CN.md)。下方原有 forEach/visit 仍只遍历一层。

使用 `hx_settings_update_begin`、类型化 setter 和一次 `hx_settings_update_finish`，或 `settings.update().setInt(...).setString(...).finish()`。每次成功都通过既有实现发布和通知。首次失败固定保留，后续项跳过，此前修改保留。finish 只检查游标，失败时也返回成功操作次数。它不提供事务回滚，也不合并通知。设置、分配器和用户状态须覆盖 setter 生命周期；`unset` 只修改显式层，游标不额外串行化既有设置操作。

参数包提供对应的 `hx_os_calldata_edit_*` 与 `CallData::edit()`。后续失败时，之前成功的 setter 仍生效。游标借用参数包，调用方排除并发修改，并在调用消费者前检查 finish。单独使用既有 setter 时，仍分别返回自己的 Result／状态码。

`hx_os_directory_visit(path, callback, user, error)` 在当前线程完成单目录打开、遍历和关闭。默认上限为启动 65536 次回调；`_ex` 接收 context、正数上限和完成次数输出。每个名称含 NUL 最多 4096 字节，内部复用一个按需增长的缓冲区。容量足够后包装层不增加逐项缓冲分配，原生适配自身可能分配。名称是未排序的借用 UTF-8 视图，仅回调期间有效；不递归、不查询类型、不创建目录快照。

回调收到已清空的非 NULL 错误记录。OK 继续，EOF 正常停止，其他状态保留回调／原生错误详情。自然结束返回成功，超过数量上限返回 LIMIT；完成次数包括主动停止或失败的回调。恰好达到上限且原生已结束时成功。回调和原生目录调用不能被抢占，文件系统并发修改沿用原生行为，重入使用独立状态。所有返回路径均关闭目录、释放缓冲。关闭 FILE_UTILS 和 FILE_INFO 后仍可使用。

C++ `Directory::forEach(path, callback, maxEntries, context)` 返回 `Result<size_t>`，失败时也带已执行数量。同步回调借用捕获，不分配回调状态。`void` 继续，`bool` 为 true 继续／false 正常停止，状态码／`Result<void>` 明确报告结果。lambda 和函数对象声明 `noexcept`，C++11 函数指针／引用遵守同一契约。需要每项独立持有文本或类型查询时，使用 `Directory::next()`。

## 翻译文件

~~~ini
# zh-CN.ini
hello="你好"
window.title="主窗口"
shortcut="第一行\n第二行"
url=https://example.invalid/
~~~

~~~cpp
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
~~~

采用扁平 key=value 目录，接受 UTF-8 BOM、CRLF/LF、# 注释、带引号的键值、 JSON 转义及代理对。未加引号的值按原文读取并裁掉两端空格/Tab，其中 # 开始注释；引号内的 # 保留。数字形状的文本和单词中的撇号都是文本。不支持节、包含文件或变量展开；不是修改已有 INI 配置解析器的语义。

同名键以后出现的值覆盖先前值；缺失键返回键本身。默认 4096 项、键值文本合计 1 MiB；输入文件也受表的文本预算限制。加载使用候选表，语法、读取、分配或容量失败均保持原表。暂时持有旧表、新表和输入，元数据另计。字符串转义复用词法模块。并发只读可以共享，修改/加载/销毁由宿主排除并发；没有全局语言或 locale。

## 宽位换算与时序

~~~c
uint64_t interval_ns;

hx_os_status status = hx_os_muldiv_u64(1001, 1000000000, 30000,
	HX_OS_ROUND_NEAREST, &interval_ns, NULL);
// On success: 33366667 ns.
~~~

内部保留完整 128 位乘积，支持向下、向上、最近值舍入，恰好一半向上。除零报 INVALID，结果超出 uint64_t 报 RANGE，失败保持输出。可直接操作 hx_os_uint128 的 low/high。支持编译器原生 128 位整数时使用内建类型，其他目标使用共同的 32 位分块算法。没有堆分配、锁、浮点精度损失或新增汇编。

精细等待接收本库绝对单调截止时间，默认最后 200 微秒自旋；_ex 允许 0..10 毫秒自旋预算，0 使用普通睡眠。NOW 立即成功，INFINITE 无效。不会修改全局计时器精度或调度优先级。系统调度可能延后唤醒，甚至跳过整个自旋窗口；该接口不提供硬实时、可取消或必然更准的保证。 Windows 兼容时钟按 tick 更新；自旋用独立 QPC 计时限制预算，预算耗尽后恢复普通睡眠，不改变原有截止时间的时钟起点和休眠语义。参阅 [Windows 时钟说明](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-queryunbiasedinterrupttime)。

repeat 在调用线程上同步执行：正毫秒间隔、正次数上限，首次在一个间隔后，错过多个周期合并；回调 OK 继续、EOF 正常停止、其他错误原样传播。回调获得非空且已清零的错误记录。_until 同时返回实际启动的次数。截止时间约束等待，不能中断回调。需要后台定时器时使用原有 timer/loop。

## 命名过程与关闭

~~~cpp
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
~~~

过程名为复制的严格 UTF-8，1..127 字节，唯一、区分大小写；重复注册报 EXISTS，缺失报 NOT_FOUND。没有签名 DSL 或隐式类型转换。参数包按值复制，回调成功才提交输出；借用指针指向对象的副作用不能回滚。 C++ 回调声明 noexcept，可返回 Result<void>、状态码或 void；Result 失败保留原生错误详情。

调用同步执行，多个线程的调用可重叠，回调在锁外运行；默认最多 256 个注册、64 个并发调用、16 层嵌套，可通过 options 调整。共享信号模块的在途计数、回调退役和等待实现；过程可以在 SIGNALS 关闭时使用。成功注册后 cleanup 被接管，最后一个在途回调退出后在锁外执行一次；被拒绝的注册不接管用户资源。

unregister_until 先阻止新启动，再等待在途执行；超时保留 ID，重试同一 ID。 close_until/池 shutdown 超时或失败保留所有者；成功才置空并释放。自我/祖先等待返回 DEADLOCK。最终销毁前完成注册、注销和控制调用，已经接受的回调可以继续退出；多个最终销毁必须互斥，并排除新 API 进入。 add/close 的分配器回入同一过程表会被拒绝，其间 stop 忽略。截止时间不能抢占同步回调或 cleanup。

宿主在卸载插件前必须完成调用并退役回调、分配器用户数据与借用指针。 C++ ProcedureHandler/ThreadPool 是宿主持有句柄的便利层，析构不偷偷等待或遗失句柄。

## 文件名与词法分析

原有文件名入口继续使用 UTC，默认格式为 `%CCYY-%MM-%DD_%hh-%mm-%ss`。`hx_os_filename_format_local_string()` / `hx::Calendar::localFilename()` 显式选择本地时间；`hx_os_filename_generate_local()` / `Calendar::localFilenameNow()` 使用当前墙上时钟；`Calendar::filenameNow()` 继续使用 UTC。Windows 非法字符、控制字符、末尾空格／点号及保留设备名会按原规则修复，Unicode 保留。扩展名可带一个前导点，最多 32 个 ASCII 字母、数字、连字符或下划线；结果最多 240 个 UTF-8 字节。不创建目录，不保证唯一，也不自动覆盖重名文件。夏令时回拨时，本地时间可能重复，生成的文件名也可能重复。

展示本地时间时，使用 `hx_os_wall_time_format_local_string()` / `hx::Calendar::local()`，不执行文件名字符替换。默认结果明确带时区偏移，例如 `2024-03-10T03:00:00-04:00`。`hx_os_wall_time_local_offset_at()` / `Calendar::localOffsetAt()` 查询指定 Unix 毫秒时间点的偏移，单位为**秒**，同时返回 DST 0/1/-1；原有“当前偏移”接口继续以**分钟**为单位。库不修改全局时区或 locale。

| 标记 | 含义 |
| --- | --- |
| `%CCYY`、`%Y`；`%YY`、`%y` | 四位年份；年份后两位 |
| `%MM`、`%m`；`%DD`、`%d` | 月份；月内日期 |
| `%hh`、`%H`；`%I`、`%p` | 24 小时；12 小时与 AM/PM |
| `%mm`、`%M`；`%ss`、`%S` | 分钟；秒 |
| `%a`、`%A`；`%b`、`%B` | 英文星期、月份的缩写／全名 |
| `%j`；`%u`、`%w` | 年内第 001～366 天；星期一为 1／星期日为 7，或星期日为 0 |
| `%L`；`%s` | 000～999 毫秒；向下取整的 Unix 秒，不受时区影响 |
| `%z`、`%:z` | 有符号偏移 +HHMM 或 +HH:MM；需要时附加秒 |
| `%Z` | 数值标签 `UTC` 或 `UTC+HH:MM[:SS]` |
| `%%` | 百分号本身 |

原有多字符标记优先于较短标记。星期、月份名称和 AM/PM 固定为英文。未知标记返回 SYNTAX，失败保留输出。`%Z` 不返回有歧义的缩写或 IANA 时区标识。UTC 时间 `ms=-1` 使用 `%s.%L` 时结果为 `-1.999`，这表示向下取整的秒与对应毫秒部分，并不是把时间当成一个实数秒值来表示。

UTC 仍支持公历延伸范围 0001～9999 年。本地转换遵循宿主 C 运行库可用范围与时区规则：Windows 适配层通过 `_localtime64_s`，保守接受 1970～3000 年的 UTC 时间点；POSIX 还受 `time_t` 位宽和已安装时区数据影响。不内置独立历史／命名时区数据库，也不承诺闰秒格式化。宿主显式修改 TZ／时区时需要自行串行协调，参见 [Microsoft 本地时间契约](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/localtime-s-localtime32-s-localtime64-s)。

```cpp
// Include <hx/Calendar.hpp>; check both Result<String> values.
auto display = hx::Calendar::local(milliseconds);
auto name = hx::Calendar::localFilename(
	milliseconds, "dat", "%Y-%m-%d_%H-%M-%S_%L");
```

这里的 `milliseconds` 是 Unix 墙上时钟毫秒，不是单调截止时间。C 版本直接写入已初始化的 `hx_string`。格式化和字符串构造同步执行，不创建定时器或线程。

词法器复制严格 UTF-8 输入，接受开头 BOM，拒绝内嵌 NUL； token 的 C 视图存活到 lexer_destroy，C++ Token 自己持有字符串。默认返回标识符、数字、引号串、单字符标点、换行，跳过空白和注释。标识符允许非 ASCII 码点，但不声称完成 Unicode 标识符标准验证；数字包含十进制/小数/指数/十六进制整数，符号单独返回。

next/peek/reset 要求外部同步；EOF 返回 END token，失败保留输出。词法错误保持到 reset；position 可取得失败起点。行列从 1 开始，列按 Unicode 码点计，Tab 占一列，CRLF 为一个换行。默认输入 16 MiB、单 token 1 MiB、token 数 1 Mi；跳过的空白、注释也受限。它不是 C 编译器、预处理器或脚本运行时。

## 裁剪与成本

新增 WIDE_MATH、PRECISE_WAIT、LEXER、PROCEDURES 默认开启，可分别关闭。文件名依赖 CALENDAR；翻译文本加载依赖 TEXT_LOOKUP+LEXER，文件加载再依赖 FILE_UTILS。SIGNALS 或 PROCEDURES 任意一个开启即可使用参数包。关闭模块保留公共符号、清除相应能力位、返回 NOT_SUPPORTED。

ASCII 修改和宽位运算无分配；replace/join 使用临时字符串保证失败不变； split 分配拥有副本的结果。子串搜索采用朴素匹配，不承诺最坏线性复杂度。翻译表加载可能逐项复制和插入，不面向热循环。过程调用成功注册后不再分配上下文内存，但仍扫描有界槽表、获取锁并间接调用回调；有实际成本，不把它用来替代本来就知道目标的直接函数调用。基准入口为 hx_os_portable_baseline；结果与目标、优化选项和系统负载有关。

## 显式调试断点

`<hx_os_util.h>` 中的 `hx_os_debug_break()` 在当前调用线程请求原生断点，Release 构建也生效。Windows 调用 `DebugBreak`，POSIX 触发 `SIGTRAP`。调试器或应用自己的处理器可以恢复执行；没有处理者时，操作系统可能终止进程。库不会安装信号处理器，也不会用这个入口报告普通调用错误。

只有业务明确决定停到断点时才调用。POSIX 契约测试在它自己的测试进程中临时安装处理器，验证收到断点并恢复执行。Windows 已验证编译和链接，自动测试不实际触发 Windows 断点。
