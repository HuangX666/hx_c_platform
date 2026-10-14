# C++11 结果、所有权与迁移

[English](CPP_RESULTS.md) · [README](../README.zh-CN.md) · [线程池](CPP_POOL.zh-CN.md)

本项目所有可能失败的 C++ 接口使用 `hx::Result<T>` 或 `hx::Result<void>`，不使用异常语法、异常对象或延迟异常转发。C99 实现和公开 C ABI 保持不变。包含需要的 `hx/*.hpp`，链接 `hx_c_platform::cpp`。

## 读取与传递结果

[Variant 便利层](VARIANT.zh-CN.md#cpp) 沿用同一规则：set/emplace/visit 返回 Result<void>，clone 返回 Result<Variant>，标量 get 返回副本，自定义 get 返回受检查的借用引用。字符串拥有字节，通过 StringView 读取。复制通过可失败的 clone 显式进行；自定义类型的普通 C++ 构造/析构必须符合 noexcept 契约。

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

`ok()` 或显式布尔转换表示状态为 OK；非 void 结果还要求存在值。`status()` 返回 C 状态码。`error()` 借用结果对象内部复制的 `hx_os_error`，保留原生错误域、错误码和原因码；需要跨越结果生命周期时复制该记录。不引入全局最后错误或临时诊断指针。

`Result<T>` 只能移动；移动消费源对象，使其变为 CLOSED 且不再持有值。内联值要求移动构造、析构不失败，复制进入结果时还要求复制构造不失败。`Result<T&>` 是可复制的借用引用，不延长被引用对象的生命。`Result<void>` 仅保存错误记录，默认构造表示成功。

`value()` 的前提是 `hasValue()` 为真。违反前提属于编程错误，调试构建使用断言，并非普通失败的处理方式。非 void 结果的 `get()` 返回可空指针，可据此检查访问。不要读取已移动的结果。结果对象自身不加锁、不分配堆内存，内部资源按对应接口契约分配。

## 部分完成与超时

| 操作 | 结果行为 |
| --- | --- |
| 文件 readSome/readAt、本地套接字 readUntil | Result<ReadResult>；包括 EOF 或部分失败时，hasValue() 可取得 bytes/eof |
| 文件 writeAll/writeAt、本地套接字 writeUntil | Result<size_t>；失败仍可取得已完成字节数 |
| IoRequest::waitUntil | 等待失败没有值；原生 I/O 已完成时，即使 I/O 失败也携带 completion/bytes |
| File::read | EOF 返回成功且 eof=true；失败保留字节进度 |
| Directory::forEach / SettingsUpdate::finish | 失败仍提供完成／成功次数 |
| Directory::next | EOF 不含目录项；分配失败不消费待取目录项 |
| Lexer::next | EOF 携带 END token；复制 token 内存失败后可以重试 |
| Task<T>::getUntil | 等待失败保留任务；完成后消费结果，包括取消或失败 |
| ProcessCapture::run / closeUntil | 失败或超时可能保留宿主持有的原生会话，用于继续处理 |
| Wait::repeat、SignalHandler::emit | 失败时 hasValue() 仍可能提供已完成回调数量 |

先判断具体操作的状态，再决定是否将值作为成功数据使用。一个结果可以在 `ok()` 为假时仍持有值。截止时间限制等待，无法抢占原生完成、回调或用户清理；原生 owner、借用缓冲区和代码模块必须保留到成功关闭或退役。

## 字符串、缓冲区与分配器

`hx::String` 持有 `hx_string`，保留 UTF-8、字节长度和内部 NUL，支持移动。`String::copy(view, context)` 显式执行可能失败的复制；`assign()` 失败保留原内容。`c_str()` 带结束符，使用 `size()` 取得支持内部 NUL 的长度。`StringView` 借用字节，支持指针加长度，不隐式分配；源被修改或销毁后视图失效。

`hx::Buffer` 持有 context 分配的字节，`Buffer::allocate()` 分配失败返回 NO_MEMORY。析构和移动通过创建时的分配器释放或转移资源。字符串结果通常接受可选 context，NULL 使用既有默认分配器；动态库诊断等传入 String 的接口使用输出对象自己的 context。

C++ 回调和线程池任务将具体类型的捕获直接存入 context 分配的内存，不通过标准字符串、vector 或类型擦除函数容器进行库自身分配。自定义分配器回调和用户类型须遵守构造、析构不失败的契约。用户自行执行的标准库分配仍由用户负责；仅关闭编译器异常不能把这些分配变成可恢复失败。

## 常用操作减少中间检查

`String::trimAscii().lowerAscii()` 在同一已初始化字符串上执行，返回引用，不分配。`append` 和 `reserve` 继续报告可能失败的结果。设置和参数包提供明确的 `update()`／`edit()` 游标：setter 立即生效，首次失败跳过后续项，最后 `finish()` 检查一次。设置 finish 在失败时也保留成功次数。原有逐项返回 Result 的 setter 继续保留，完整调用见 [直接调用示例](../examples/direct_calls.cpp) 和 [C/C++ 契约](COMMON_UTILITIES.zh-CN.md)。

`ConfigDocument::stringView`、`CallDataView::stringView` 和 `TextLookup::getView` 返回借用文本，保持文档／参数包／翻译表存储有效并排除修改。翻译项缺失时借用传入的 fallback。`Settings::stringView` 与 `SettingValue::asStringView` 返回 `SettingStringView`，持有不可变值字节，设置更新或关闭后仍有效；复制持有对象不分配。从该对象取得的普通 `StringView` 仍借用它。原有复制字符串 getter 全部保留。

`Directory::forEach` 完成打开、读取和关闭，复用名称缓冲区；EOF 和 bool 回调返回 false 均正常结束。`File::read` 成功结果通过 `ReadResult::eof` 表达文件结束，`readSome/readAt` 保留原 EOF 状态。带默认值的设置 getter 只对 NOT_FOUND 回退。

## 回调与生命周期

信号、过程、异步管道、目录监听、描述符监听和周期定时器回调可以返回 void、`hx_os_status` 或 `Result<void>`。函数对象和 lambda 必须声明 `noexcept`，C++11 函数指针／引用通过契约保证相同行为。捕获的构造和析构不能失败。void 表示成功，状态码报告错误类别，Result 保留完整原生详情。

信号和过程直接返回失败；过程参数仅成功时提交。异步 I/O 将回调失败与原生完成状态分开保存，完成后通过 `callbackResult()` 查询。监听和定时器回调失败会请求停止，退役或成功关闭后检查 `callbackResult()`；过早查询返回对应等待或忙碌状态，避免与回调读写竞争。

原生资源和便利对象分别保留回调状态引用。原生退役销毁捕获一次，便利对象可以继续保留诊断。提前销毁 I/O 请求便利对象只释放观察引用，原生操作及捕获仍有效。异步管道读写缓冲区，包括 StringView 写入，均借用到完成回调返回，必须保持实际存储有效。

同步资源通过 RAII 和返回 Result 的工厂创建：File::open、Directory::open、Library::open、Pages::allocate、MappedFile::open/create、FileRangeLock::acquire、CpuUsage::create、Semaphore::create 以及平台请求工厂。需要处理关闭或解锁错误时显式调用对应方法。默认空构造不创建资源。ProfileScope 不可移动，先传入 profiler 构造，再调用 `start()` 并检查，关心结束状态时显式调用 `finish()`。

ThreadPool、Thread、IoLoop、AsyncPipe、Process、ProcessCapture、DirectoryWatch、DescriptorWatch、PeriodicTimer、SignalHandler、ProcedureHandler、Settings 和 Profiler 引用宿主持有的原生 owner 变量。便利对象析构不暗中无限等待，不丢弃可重试句柄。owner 变量和借用的捕获必须覆盖所有在途工作及关闭重试，调用者排除 owner 变量的并发修改。

## 现有消费者迁移

| 旧调用方式 | 当前方式 |
| --- | --- |
| 资源构造函数执行可能失败的工作 | 工厂返回 Result<Resource>，先检查再移动或访问值 |
| 文本方法直接返回标准字符串 | Result<hx::String>，使用 c_str()/size()/view() |
| getter 直接返回 T | Result<T>，先检查再取得 value |
| 连续执行多个 setter | update()/edit() 后检查一次 finish()，或逐项检查 Result<void> |
| 延迟转发回调错误 | 原生退役后调用 callbackResult() |
| 通过栈展开检查取消 | token.check() 返回 Result<void> |
| 提交后直接取得值 | 检查 Result<Task<T>>，再检查 getUntil() 的 Result<T> |
| 隐式 token 或解析诊断 | 复制的 hx_os_error；可选解析诊断或调用者持有的动态库诊断 String |

这些属于 C++ 源码签名和模板布局变更，C++ 消费者和插件应一起重新编译。C 调用者继续使用原有导出函数、公开结构体和 ABI，不增加第三方依赖。

## 关闭异常构建

项目对自己的 C++ 测试和示例私有应用以下设置，安装导出的接口目标不影响消费者其它目标。应用采用相同策略时：

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

目标平台的编译、部署要求及限制见[平台支持](PLATFORMS.zh-CN.md)。公开示例独立于维护者的本地回归。
