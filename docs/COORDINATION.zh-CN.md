# 线程协作工具

[English](COORDINATION.md)

当控制线程需要向多个后台工作线程广播停止请求、等待一轮并发任务全部完成，或者使用多读单写的读写锁保护共享状态时，可以组合使用本模块的取消令牌（`hx_os_cancel_token`）、等待组（`hx_os_wait_group`）和读写锁（`hx_os_rwlock`）。

### 快速运行示例

完整示例位于 [coordination.c](../examples/coordination.c)：在启动两个工作线程之前先向 `WaitGroup` 登记任务计数，工作线程在写锁保护下累加处理记录数，主线程随后发起取消请求、等待 `WaitGroup` 计数归零、`join` 两个工作线程，最后在读锁下读取统计结果并销毁所有协调对象。

按[示例运行方法](../examples/README.zh-CN.md#run)启用示例后，构建并运行 `hx_os_coordination`（无需命令行参数或输入文件）：

```sh
cmake --build build --config Release --target hx_os_coordination
```

预期输出格式为 `Processed ... records, then stopped cooperatively.`（具体记录数随操作系统线程调度而变化）。在自己的工程中接入时，可参考[最小工程配置](USAGE.zh-CN.md#c-project)直接使用该结构。

## 1. 三种独立工具

启用 `HX_OS_ENABLE_THREAD_EX` 即可使用以下三项原语。它们直接面向原生操作系统线程，不依赖线程池或协程运行时，也不会在后台隐式创建线程：

| 协调原语 | 核心用途 |
| --- | --- |
| **取消令牌（`hx_os_cancel_token`）** | 发布协作式停止请求，立即唤醒所有支持取消的等待操作，或触发已注册的取消回调 |
| **等待组（`hx_os_wait_group`）** | 统计尚未完成的并发任务数量，支持父任务在运行期间继续追加子任务计数 |
| **读写锁（`hx_os_rwlock`）** | 允许多个读线程并发读取共享数据，写线程更新时独占加锁 |

> [!IMPORTANT]
> **取消请求不等于任务已结束，WaitGroup 归零也不等于线程已退出**
> - 发出取消请求只是设置标志并唤醒可取消等待，无法强行打断阻塞在第三方不可取消系统调用中的线程。
> - `WaitGroup` 计数归零仅代表工作函数内调用了 `hx_os_wait_group_done()`，此时原生线程的栈帧收尾和 TLS 析构可能尚未跑完。在释放线程所使用的共享状态或销毁协调对象之前，必须先对所有工作线程完成 `hx_os_thread_join()`。

[完整协作示例](../examples/coordination.c) 演示了从登记计数、并发读写、请求取消到等待组归零与线程 `join` 的完整闭环。

## 2. 先登记数量，再发布任务

使用 `hx_os_wait_group` 时，务必遵循**“先在提交线程 `add`，再启动工作线程；若启动失败则立即 `done` 回滚”**的顺序：

```c
hx_os_status status = hx_os_wait_group_add(group, 1, &error);
if (status == HX_OS_OK) {
	status = hx_os_thread_create(NULL, worker, state, &thread, &error);
	if (status != HX_OS_OK)
		(void)hx_os_wait_group_done(group, 1, NULL);
}

// At the end of every worker path:
(void)hx_os_wait_group_done(group, 1, NULL);
```

- **动态派生子任务**：只要当前未完成计数仍然大于 `0`，正在运行的任务就可以继续调用 `hx_os_wait_group_add()` 增加子任务，然后再调用自己的 `hx_os_wait_group_done()`。
- **轮次等待与边界**：`hx_os_wait_group_wait_until(group, deadline, NULL, ...)` 等待当前轮次的计数彻底归零（包含本轮期间新加入的子任务）。若等待超时返回 `HX_OS_TIMEOUT`，计数与对象状态均保持不变，可供继续等待。如需复用同一个 `WaitGroup` 开启新一轮任务，必须等上一轮的所有等待者全部返回后再执行新的 `add`。计数上溢或下溢返回 `HX_OS_RANGE`；当仍有未完成计数或活跃等待者时调用销毁会返回 `HX_OS_BUSY`。
- **等待调用本身的取消令牌**：向 `hx_os_wait_group_wait_until()` 传入取消令牌时，该令牌**只用于取消当前这次 `wait` 等待调用本身**，不会自动取消组内正在运行的任务。因此在停机流程中，应当先触发业务工作线程的取消令牌，然后**不带已取消令牌**地调用 `hx_os_wait_group_wait_until()`，从而真正等到所有工作线程完成清理。

## 3. 取消与订阅

`hx_os_cancel_token` 采用单向置位设计：一旦调用 `hx_os_cancel_request()` 触发取消，该令牌就永久保持已取消状态（不可复位；若下一阶段需要新的取消范围，请创建新的令牌）。创建时默认预分配 `64` 个回调槽位（可通过 `max_callbacks` 自定义上限；各类 `*_cancellable_until` 等待在阻塞期间也会临时借用一个槽位）：

- **状态查询与可取消等待**：
  - `hx_os_cancel_requested(token)`：非阻塞查询是否已收到取消请求。
  - `hx_os_cancel_wait_until(token, deadline, error)`：等待取消信号发生；观察到取消时返回 `HX_OS_OK`，到达截止时间仍未取消则返回 `HX_OS_TIMEOUT`。
  - `hx_os_sleep_cancellable_until(deadline, token, error)`：可被取消唤醒的睡眠；被取消时返回 `HX_OS_CANCELLED`，正常睡满截止时间返回 `HX_OS_OK`（若传入 `HX_OS_DEADLINE_INFINITE`，则必须提供非空令牌）。
- **订阅取消回调（`subscribe` / `unsubscribe_until`）**：
  - `hx_os_cancel_subscribe()` 返回注册 ID。首次发起取消请求的线程会在令牌内部锁之外**同步调用**所有已注册回调，因此回调函数必须简短（仅做唤醒或设置标志，切勿在回调中执行 `join` 或耗时阻塞操作）。
  - 如果向一个**已经处于已取消状态**的令牌调用 `subscribe`，函数会立即返回 `HX_OS_CANCELLED`，既不会调用回调，也不会接管用户数据。
  - 在释放回调所引用的用户数据之前，必须调用 `hx_os_cancel_unsubscribe_until(token, id, deadline, error)`。它会阻止该回调被新启动，并等待正在执行中的回调返回；若返回 `HX_OS_TIMEOUT`，必须保留该 ID 和用户数据并稍后重试（在回调内部等待自身退订会返回 `HX_OS_DEADLOCK`）。

## 4. 读写锁

调用 `hx_os_rwlock_read_until()` 或 `hx_os_rwlock_write_until()` 获取读锁或写锁（均支持传入单调时钟截止时间和可选取消令牌）：
- **同线程同模式解锁**：获取锁后，必须由**同一个原生线程**调用对应的读解锁或写解锁。排队中的写者会阻止后续新读者插队。
- **不可递归、不支持直接升级**：读写锁不可递归加锁，也不支持在持有读锁的状态下直接升级为写锁（如需修改，请先释放读锁、再获取写锁，并在拿到写锁后重新检查业务条件）。
- **面向原生线程**：`hx_os_rwlock` 会阻塞底层操作系统线程，不能在协程中单独挂起协程而让出工作线程；协程内请使用 `<hx/CoroutineSync.hpp>` 中的协程版 `RwLock`。

## 5. C++11 便利类保留明确的宿主所有权

包含 `<hx/Coordination.hpp>` 即可使用无异常的 `hx::CancelToken`、`hx::WaitGroup` 和 `hx::ReadWriteLock`。它们与 `hx::Thread` 一致，均通过引用宿主持有的原生 `owner` 指针（初始为 `NULL`）创建：

```cpp
hx_os_wait_group *groupOwner = NULL;
auto created = hx::WaitGroup::create(groupOwner);
if (!created)
	return 1;

auto &group = created.value();
auto added = group.add();
if (!added)
	return 1;

// Publish work here; every accepted task must eventually call group.done().
```

- **显式关闭与重试**：C++ 便利类的析构函数不会暗中阻塞等待，也不会销毁底层对象。请在停止新任务后显式调用 `close()`（`WaitGroup::closeUntil(deadline)` 会先等待计数归零，成功后再销毁底层对象）。若返回 `HX_OS_BUSY`、`HX_OS_TIMEOUT` 或 `HX_OS_CANCELLED`，`owner` 指针保持有效，可供稍后重试。
- **RAII 读写锁守卫（`ReadWriteGuard`）**：`ReadWriteLock::readUntil()` 和 `writeUntil()` 返回 `Result<ReadWriteGuard>`。守卫仅支持移动，必须留在获取锁的同一线程并在离开作用域时自动解锁（也可显式调用 `guard.unlock()` 检查错误；当仍有守卫持锁时调用 `lock.close()` 会返回 `HX_OS_BUSY`）。
- **取消订阅 ID 管理**：`CancelToken::subscribe(function, user)` 返回 `Result<uint64_t>`。请保存该注册 ID，并在销毁回调所捕获的用户状态之前调用 `unsubscribeUntil(id, deadline)` 并确认返回 `HX_OS_OK`。
