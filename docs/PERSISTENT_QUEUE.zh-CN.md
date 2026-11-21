# SQLite 持久化消息队列

[English](PERSISTENT_QUEUE.md) · [README](../README.zh-CN.md)

当边缘设备或服务需要在断网、进程崩溃或系统重启后继续补报关键业务记录（如门禁识别记录、计费事件或遥测载荷）时，可以使用基于 SQLite 的本地持久化消息队列（`hx_os_persistent_queue`）。生产者为每条记录分配稳定的业务 ID 并落盘写入；消费者领取（`take`）记录后获得带期限的处理租约（Lease），仅在远端平台确认接收成功后才执行确认删除（`ack`）。

### 快速运行示例

启用示例与 SQLite 后端后（路径约定见[运行说明](../examples/README.zh-CN.md#run)），构建 `hx_os_persistent_queue` 并依次执行 `publish` 与 `consume`：

```sh
cmake --build build --config Release --target hx_os_persistent_queue
./build/bin/hx_os_persistent_queue publish records.db
./build/bin/hx_os_persistent_queue consume records.db
```

- `publish` 会将 3 条 JSON 识别记录写入本地 `records.db`。
- `consume` 会依次输出 3 行 `Handle recognition-...` 并调用 `ack` 确认删除。
- 如果再次运行 `consume`，程序会在等待约 1 秒后正常退出，已确认删除的消息不会重复出现。

在实际接入时，请打开 [persistent_queue.c](../examples/persistent_queue.c)，将打印语句替换为真实的 HTTP/MQTT 上传逻辑：只有在收到服务端成功响应后才调用 `ack`，上传失败需要稍后重试时调用 `release` 立即归还租约。

## 1. 写入、取出、确认

消费者从本地数据库取出一条消息、处理并确认的最小流程如下：

```c
hx_os_persistent_queue *queue = NULL;
hx_os_persistent_message *message = NULL;

hx_os_status status = hx_os_persistent_queue_open(
	NULL, "records.db", NULL, &queue, NULL);
if (status == HX_OS_OK)
	status = hx_os_persistent_queue_take_until(queue,
		hx_os_deadline_after_ms(1000), NULL, &message, NULL);

if (status == HX_OS_OK) {
	hx_os_bytes_view body = hx_os_persistent_message_data(message);

	printf("%.*s\n", (int)body.size, (const char *)body.data);
	// Replace printing with real work; acknowledge only on success.
	status = hx_os_persistent_queue_ack(queue, message, NULL);
}

hx_os_persistent_message_destroy(&message);
hx_os_persistent_queue_close(&queue, NULL);
```

> [!IMPORTANT]
> **销毁内存消息句柄（`destroy`）不等于业务确认（`ack`）**
> - `hx_os_persistent_message_destroy(&message)` 仅释放当前进程内存中的消息副本，**既不会删除数据库里的记录，也不会提前释放它的租约**。
> - 业务处理成功时，必须显式调用 `hx_os_persistent_queue_ack()` 将记录从数据库中删除。
> - 业务处理失败且希望立即允许重试时，调用 `hx_os_persistent_queue_release()` 将记录立即放回待领取状态（否则记录会等到租约到期后才重新变为可领取）。

## 2. 可以运行的 JSON 业务示例

完整示例 [persistent_queue.c](../examples/persistent_queue.c) 模拟了工厂和医院闸机入口的识别记录补报流程：

```sh
cmake --build build-shared-linux --target hx_os_persistent_queue
build-shared-linux/bin/hx_os_persistent_queue publish records.db
build-shared-linux/bin/hx_os_persistent_queue consume records.db
```

你可以在两个终端中同时执行最后一条 `consume` 命令，观察多进程并发消费同一个队列文件（Windows Visual Studio 构建请添加 `--config Debug` 并运行 `bin/Debug/` 下的可执行文件）。

C++11 便利层在 [PersistentQueue.hpp](../include/hx/PersistentQueue.hpp) 中提供了 `hx::PersistentQueue::open`、`push`、`take`、`ack`、`release`、`renew` 和 `close`，统一返回无异常的 `hx::Result` 并通过 RAII 自动释放内存消息对象。

## 3. 交付规则与崩溃恢复

同一个本地 SQLite 队列文件支持跨进程与跨线程并发访问：
- **租约（Lease）与凭据（Receipt）保护**：默认处理租约期限为 `30` 秒。消费者可以通过 `renew` 延长尚未过期的租约，或通过 `release` 立即归还。如果消费者进程崩溃或未能在 30 秒内完成 `ack`，其他消费者就能重新 `take` 到同一条消息 ID，并获得**新的领取凭据**。此时哪怕旧消费者恢复运行并拿着过期的旧凭据调用 `ack` 或 `renew`，也会收到 `HX_OS_CLOSED`，绝不会误删新消费者正在处理的记录。
- **至少一次交付（At-Least-Once）与幂等写入**：
  - 由于超时重投可能发生，接收端业务平台应当按稳定的消息 ID 做幂等去重。
  - 当某条消息 ID 仍在队列中待处理时，使用**相同 ID 和完全相同的正文**再次 `push` 会被视作幂等重试并返回 `HX_OS_OK`；若使用相同 ID 但传入**不同正文**，则返回 `HX_OS_EXISTS`。一旦消息被 `ack` 确认删除，队列不会永久保留历史已删 ID。
- **时钟与本地文件系统要求**：
  - 持久化的租约到期时间记录为 UTC 毫秒时间戳（因为单调时钟无法跨重启持久化），因此大幅调整系统墙钟时间会影响已领取租约的到期判断；而进程内的 `take_until` 等待截止时间仍严格基于单调时钟。
  - 队列数据库文件必须放在本地磁盘文件系统上，切勿放在 NFS/SMB 等网络共享挂载目录上。
- **写事务内取时规则**：`take`、`ack` 和 `renew` 都在成功获取 SQLite 写事务之后采样当前 UTC 时间。如果在等待其他写入者释放数据库锁的期间租约已经到期，`ack` / `renew` 会返回 `HX_OS_CLOSED` 且记录保留在队列中。

## 4. 同步、批量与容量

- **落盘持久性配置**：单条 `push` / `ack` 以及批量操作均在 SQLite 事务内执行，并请求磁盘同步完成后才返回 `HX_OS_OK`。新建数据库默认采用 `DELETE` 回滚日志模式与 `synchronous = EXTRA`（实际掉电不丢数据仍取决于操作系统、文件系统和磁盘硬件是否正确兑现刷盘指令）。
- **整批事务提交（`push_many` / `ack_many`）**：每批最多支持 `1,024` 条记录。与内存消息队列的“保留已完成前缀”不同，持久化队列的批量操作属于**单个数据库原子事务**——整批一次性提交并同步。若提交因 I/O 错误返回失败，事务结果可能处于不确定状态，生产者应使用原消息 ID 重试整批提交。
- **有界容量保护**：默认上限为最多 `65,536` 条待处理消息、总正文 `64 MiB`、单条正文最大 `1 MiB`。当队列达到上限时，新写入返回 `HX_OS_LIMIT`，绝不会自动覆盖或淘汰尚未确认的旧记录。容量配置在数据库首次创建时写入持久化元数据；后续打开时传入 `NULL` 选项会自动继承已有容量，显式传入不一致的容量配置会被拒绝。

## 5. 生命周期与原生调用

- **无后台线程与锁竞争预算**：本模块不启动任何后台线程或网络服务。`open`、`push` 和 `ack` 的 SQLite 锁竞争等待预算为 `5` 秒（底层磁盘同步耗时可能超出该值）。`take` 为非阻塞调用；`take_until` 通过有界短睡眠重试空队列或锁竞争，并支持取消令牌（但无法强行打断正在执行的底层文件系统系统调用）。
- **关闭与内存句柄独立**：调用 `hx_os_persistent_queue_close()` 前，请确保没有其他线程正在调用或即将进入该队列句柄（关闭失败时保留原句柄）。已取出的 `hx_os_persistent_message` 独立持有自己的正文字节副本，即使队列已关闭也依然可以安全读取；但从消息对象借用的 ID 字符串与 `hx_os_bytes_view` 视图会在调用 `hx_os_persistent_message_destroy()` 后失效。
- **裁剪与原生 SQLite 共用**：模块可通过 `HX_OS_ENABLE_PERSISTENT_QUEUE` 独立开关（依赖 `HX_OS_ENABLE_RANDOM` 与 `HX_OS_ENABLE_THREAD_EX`）。队列独占管理自身的表结构与 SQLite 连接；如果业务代码也需要直接使用 SQLite，请通过 [hx_c_platform::sqlite](NATIVE_DEPENDENCIES.zh-CN.md) 创建独立的业务数据库连接，不要直接修改队列内部表结构或日志模式。
