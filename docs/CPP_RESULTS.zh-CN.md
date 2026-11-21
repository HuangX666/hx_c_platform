# C++11 结果、所有权与迁移

[English](CPP_RESULTS.md) · [README](../README.zh-CN.md) · [线程池](CPP_POOL.zh-CN.md)

本项目的所有 C++11 便利层接口在可能失败的操作上统一使用 `hx::Result<T>` 或 `hx::Result<void>` 返回结果，彻底避免 `try`/`catch`、异常对象分配和延迟异常转发。底层 C99 实现与公开 C ABI 保持不变；在 C++ 工程中只需包含对应的 `hx/*.hpp` 头文件并链接 `hx_c_platform::cpp`。

### 快速体验：读取文件并处理错误

在[读取文件的完整 C++ 程序](USAGE.zh-CN.md#files)中，程序调用 `hx::Files::readText()` 读取文本，先用 `if (!text)` 检查是否失败，成功后再通过 `text.value().c_str()` 输出内容：
- 当工作目录下存在 `hello.txt` 时，程序直接打印文件内容；
- 当路径不存在时，程序打印 `Read failed` 以及对应的错误状态名。

`hx::Result<T>` 将返回值与 `hx_os_error` 错误诊断打包在同一个栈上对象里。绝大多数操作只需在 `ok()` 为真时读取 `value()`；而批量读写等支持“部分完成”的接口，在返回错误或超时时仍可能保留已完成的数量（可通过 `hasValue()` 检查后读取）。下文将详细说明结果读取、部分完成语义、资源生命周期与无异常构建配置。

## 读取与传递结果

~~~cpp
#include <hx/Files.hpp>

hx::Result<hx::String> load_configuration(const char *path) noexcept
{
	auto result = hx::Files::readText(path);
	if (!result)
		return hx::Failure(result.error());

	return std::move(result).value();
}
~~~

- **检查成功与错误**：`ok()`（或显式 `bool` 转换）为真代表状态为 `HX_OS_OK`（对于非 `void` 结果，同时保证存在有效值）。`status()` 返回底层 `hx_os_status` 状态码；`error()` 返回结果对象内部持有的 `hx_os_error` 引用，完整保留原生错误域、平台错误码和原因码。若诊断信息需要活得比 `Result` 对象更久，请按值复制该 `hx_os_error` 结构体。库不维护全局 `last_error` 或临时诊断指针。
- **移动语义与生命周期**：
  - `Result<T>` 是**只移动（move-only）**类型。移动操作会消费源对象，将其状态置为 `HX_OS_CLOSED` 并清空内联值。内联存储要求 `T` 的移动构造和析构不抛异常（若按值复制进 `Result`，则复制构造也必须不抛异常）。
  - `Result<T&>` 是可复制的借用引用包装，不会延长被引用对象的生命周期。
  - `Result<void>` 仅保存错误记录，默认构造即表示 `HX_OS_OK`。
- **安全访问内联值**：调用 `value()` 的前提是 `hasValue()` 为真（违背该前提属于编程错误，Debug 构建下会触发断言）。对于非 `void` 结果，也可以调用 `get()` 获取可空指针进行安全检查。切勿读取已被移走（moved-from）的 `Result` 对象。`Result` 自身不加锁、也不分配堆内存。

[Variant 便利层](VARIANT.zh-CN.md#cpp) 遵循完全一致的规则：`set`、`emplace`、`visit` 返回 `Result<void>`，`clone` 返回 `Result<Variant>`，标量 `get` 返回值副本，自定义类型 `get` 返回受检查的借用引用；字符串拥有独立字节并通过 `StringView` 读取。

构造 `hx::Failure(HX_OS_OK)` 会得到 `HX_OS_INVALID`，避免将失败对象转换为成功结果。

## 部分完成与超时

在流式 I/O、批量处理或带截止时间的等待中，操作可能在完成一部分工作后遇到错误或超时。此时 **`ok()` 为 `false`，但 `hasValue()` 仍可能为 `true`**：

| 操作接口 | `Result` 行为与部分完成契约 |
| --- | --- |
| 文件 `readSome` / `readAt`、本地套接字 `readUntil` | 返回 `Result<ReadResult>`；遇到 EOF 或部分失败时，`hasValue()` 仍为真，可读取已传输的 `bytes` 和 `eof` 标志 |
| 文件 `writeAll` / `writeAt`、本地套接字 `writeUntil` | 返回 `Result<size_t>`；写入中途失败时，仍可通过 `value()` 获取已成功写入的字节数 |
| `IoRequest::waitUntil` | 等待本身超时或失败时无值；一旦底层原生 I/O 完成（即使 I/O 结果本身报错），仍携带 `completion` 和 `bytes` |
| `File::read` | 正常到达文件末尾时返回 `HX_OS_OK` 且 `eof = true`；中途失败时保留已读取字节进度 |
| `Directory::forEach` / `SettingsUpdate::finish` | 中途失败时，仍保留已完成遍历或已成功应用的条目数量 |
| `Directory::next` | 到达目录末尾（EOF）时不含目录项；若分配目录项名称内存失败，不会消费当前待读取的目录项 |
| `Lexer::next` | 到达末尾（EOF）时携带 `END` 令牌；复制令牌内存失败时保留游标位置，可供重试 |
| `Task<T>::getUntil` | 等待超时或失败时保留 `Task` 句柄；一旦任务执行结束（无论成功、失败还是被取消），均消费句柄并返回结果 |
| `ProcessCapture::run` / `closeUntil` | 失败或超时时，可能保留宿主持有的原生会话句柄以便后续重试清理 |
| `Wait::repeat`、`SignalHandler::emit` | 回调中途报错时，`hasValue()` 仍可提供已成功执行的回调次数 |

> [!IMPORTANT]
> **先判状态，再看进度；超时不等于后台已停止**
> 请始终先检查操作的 `ok()` 或 `status()`，再决定如何使用 `value()`。单调时钟截止时间只限制当前调用的等待时长，无法强制抢占正在进行的底层系统 I/O、回调执行或用户清理代码。在操作返回 `HX_OS_TIMEOUT` 或 `HX_OS_BUSY` 后，宿主持有的 `owner`、借用缓冲区及代码所在模块必须继续保持有效，直到资源成功关闭并退役。

## 字符串、缓冲区与分配器

- **`hx::String` 与 `hx::StringView`**：`hx::String` 独占持有底层 `hx_string`，完整保留 UTF-8 字节、显式长度以及内嵌的 `\0`（NUL）字节，仅支持移动。需要复制时，请调用显式返回 `Result` 的 `String::copy(view, context)`；调用 `assign()` 若遇内存不足会保留原字符串内容不变。`c_str()` 保证以 `\0` 结尾，二进制安全长度请使用 `size()`。`StringView` 仅借用外部字节（指针 + 长度），不发生隐式内存分配；一旦源字符串被修改或销毁，视图即失效。
- **`hx::Buffer`**：独占持有通过 `hx_os_context` 分配的字节缓冲区。`Buffer::allocate()` 在内存不足时返回 `HX_OS_NO_MEMORY`，析构与移动均通过创建时绑定的分配器释放或移交所有权。输出 `hx::String` 的接口通常接受可选的 `context`（传入 `NULL` 使用默认分配器）；而像动态库加载诊断这类由调用方传入已存在 `String` 对象的接口，则会沿用该输出对象自身绑定的 `context`。
- **分配器边界**：C++ 回调与线程池任务会将具体类型的捕获变量直接存入由 `hx_os_context` 分配的内存中，库内部绝不通过 `std::string`、`std::vector` 或 `std::function` 进行隐式堆分配。

## 常用操作减少中间检查

为了避免在连续的字符串处理、配置修改或目录遍历中编写冗长的逐行错误检查，便利层提供了以下流式与游标辅助接口（完整用法见[直接调用示例](../examples/direct_calls.cpp)和[C/C++ 契约](COMMON_UTILITIES.zh-CN.md)）：

- **原地字符串处理**：`String::trimAscii().lowerAscii()` 直接在已初始化的同一份字符串存储上原地修改并返回自身引用，不产生内存分配（而可能扩容的 `append` 和 `reserve` 仍返回 `Result`）。
- **批量更新游标（`update()` / `edit()`）**：`Settings` 和参数包提供 `update()` / `edit()` 游标。连续调用多个 setter 时，每次修改立即生效；一旦某个 setter 失败，后续 setter 会自动跳过，最后只需调用一次 `finish()` 集中检查错误（`SettingsUpdate::finish()` 在失败时还会保留已成功修改的项数）。原有的单项返回 `Result` 的 setter 也全部保留。
- **零分配文本借用（`*View`）**：
  - `ConfigDocument::stringView`、`CallDataView::stringView` 和 `TextLookup::getView` 直接返回借用的 `StringView`（使用期间请保持文档、参数包或翻译表存储有效且不被修改；翻译项缺失时直接借用传入的 `fallback`）。
  - `Settings::stringView` 与 `SettingValue::asStringView` 返回 `SettingStringView` 租约对象：它通过引用计数持有不可变的字符串字节，即使后续设置被更新或关闭也依然有效，复制该租约对象不产生堆分配（但从该租约取出的普通 `StringView` 仍借用自该租约）。
- **目录遍历与带默认值的读取**：`Directory::forEach` 自动完成目录打开、复用名称缓冲区读取以及关闭，遇到正常 EOF 或回调返回 `false` 均视作正常结束。带默认值的设置项 getter 仅在状态为 `HX_OS_NOT_FOUND` 时回退到默认值，不会掩盖类型不匹配等其他错误。

## 回调与生命周期

信号、过程、异步管道、目录监听、描述符监听和周期定时器的 C++ 回调均支持返回 `void`、`hx_os_status` 或 `Result<void>`：
- 函数对象和 lambda 必须声明为 `noexcept`（C++11 函数指针/引用通过契约保证不抛异常），捕获对象的构造与析构也必须不失败。
- 返回 `void` 代表成功，返回 `hx_os_status` 报告对应状态类别，返回 `Result<void>` 则保留完整的 `hx_os_error` 原生诊断。

不同模块的回调错误传递方式如下：
- **同步信号与过程**：直接将回调失败返回给发起方（过程参数仅在回调返回成功时提交）。
- **异步 I/O 请求**：将回调自身的错误与底层原生 I/O 完成状态分开保存，操作完成后可通过 `callbackResult()` 查询。
- **监听与周期定时器**：回调返回错误会自动请求停止监听或定时器；请在资源退役或成功关闭后调用 `callbackResult()` 读取错误（若在回调尚在运行时过早查询，会返回相应的等待或忙碌状态，从而避免数据竞争）。

> [!NOTE]
> **两类资源封装模型：RAII 独占对象 vs. 宿主指针视图**
> 1. **同步资源（RAII 工厂模型）**：`File::open`、`Directory::open`、`Library::open`、`Pages::allocate`、`MappedFile::open`/`create`、`FileRangeLock::acquire`、`CpuUsage::create`、`Semaphore::create` 等通过返回 `Result<T>` 的静态工厂创建。若需要显式处理关闭或解锁时的错误，可在析构前调用对应的 `close()` / `unlock()` 方法。`ProfileScope` 不可移动，构造时传入 profiler，随后调用 `start()` 并检查结果，需要检查结束状态时显式调用 `finish()`。
> 2. **含后台活动或需重试关闭的资源（宿主 `owner` 视图模型）**：`ThreadPool`、`Thread`、`IoLoop`、`AsyncPipe`、`Process`、`ProcessCapture`、`DirectoryWatch`、`DescriptorWatch`、`PeriodicTimer`、`SignalHandler`、`ProcedureHandler`、`Settings` 和 `Profiler` 均引用宿主持有的原生 `owner` 指针变量。它们的析构函数**不会暗中无限等待，也不会丢弃超时后需要重试的句柄**。请确保宿主 `owner` 变量、回调捕获以及借用缓冲区（如异步管道读写缓冲区）活过所有在途操作与关闭重试。

## 现有消费者迁移

如果您的代码正从旧版 C++ 接口升级到当前基于 `hx::Result` 的契约，请按下表调整调用方式：

| 旧调用方式 | 当前 `hx::Result` 调用方式 |
| --- | --- |
| 构造函数直接执行可能失败的初始化 | 改为调用静态工厂获取 `Result<Resource>`，检查 `ok()` 后再移动或访问 |
| 文本方法直接返回 `std::string` | 返回 `Result<hx::String>`，通过 `c_str()`、`size()` 或 `view()` 访问 |
| Getter 直接返回值 `T` | 返回 `Result<T>`，先检查状态再调用 `value()` |
| 连续调用多个无返回值的 Setter | 使用 `update()` / `edit()` 游标配合一次 `finish()`，或逐项检查 `Result<void>` |
| 依赖延迟转发的回调异常/错误 | 在原生资源退役或关闭后调用 `callbackResult()` |
| 通过异常栈展开响应取消 | 调用 `token.check()` 并返回 `Result<void>` |
| 线程池 `submit` 后直接取值 | 先检查 `Result<Task<T>>`，再检查 `getUntil()` 返回的 `Result<T>` |
| 隐式全局解析或加载诊断 | 读取 `Result::error()` 中的 `hx_os_error` 副本，或传入调用方持有的诊断 `hx::String` |

升级头文件时，请将 C++ 宿主程序与相关插件一起重新编译。纯 C 语言调用方无需改动，原有导出函数、公开结构体和 C99 ABI 保持完全兼容。

## 关闭异常构建

本仓库对自身的 C++ 示例与测试目标启用了无异常编译，但导出的 `hx_c_platform::cpp` 接口目标不会将无异常开关强加给您的应用目标。如果您的工程也希望开启无异常构建，可在 `CMakeLists.txt` 中按如下方式配置：

~~~cmake
target_link_libraries(my_app PRIVATE hx_c_platform::cpp)
target_compile_features(my_app PRIVATE cxx_std_11)

if(MSVC)
	target_compile_options(my_app PRIVATE /EHs-c-)
	target_compile_definitions(my_app PRIVATE _HAS_EXCEPTIONS=0)
else()
	target_compile_options(my_app PRIVATE -fno-exceptions)
endif()
~~~

各目标平台的编译器版本、系统要求及能力边界详见[平台支持](PLATFORMS.zh-CN.md)。
