# C++11 线程池便利层

[English](CPP_POOL.md) · [README](../README.zh-CN.md) · [Result 契约](CPP_RESULTS.zh-CN.md)

当你需要将独立计算或后台任务交给固定数量的工作线程并发执行，并在稍后按截止时间获取返回值或错误时，可以使用 `hx/ThreadPool.hpp`。它在有界 C99 线程池之上提供了仅头文件的 C++11 封装：链接 `hx_c_platform::cpp` 即可使用，全程通过 `hx::Result<T>` 返回状态，支持 `-fno-exceptions` / `/EHs-c-` 无异常构建。底层调度器与公开 C ABI 保持纯 C99；协程属于[独立模块](COROUTINES.zh-CN.md)，普通线程池任务无需启用协程。

### 快速运行示例

完整示例位于 [thread_pool.cpp](../examples/thread_pool.cpp)：创建包含 2 个工作线程的线程池，向后台任务传入参数 `21`，通过 `getUntil()` 等待并取出计算结果，最后调用 `shutdownUntil()` 显式关闭线程池。

按[示例运行方法](../examples/README.zh-CN.md#run)启用示例后，单独构建并运行 `hx_os_cpp_thread_pool`（无需命令行参数或输入文件）：

```sh
cmake --build build --config Release --target hx_os_cpp_thread_pool
```

预期输出：

```text
Task result: 42
```

在实际工程中，可以直接将 [thread_pool.cpp](../examples/thread_pool.cpp) 作为模板，按[最小工程配置](USAGE.zh-CN.md#cpp-project)接入你的 `main.cpp`。

## 所有权与关闭

`hx::ThreadPool` 是宿主持有的 `hx_os_thread_pool *` 原生指针的轻量视图（View）：
- **宿主持有生命周期**：底层的 `owner` 变量必须比所有 `ThreadPool` 视图活得更久，且在调用线程池接口期间不得并发修改 `owner` 变量本身。复制 `ThreadPool` 视图仍指向同一个 `owner`。
- **析构不暗中阻塞**：`ThreadPool` 视图析构时不会关闭线程池、不会无限等待，更不会 `detach` 或强杀后台线程。宿主必须在退出前显式关闭并销毁原生 `owner`。
- **任务句柄独立引用计数**：`Task<T>` 和 `CancellationSource` 会自动管理各自的底层引用计数。`Task<T>` 可以比已成功销毁的线程池活得更久（但未释放的结果对象仍会占用对应的分配器 `context`）。

[完整示例](../examples/thread_pool.cpp) 分别检查了创建、提交、获取结果和关闭四个阶段。示例中的 2 秒截止时间只是演示策略；在长期运行的服务中，应将 `owner` 保存在宿主状态里，停止接收新任务后，使用新的单调时钟截止时间重试关闭。`shutdownUntil()` 会依次执行关闭与销毁，且**仅在完全成功时将 `owner` 置空**；调用最终销毁前，调用方必须确保没有其他线程正在发起新调用或并发销毁。

`closeUntil()` 一旦开始，线程池就会永久拒绝新提交，并提供两种关闭模式：
- `HX_OS_POOL_DRAIN`：继续执行所有已入队的任务，直到队列排空。
- `HX_OS_POOL_CANCEL_PENDING`：跳过队列中尚未开始的任务（仍会执行其清理回调），但不会强制打断正在运行中的任务。

两种模式都会等待任务清理完成以及工作线程安全退出。如果工作线程尝试关闭自己所在的线程池，会立即返回 `HX_OS_DEADLOCK`；若存在并发关闭或尚在准备阶段（prepare）的提交，可能返回 `HX_OS_BUSY`。库不会在后台暗中启动回收线程或强杀业务线程。

> [!IMPORTANT]
> **为什么 `shutdownUntil()` 返回 `HX_OS_TIMEOUT` 或清理错误时必须保留 `owner`？**
> 只要后台工作线程尚未完全退出，它就可能仍在访问线程池状态或任务捕获的数据。如果在超时后直接丢弃或强行释放内存，后台线程就会触发释放后使用（Use-After-Free）。因此：
> - 工作线程退出与底层线程句柄回收是两个独立步骤。若 `destroy()` 或 `shutdownUntil()` 返回 `HX_OS_TIMEOUT`、`HX_OS_BUSY` 或原生清理错误，请原样保留非空的 `owner` 指针以及任务借用的数据，稍后用新的截止时间再次调用 `shutdownUntil()` 重试（已释放的线程句柄会自动跳过，已完成的任务和清理回调不会重复执行）。C 接口 `hx_os_thread_pool_destroy()` 与 `hx_os_thread_pool_shutdown()` 遵循完全相同的规则。
> - 即使 `create()` 或 `withThreads()` 创建失败（例如部分工作线程启动失败且回滚清理未能在截止时间内完成），`owner` 也可能非空并处于正在关闭状态。此时不要提交任务，请检查 `owner` 并通过 `hx::ThreadPool(owner).shutdownUntil(deadline)` 完成回收（详见[通用生命周期约定](CONTRACTS.zh-CN.md)）。

## 等任务完成，线程池继续使用

如果希望等待当前批次的任务执行完毕，但**不关闭线程池**以便继续提交下一批工作，可以使用以下两类等待接口：
- **批次快照等待（`waitUntil(deadline)` / `waitFor(duration)`）**：在获取池互斥锁的瞬间记录当前已接收任务的边界，只等待这些任务完成；等待期间新提交的任务不会拉长本次等待。
- **全池空闲等待（`waitIdleUntil(deadline)` / `waitIdleFor(duration)`）**：等待池中所有已接收任务全部完成（观察到瞬时空闲状态）；等待期间若持续有新任务提交，等待时间会相应顺延。

两类接口都会等待延迟任务（delayed tasks）及其清理回调执行完毕，到达单调时钟截止时间时返回 `HX_OS_TIMEOUT`（传入零时长表示非阻塞轮询）：

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

这段代码位于返回 `hx::Result<void>` 且持有有效 `pool` 的函数中。继续使用 `second` 前仍应检查其提交状态；池级等待成功只代表“任务及清理回调已执行完毕”（包含执行失败或被取消的任务），单个任务的业务返回值或错误仍需通过各自 `Task<T>` 的 `getUntil()` 读取。

多个外部线程可以并发调用等待接口。如果在同一线程池的工作线程（或任务清理回调）内部调用这两个接口，会返回 `HX_OS_DEADLOCK`。尚在锁外准备（prepare）、未正式入队的任务不计入等待范围。快照等待超时后再次调用会重新捕获当前边界；空闲等待成功仅代表观察到某一时刻无未完成任务，不会阻止后续提交。若内部 64 位提交序号耗尽，新提交会返回 `HX_OS_LIMIT` 而不会发生整数回绕。对应的 C 接口为 `hx_os_thread_pool_wait_until()` 和 `hx_os_thread_pool_wait_idle_until()`。

## 提交与结果

`submit(callable, args...)` 返回 `Result<Task<T>>`，支持普通函数、成员函数指针、lambda、只移动（move-only）捕获与参数，以及 `std::ref`。存储的可调用对象按左值调用，存储的参数以右值移动方式传入。

> [!NOTE]
> **无异常调用契约**：函数对象和 lambda 的 `operator()` 必须声明为 `noexcept`，捕获变量和参数的构造与析构也必须保证不抛异常。C++11 的普通函数指针和成员函数指针无法在类型签名中携带 `noexcept`，但实现代码同样必须遵守不抛异常的契约。

| 任务返回值类型 | `submit()` 返回类型 | `get()` / `getUntil()` 返回类型与语义 |
| --- | --- | --- |
| `T` | `Result<Task<T>>` | `Result<T>` |
| `Result<T>` | `Result<Task<T>>` | `Result<T>`（保留完整 `hx_os_error` 及部分完成值） |
| `void` 或 `Result<void>` | `Result<Task<void>>` | `Result<void>` |
| `T&` | `Result<Task<T&>>` | `Result<T&>`（借用外部存储，被引用对象须活过消费期） |

如果任务直接返回普通的 `hx_os_status` 整数，会生成 `Task<hx_os_status>`（把该状态码当作普通返回值）；若希望将其作为任务成败状态传播，请让任务返回 `Result<void>`。

`Task<T>` 提供以下方法：

| 方法 | 行为与契约 |
| --- | --- |
| `valid()` | 是否持有有效的底层原生任务引用 |
| `ready()` | 非阻塞查询任务及捕获清理是否均已完成 |
| `waitUntil(deadline)` | 返回 `Result<void>`；等待至库的单调时钟绝对截止时间 |
| `waitFor(milliseconds)` | 返回 `Result<void>`；等待相对时长（零或负时长表示立即轮询） |
| `wait()` | 返回 `Result<void>`；显式无限期等待任务完成 |
| `getUntil(deadline)` | 返回 `Result<T>`；等待超时或失败时保留 `Task` 可供重试，任务已完成时消费句柄并返回结果 |
| `get()` | 返回 `Result<T>`；显式无限期等待并消费结果 |
| `cancel()` | 发起协作式取消请求 |
| `cancellationSource()` | 返回独立的 `CancellationSource` 控制句柄，可交给控制线程持有 |

注意：`wait*()` 返回成功仅代表任务已结束，任务本身是成功、失败还是被取消，需要通过 `getUntil()` 或 `get()` 检查。同一线程池的工作线程等待未完成任务会返回 `HX_OS_DEADLOCK`。一旦任务完成（无论是成功、返回错误还是在队列中被取消），调用 `getUntil()` / `get()` 都会消费该 `Task` 句柄；直接析构 `Task` 只会释放观察引用，不会取消正在运行的后台任务。

`Result<T>` 采用内联存储，自身不产生额外堆分配。读取 `value()` 前必须先检查 `ok()`（或在支持部分完成结果的接口上检查 `hasValue()`）；也可以使用 `get()` 获取可空指针。若任务返回 `T&` 引用，被引用的外部对象必须活过结果消费阶段，切勿返回指向任务局部捕获或参数（会在工作线程清理阶段析构）的引用。

## 取消

使用 `submitCancelable()` 可以向任务传入 `hx::CancellationToken`，并通过返回的 `Task` 提取独立的 `hx::CancellationSource` 留给控制线程：

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

当控制线程调用 `source.requestStop()` 后，任务内的 `token.stopRequested()` 会变为 `true`，调用 `token.check()` 会返回 `HX_OS_CANCELLED`。

- **生命周期独立**：`CancellationSource` 和 `CancellationToken` 内部持有原生任务引用计数，可以安全地比线程池活得更久。
- **并发使用边界**：不要在多个线程上同时对同一个 `Task` 对象执行方法调用、移动、结果消费或析构。如果需要在一个线程等待 `Task`、在另一个线程发起取消，请先在移交 `Task` 之前调用 `task.cancellationSource()` 复制出独立的 `CancellationSource`；不同的 `CancellationSource` 副本可以在任务执行或等待期间并发调用 `requestStop()`。
- **协作式语义**：取消只能被周期性检查或可取消等待感知，无法强行打断阻塞在第三方系统调用中的用户代码。

## 容量、分配与清理

C++ 线程池通过底层 `hx_os_thread_pool_submit_prepared()` 提交任务：
1. **先预留槽位，再分配构造**：先检查并预留队列容量；如果队列已满或已关闭，会直接拒绝提交，不会复制可调用对象或分配 C++ 任务内存。
2. **锁外准备（Prepare）**：C++ 任务对象的内存分配与构造在池互斥锁之外执行。若内存分配失败，立即返回 `HX_OS_NO_MEMORY` 并回收部分资源。
3. **关闭竞争清理**：如果准备阶段已完成，但在入队瞬间遇到线程池并发关闭，提交会返回 `HX_OS_CLOSED`，并由当前提交线程立即执行一次清理（注意：此时已移入任务的参数已被消费，无法还原回调用方原变量）。一旦任务被正式接收，无论正常执行还是作为排队任务被取消，其捕获对象都保证由工作线程恰好清理一次。

可调用对象、内联结果槽位和引用状态合并在同一次按真实对齐（`alignas`）分配的 `context` 内存中（底层原生任务与事件对象独立分配）。单个内联任务对象的默认大小上限为 `1 MiB`，可通过 `ThreadPool(owner, maxTaskBytes)` 自定义。该上限仅约束内联任务结构体本身的大小，不限制用户自行在堆上分配的对象图；如果在队列排空后仍长期持有大量 `Task` 或未消费的结果句柄，对应的 `context` 内存也会继续保留。

> [!WARNING]
> **析构函数与回调的线程安全要求**
> 任务捕获和结果对象的析构函数可能在**后台工作线程**、**提交被拒时的调用线程**，或**释放最后一个引用的线程**上执行。析构函数必须快速正常返回，不得包含无界阻塞，且绝对不能通过异常栈展开（unwind）或 `longjmp` 跨越 C 调用帧。

## 对齐、重新编译与裁剪

- **高对齐类型支持**：结果槽位和捕获存储使用 C++11 `alignas`，通过绑定的 `hx_os_context` 按对象的实际对齐要求分配内存，支持 `16`～`128` 字节对齐的值类型、只移动类型和引用。
- **C++ 重编译边界**：公开 C 结构体、不透明句柄和导出函数保持稳定的 C99 ABI；但 C++ 模板包装层属于源码级接口，升级头文件后请将主程序与共享 C++ `Task` 对象的插件一起重新编译，不要跨模块传递不同版本头文件编译出的 C++ 包装对象。
- **模块裁剪与插件生命周期**：若构建时关闭了 `HX_OS_ENABLE_THREAD_POOL`，工厂函数会返回 `HX_OS_NOT_SUPPORTED` 且对应能力位清零。`hx_c_platform::cpp` 仅要求 C++11，不会向消费方目标强加异常编译开关（无异常配置方法见 [Result 指南](CPP_RESULTS.zh-CN.md)）。在卸载动态库/插件或销毁自定义 `hx_os_context` 之前，必须确保所有关联的 `Task`、结果对象和取消源引用均已彻底释放。
