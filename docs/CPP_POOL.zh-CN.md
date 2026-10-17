# C++11 线程池便利层

[English](CPP_POOL.md) · [README](../README.zh-CN.md) · [Result 契约](CPP_RESULTS.zh-CN.md)

**先运行一个完整例子。** 把独立计算交给固定数量的后台线程，并在稍后取得结果时，使用线程池。

打开 [thread_pool.cpp](../examples/thread_pool.cpp)。withThreads(owner, 2) → submit 把 21 交给任务 → getUntil 取得计算结果 → shutdownUntil 关闭；逐步检查 Result。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 examples，并只构建 `hx_os_cpp_thread_pool`。此例无命令行参数、无需输入文件。

**会得到什么：**输出 `Task result: 42`。把 lambda 换成自己的工作；捕获的引用必须活到任务完成，关闭超时后继续保留 owner。

下面的短片段用于解释接口；复制运行时以完整示例为准。也可按[最小工程配置](USAGE.zh-CN.md#cpp-project)把完整源码作为自己的 main。

`hx/ThreadPool.hpp` 在有界 C 线程池上提供仅头文件的 C++11 接口，链接 `hx_c_platform::cpp`。调度器和 C ABI 继续使用 C99。便利层采用显式结果，支持关闭异常编译，协程是[独立模块](COROUTINES.zh-CN.md)，不需要为了普通线程池任务启用它。

## 所有权与关闭

`ThreadPool` 是宿主持有的 `hx_os_thread_pool *` 变量的视图。该变量必须比所有视图活得更久，调用期间不能并发修改它；复制视图仍引用同一个 owner。视图析构不关闭、不无限等待、不分离线程，宿主最终负责关闭并销毁原生 owner。

`Task<T>` 和取消源自动释放自己的引用。Task 可以比成功销毁的线程池活得更久，保留的结果可能使分配 context 继续忙碌。视图不会隐藏超时或忙碌后用于重试的 owner。

[完整示例](../examples/thread_pool.cpp) 分别检查创建、提交、结果与关闭。两秒截止时间属于消费者策略。服务程序应将原生 owner 保留在长期存在的关闭状态中，停止新提交后使用新的单调截止时间重试。`shutdownUntil()` 合并关闭和销毁，仅成功时清空 owner；调用者必须排除新调用和同时发生的最终销毁。

`closeUntil()` 开始后永久拒绝新提交。DRAIN 执行已接收的排队任务；CANCEL_PENDING 跳过排队任务，不强制打断正在运行的工作。两者均等待清理与原生线程退出。工作线程关闭自己的池返回 DEADLOCK，并发关闭或仍在准备的提交可能返回 BUSY，不引入后台回收线程或强制终止。

工作线程退出和释放线程句柄是两个步骤。如果 `destroy()` 或 `shutdownUntil()` 返回原生清理错误，保留同一个非空 owner，处理错误后重试。已经释放的句柄会跳过，不会重新执行任务或任务清理回调。C 接口 `hx_os_thread_pool_destroy()`、`hx_os_thread_pool_shutdown()` 遵循相同规则。

如果部分工作线程启动失败，而且回滚清理没有完成，创建也可能保留一个处于关闭状态的 owner。即使 `create()`／`withThreads()` 返回失败的 Result，也要检查 `owner`；用 `hx::ThreadPool(owner).shutdownUntil(deadline)` 重试清理，不能继续提交任务。创建错误会保留，具体规则见[通用生命周期约定](CONTRACTS.zh-CN.md)。

## 等任务完成，线程池继续使用

`pool.waitUntil(deadline)` 等待本次调用在池互斥锁内确定边界时已经接受的任务，后来提交的任务不延长本次等待。`pool.waitIdleUntil(deadline)` 等待池中没有尚未完成的任务，等待期间继续提交可能延长等待。`waitFor(milliseconds)` 和 `waitIdleFor(milliseconds)` 接受相对时长。这两种等待都包含延迟任务和任务清理，不关闭池；到达单调截止时间返回 TIMEOUT，零时长表示立即查询。

```cpp
auto first = pool.submit([]() noexcept { return 21 * 2; });
if (!first)
	return hx::Failure(first.error());

auto completed = pool.waitFor(std::chrono::milliseconds(2000));
if (!completed)
	return completed;

// Waiting leaves the pool available for the next batch.
auto second = pool.submit([]() noexcept { return 7 * 3; });
```

这段代码放在返回 `hx::Result<void>` 且持有有效 `pool` 的函数中。使用 `second` 前仍要检查提交结果，每个任务的执行结果通过 `getUntil()` 获取：池等待成功只代表任务和清理已完成，也包含执行失败或被取消的任务，不代表所有任务都执行成功。最终不再使用时仍需显式关闭池。

多个外部线程可以同时等待。本池工作线程（包括清理回调）调用这两个入口返回 DEADLOCK。尚在准备、未正式接受的任务不属于等待范围。超时后重新调用快照等待，会重新确定任务边界；空闲成功只是观察到某一时刻没有未完成任务，并不禁止随后提交。最终销毁时须排除新调用。内部提交序号耗尽后，新提交返回 LIMIT，不发生回绕。

C 对应入口是 `hx_os_thread_pool_wait_until()` 和 `hx_os_thread_pool_wait_idle_until()`。提交和等待不新增内存分配；每个已接受任务增加两个链表指针和一个 64 位序号，完成时额外获取一次池互斥锁以移出未完成列表。每个池增加一个完成条件变量，防止等待完成的线程占用唤醒工作线程的通知。这些是正确性措施，不作为性能提升的结论。

## 提交与结果

`submit(callable, args...)` 返回 `Result<Task<T>>`，接受普通函数、成员指针、lambda、可移动捕获和参数以及 `std::ref`。存储的函数按左值调用，参数移动到调用中。函数对象的调用必须声明 `noexcept`，参数和捕获的构造、析构必须不失败。C++11 的普通函数和成员函数指针无法把该保证编码进类型，其实现仍须遵守相同契约。

| 任务返回值 | 提交结果 | 获取结果 |
| --- | --- | --- |
| T | Result<Task<T>> | Result<T> |
| Result<T> | Result<Task<T>> | Result<T>，保留原生错误及部分结果 |
| void 或 Result<void> | Result<Task<void>> | Result<void> |
| T& | Result<Task<T&>> | Result<T&>，借用外部存储 |

返回普通状态整数的任务产生 `Task<hx_os_status>`，整数是任务值。如果需要它表示执行成功或失败，应返回 `Result<void>`。

| 方法 | 契约 |
| --- | --- |
| valid() | 是否持有原生任务引用 |
| ready() | 不阻塞查询完成状态，包含捕获清理完成 |
| waitUntil(deadline) | Result<void>；库的单调绝对截止时间 |
| waitFor(milliseconds) | Result<void>；零或负时长表示轮询 |
| wait() | Result<void>；显式无限等待 |
| getUntil(deadline) | Result<T>；等待失败保留任务，已完成时消费结果 |
| get() | Result<T>；显式无限等待 |
| cancel() | 请求协作取消 |
| cancellationSource() | 返回供控制线程使用的独立引用 |

等待成功仅表示工作完成；任务是否成功由 `getUntil()` 或 `get()` 判断。同池工作线程等待未完成任务返回 DEADLOCK；超时保留任务。已完成的失败和排队取消与成功一样会消费观察句柄。销毁 Task 仅释放观察引用，不取消工作。

`Result<T>` 内联存储值，自身不分配内存。先检查 `ok()`，或在明确允许部分结果的接口上检查 `hasValue()`，再读取 `value()`；`get()` 返回可空指针。引用结果必须比消费过程活得更久，不能指向已在工作线程清理阶段销毁的捕获或参数。

## 取消

下列函数接收宿主持有的线程池视图，返回独立控制句柄。控制线程保留该句柄，任务独立执行。

~~~cpp
#include <hx/ThreadPool.hpp>

hx::Result<hx::CancellationSource>
start_work(hx::ThreadPool &pool) noexcept
{
	auto submitted = pool.submitCancelable(
		[](hx::CancellationToken token) noexcept -> hx::Result<void> {
			while (!token.stopRequested()) {
				// Perform one bounded unit of work.
				hx_os_cpu_relax();
			}
			return token.check();
		});

	if (!submitted)
		return hx::Failure(submitted.error());

	return submitted.value().cancellationSource();
}
~~~

`CancellationToken::check()` 在收到停止请求后返回 CANCELLED。取消源和 token 保留原生引用，可以比线程池活得更久。将 Task 移交等待线程前先复制取消源；不能同时对同一个 Task 执行方法调用、移动、消费或析构。不同且稳定的取消源可以与执行、等待并发请求停止。取消无法打断任意阻塞用户代码。

## 容量、分配与清理

准备式提交先预留队列容量，再构造 C++ 任务。满队列或已关闭队列不会复制函数对象或分配 C++ 任务。准备在池互斥锁之外执行，分配失败返回 NO_MEMORY 并回收部分资源。用户捕获的复制、移动必须不失败；可能失败的状态分配应在提交前完成并显式检查。

如果关闭与已成功准备的提交竞争，提交返回 CLOSED，提交线程执行一次清理；已经移走的输入无法还原。已接收的捕获由工作线程清理一次，包括被跳过的排队取消任务。拒绝后的清理完成前仍占用对应容量。

函数对象、结果和引用状态共用一次按真实对齐分配的 context 内存；原生任务和事件的分配独立存在。默认内联任务大小上限为 1 MiB，`ThreadPool(owner, maxTaskBytes)` 可调整。限制的是任务对象尺寸，不限制用户自行持有的堆对象图；保留大量 Task 或结果会在队列排空后继续持有内存。本库不更换全局 C++ 分配器。

捕获和结果的析构可能在工作线程、拒绝提交的线程或释放最后一个引用的线程执行，必须正常返回，避免无界等待。库的截止时间无法抢占析构；用户回调不能通过栈展开或跳转越过 C 调用帧。

## 对齐、重新编译与裁剪

结果和捕获存储使用 C++11 `alignas`，任务按真实对齐通过 context 分配。测试覆盖 16～128 字节对齐的结果、可移动值、引用、回滚与析构。布局变小本身不能证明运行更快。

此次迁移改变 C++ 源码签名和模板布局，消费者、插件需要一起重新编译，不能跨模块交换旧版与新版 C++ Task 对象。公开 C 结构体、原生不透明句柄和导出函数保持不变。

关闭 THREAD_POOL 后，工厂返回 NOT_SUPPORTED，能力位清除。关闭测试和示例时，纯 C 库不要求 C++ 编译器。导出的 C++ 目标只要求 C++11，不把异常编译选项强制传递给消费者其它目标；选项见 [Result 指南](CPP_RESULTS.zh-CN.md)。

分配器代码、任务函数、析构函数、借用数据及所在模块必须保持有效，直到所有任务、结果和取消引用释放。任务引用不自动阻止插件卸载。不同静态库副本维护各自状态，应通过创建资源的同一实例释放。
