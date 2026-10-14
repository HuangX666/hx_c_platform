# 线程协作工具

[English](COORDINATION.md)

## 1. 三种独立工具

开启 `HX_OS_ENABLE_THREAD_EX` 即可使用。这些工具面向原生线程，不要求线程池、协程运行时或后台工作线程。

| 工具 | 用途 |
|---|---|
| 取消令牌 | 发布协作停止请求，唤醒支持取消的等待，或请求取消已经注册的任务。 |
| WaitGroup 等待组 | 统计尚未完成的任务，支持父任务尚未完成时加入子任务。 |
| 读写锁 | 允许多个读者并发读取，写入时独占保护。 |

取消只是请求，并不表示工作已经结束。不可取消的第三方阻塞函数不会被强制打断。等待组完成也不等于原生线程退出或 TLS 清理完成；释放线程状态之前仍要分别 join。

[完整协作示例](../examples/coordination.c) 启动两个工作线程，在写锁内更新共享记录数，然后请求取消、等待组归零、等待两个线程退出，最后在读锁内读取结果。复用现有已开启 examples 的构建目录，编译 `hx_os_coordination`。记录数随调度变化，以正常完成为预期，不固定某个数字。

## 2. 先登记数量，再发布任务

```c
hx_os_status status = hx_os_wait_group_add(group, 1, &error);
if (status == HX_OS_OK) {
	status = hx_os_thread_create(NULL, worker, state, &thread, &error);
	if (status != HX_OS_OK)
		(void)hx_os_wait_group_done(group, 1, NULL);
}

/* At the end of every worker path: */
(void)hx_os_wait_group_done(group, 1, NULL);
```

前半段在提交任务的线程执行；最后的 `done` 应放在工作函数内部，包括失败和取消退出路径。启动失败需要减回已经登记的数量。当前数量仍为正时，任务可以先加入子任务，再报告自己完成。

`hx_os_wait_group_wait_until(group, deadline, NULL, ...)` 等待当前这一轮归零。它不是提交时的任务快照，这一轮后续加入的子任务也要完成。超时保留计数和对象；下一轮必须等上一轮所有等待者都返回后再开始。计数下溢／上溢返回 `RANGE`，销毁仍有任务或等待者的组返回 `BUSY`。

向 wait 传入取消令牌，取消的是当前等待调用，不会自动取消计数中的任务。关闭时，应先向工作线程请求取消，再不带已取消令牌地等待组归零，确认工作实际结束。

## 3. 取消与订阅

令牌一旦取消就保持取消状态，不会复位；新一轮取消范围使用新令牌。默认预分配 64 个注册槽，可用 `max_callbacks` 指定其他有限容量。可取消等待也会临时占用注册槽。

- `hx_os_cancel_requested` 查询取消标志。
- `hx_os_cancel_wait_until` 在观察到取消请求时返回 `OK`，等待截止时返回 `TIMEOUT`。
- `hx_os_sleep_cancellable_until` 在取消时返回 `CANCELLED`，睡眠截止时返回 `OK`；无限期可取消睡眠必须传令牌。
- `subscribe` 返回注册 ID。首次请求取消的线程在令牌锁外同步执行回调，回调应只做简短的唤醒／请求操作，不应 join 或执行阻塞业务。
- 对已取消令牌注册会返回 `CANCELLED`，不调用回调，也不接管用户数据。
- `unsubscribe_until` 阻止新回调启动，并等待正在执行的回调结束。超时后 ID 和用户数据仍需保留，可以使用同一 ID 重试；回调等待自己结束会返回 `DEADLOCK`。

线程池任务、I/O 请求和协程的绑定继续遵循各自已有的取消契约。绑定成功后保留目标，直到成功退订；目标完成并不会自动删除注册。绑定不会创建分离线程来执行阻塞回调。另一个并发 `request` 可能先于首次请求的回调派发完成而返回。

## 4. 读写锁

使用 `hx_os_rwlock_read_until` 或 `hx_os_rwlock_write_until` 获取锁，也可以传取消令牌。获取后必须由同一个原生线程按同一模式解锁。已经排队的写者会阻止新读者进入，但不承诺严格 FIFO，也不保证绝无饥饿。

锁不可递归，不支持持有读锁时直接升级成写锁。需要升级时先释放，再获取写锁，并重新检查共享条件。读锁的线程所有权属于调用者契约，不为每个线程分配一张所有权表。这些是原生线程等待，不能单独挂起协程而释放工作线程；协程代码应使用对应的协程同步原语。

等待使用单调截止时间，但重新获取内部互斥锁、结束取消注册等操作可能超过指定时间。`NOW` 表示不等待条件满足，并不承诺硬实时完成。

## 5. C++11 便利类保留明确的宿主所有权

包含 `<hx/Coordination.hpp>`。`CancelToken`、`WaitGroup`、`ReadWriteLock` 与 `hx::Thread` 一样，使用宿主持有、初始为 NULL 的 owner 指针引用创建。

```cpp
hx_os_wait_group *groupOwner = NULL;
auto created = hx::WaitGroup::create(groupOwner);
if (!created)
	return 1;

auto &group = created.value();
auto added = group.add();
if (!added)
	return 1;

/* Publish work here; every accepted task must eventually call group.done(). */
```

把 owner 指针、便利类和工作数据保留在宿主生命周期状态中。便利类析构不等待，也不释放底层对象，需要显式 `close()`；`WaitGroup::closeUntil(deadline)` 会先等待，再在安全时销毁。`BUSY`、`TIMEOUT` 或 `CANCELLED` 后仍保留 owner，便于重试。关闭前必须排除新的调用者和任务提交。

`ReadWriteLock::readUntil`、`writeUntil` 返回 `Result<ReadWriteGuard>`。guard 离开作用域时解锁，只允许移动，并且必须始终处于获取锁的那个线程。所有 guard 释放前，锁的 owner 必须保持有效。显式 `guard.unlock()` 可以检查错误；guard 仍持锁时，`lock.close()` 返回 `BUSY`。

`CancelToken::subscribe(function, user)` 返回 `Result<uint64_t>`。保存该 ID，在释放回调数据前调用 `unsubscribeUntil(id, deadline)` 并确认成功。便利类不会隐藏分配的 lambda 所有者，也不会默默丢弃退订超时的注册。所有方法使用 `Result`，不使用异常。
