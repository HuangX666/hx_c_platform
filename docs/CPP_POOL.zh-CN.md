# C++11 线程池封装

[English](CPP_POOL.md) · [README](../README.zh-CN.md)

`hx/ThreadPool.hpp` 在现有有界 C 线程池上提供仅头文件的 C++11 接口。
C++ 消费者链接 `hx_c_platform::cpp`，纯 C 消费者继续链接
`hx_c_platform::hx_c_platform`。没有第二套工作线程调度器，
没有把 C++ 对象放进 C ABI。C++ 消费者需开启异常，基础库仍按 C99 编译。
协程留待后续，本模块不提供协程。

~~~cmake
find_package(hx_c_platform 0.1 CONFIG REQUIRED)
target_link_libraries(my_cpp_app PRIVATE hx_c_platform::cpp)
~~~

导入目标声明 C++11 要求，并传递 C 库所需链接项。
头文件不依赖预编译头或总头。C++ 类型是源码接口，
不保证不同编译器／标准库之间的 C++ 二进制 ABI。

## 所有权与关闭

`ThreadPool` 是宿主持有的 `hx_os_thread_pool *` 变量的视图。
该变量必须活过所有视图，且不能与线程池调用并发修改。
复制视图仍引用同一个所有者。
视图析构不关闭、不等待、不分离线程；原始所有者仍可继续操作。
宿主必须最终完成 close 和 destroy。

这样可以在关闭返回 TIMEOUT/BUSY 时保留一个仍能访问的线程池及任务，
不在 C++ 析构中藏入无限等待。
`Task<T>` 和取消句柄会自动释放自己持有的引用。
任务可活过已经成功销毁的线程池；残留的任务／结果分配仍可能使 context 忙碌。

~~~cpp
#include <hx/ThreadPool.hpp>

hx_os_thread_pool *owner = NULL; // Keep in the host's long-lived state.
hx_os_thread_pool_options options;
hx_os_thread_pool_options_init(&options);
options.threads = 4;
options.queue_capacity = 128;

auto pool = hx::ThreadPool::create(owner, NULL, &options);
auto answer = pool.submit([](int x) { return x * 2; }, 21);
int value = answer.get(); // 42; task exceptions propagate here.

hx_os_status status = pool.closeUntil(hx_os_deadline_after_ms(2000));
if (status == HX_OS_OK) {
	// Exclude all pool callers before destroying the owner.
	status = pool.destroy();
}
// Handle status. TIMEOUT/BUSY keeps owner intact for a later retry.
~~~

[独立示例](../examples/thread_pool.cpp) 包含错误处理，
并在安装消费测试中实际运行。两秒只是示例策略，不是全库固定默认。
服务程序应把 owner 放在持久的关闭状态中，停止上层新提交，
按业务需要对任务请求取消，再使用新的单调截止时间重试 close。
TIMEOUT/BUSY 时不能丢弃这些状态。成功关闭后，destroy 前也必须排除所有
并发线程池调用；destroy 成功清空 owner。

进入关闭流程后，不再接受新任务。
DRAIN 执行已接受的排队任务；CANCEL_PENDING 取消尚未开始的任务，
不强制打断正在执行的任务。两者均等待工作线程清理和原生线程退出。
同池工作线程调用 close 返回 DEADLOCK；并发关闭者可能收到 BUSY，
正在提交线程上执行的准备回调也会使 close/destroy 保持忙碌。
基础库不强杀线程，不暗中建立后台回收线程。

## 提交与结果

`submit(callable, args...)` 接受 lambda、普通／成员指针、
只能移动的函数对象和参数、`std::ref`，支持值、引用、void 返回值。
存储的可调用对象按左值调用，存储的参数移动给调用表达式；
`std::ref` 明确表示保留借用引用。
返回的 `Task<T>` 只能移动，支持：

| 方法 | 契约 |
|---|---|
| valid() | 当前对象是否持有任务引用 |
| ready() | 非阻塞完成查询；完成包含参数和函数对象清理 |
| waitUntil(deadline) | 库的单调时钟绝对截止时间，返回等待状态 |
| waitFor(milliseconds) | 相对时长；零和负值表示立即查询 |
| wait() | 显式无限等待；等待失败抛 StatusError |
| get() | 无限等待并消费结果，返回值或重抛任务异常 |
| cancel() | 协作取消请求，不强制打断 |
| cancellationSource() | 获取可供其他线程持有的独立取消源 |

`HX_OS_DEADLINE_NOW` 表示查询，`HX_OS_DEADLINE_INFINITE` 显式无限等待。
等待成功只表示完成，执行结果或异常由 get 取出。
超时不消费任务；同池等待未完成任务返回 DEADLOCK，
不会偷偷执行其他排队任务来尝试解除死锁。
get 在等待失败时保留句柄；取得已完成的结果、异常或取消后消费句柄。

用户异常在工作线程捕获，在 get 处重新抛出，不穿过 C 回调边界。
库错误使用 `hx::StatusError`，可取得状态和复制的 `hx_os_error`。
排队取消返回 CANCELLED。
丢弃 Task 只释放观察结果的引用，不会取消工作。
本版不提供 std::future 转换、shared_future 或 continuation 接口。

绑定参数会复制／移动，但不会自动深拷贝指针指向的对象。
借用对象必须活过工作线程使用它的时间。
返回 `T&` 必须引用能活到结果消费结束的外部存储，
不能返回清理阶段会销毁的参数／捕获对象的内部引用。
结果和绑定值的析构必须不抛异常。
析构可能发生在工作线程、提交回滚线程，或释放最后一个结果引用的线程。

## 准入、分配与重入

C 接口 `hx_os_thread_pool_submit_prepared` 先预留队列准入名额，
再构造 C++ 任务载荷。队列已满或已经关闭时，
拒绝提交不会复制可调用对象，也不分配 C++ 任务状态。
原有 C submit 的拒绝／清理契约保持不变。

准备和清理回调在池互斥锁外执行。
准备失败需清理部分资源；C++ 拷贝／移动构造抛异常时在提交线程重抛。
如果关闭与已成功准备的载荷竞争，C 入口返回 CLOSED，
并在提交线程恰好清理一次；这时无法恢复已经移动过的调用参数。
接受成功的载荷在工作线程恰好清理一次，被跳过的取消任务同样如此。
拒绝清理完成前，池保留提交准入计数，防止过早销毁。

函数对象、结果存储及状态管理共用一次来自池 context 的对齐分配，
原生任务／事件另有分配。视图默认限制单个内联任务状态为 1 MiB；
可用 `hx::ThreadPool view(owner, maxTaskBytes)` 指定其他上限。
该限制包含结果存储及管理结构的 sizeof，
不包含任意用户类型或异常对象另外分配的对象图。
队列之外保留多少结果，由宿主保留多少 Task 句柄决定。
库不会替换进程的 C++ 分配器。

准备／清理回调和用户析构应避免无界等待。
关闭截止时间不能取消任意用户代码，也不提供硬实时保证。
工作线程的同池等待会被拒绝，但库不能检测所有涉及外部资源的依赖环。

## 跨线程协作取消

~~~cpp
auto task = pool.submitCancelable([](hx::CancellationToken token) {
	while (!token.stopRequested()) {
		// Perform one bounded unit of work, then check again.
	}
	token.throwIfCancelled();
});
auto stop = task.cancellationSource();
// Move task to its waiting thread; pass a copy of stop to the controller.
stop.requestStop();
~~~

CancellationSource/Token 持有原生任务引用，可活过线程池。
把 Task 交给等待线程之前先复制取消源。
不能在同一个 Task 对象上并发执行 cancel 与 get／移动／析构；
使用独立取消源即可在任务执行或被等待时安全请求停止。
同一个取消源对象本身的赋值／析构也需要排除并发使用。
取消只是请求，任务需要检查 token，或采用业务选定的可中断原生操作。

## 存储布局与重新编译

内部结果状态在编译时选择成员或基类存储。
void 空存储可利用空基类优化，适合的结果存储可以复用基类尾部填充。
短小非空的基类若放在 8 字节对齐的引用计数之前，反而可能使 32 位对象变大，
因此这类结果保留成员存储。不需要压缩访问、缩小原子类型或引入 C++20 特性。

结果槽以不抛异常的空状态初始化；执行要么构造有效结果，要么保存异常，
两种结果互斥。两种存储方式保持相同的资源释放及完成通知契约，
通过公共 API 验证结果对齐、转移和销毁。

内部 C++ 模板布局可能随源码版本调整。
更新后应一起重新编译所有使用封装的消费者／插件，
不能把旧头文件实例化的任务交给采用新布局编译的代码。
公共 C 结构和不透明句柄 ABI 保持不变。
实际大小对照见[验证记录](VALIDATION.zh-CN.md)。

## 链接和可选功能

`HX_OS_ENABLE_THREAD_POOL=OFF` 保留 C ABI，但创建返回 NOT_SUPPORTED，
C++ 工厂抛出相应的 StatusError；可先通过能力位决定是否创建。
关闭测试和示例、仅构建 C 库时，不需要 C++ 编译器。

所有任务、结果和取消引用释放前，分配器回调、任务函数、C++ 析构函数、
异常类型及其模块都必须仍然可用。持有任务引用不会让系统加载器自动保留插件。
多个 DLL 静态链接的库副本有独立状态，分配／释放必须通过对应实例，
C++ 对象还需要兼容的运行库。
通用线程 TLS 及其析构生命周期继续遵循[核心契约](CONTRACTS.zh-CN.md)。
