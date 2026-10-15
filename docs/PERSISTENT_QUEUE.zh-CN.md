# SQLite 持久化消息队列

[English](PERSISTENT_QUEUE.md) · [README](../README.zh-CN.md)

**先模拟三条需要补报的识别记录。** 使用包含 SQLite 后端的配置，构建 `hx_os_persistent_queue`。在 build 中先运行 `hx_os_persistent_queue publish records.db`，再运行 `hx_os_persistent_queue consume records.db`；程序路径按[运行说明](../examples/README.zh-CN.md#run)添加 bin／配置目录。

publish 把三条 JSON 写入 records.db；consume 输出三行 `Handle recognition-...` 并确认。再 consume 会等待约一秒后结束，已确认的消息不会重新出现。[persistent_queue.c](../examples/persistent_queue.c) 中，把打印位置替换成实际上传，只有平台确认成功后才 ack；上传失败时 release，不能打印完成就当作业务成功。

第一次练习用新的演示数据库。多个进程可以打开同一队列，但一次取出得到的是带期限的处理权；超时重投可能重复，业务应按稳定 ID 去重。

## 1. 写入、取出、确认

需要在断网或重启后继续处理的记录，可以放进这个队列。生产者给记录分配稳定 ID，写入 JSON 或任意字节；消费者取出消息，处理／上传成功后再确认。

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

把注释替换成真实处理逻辑，检查业务结果，成功后才 ack。业务失败而需要重试时调用 release。释放消息副本并不等于确认，也不会立即释放租约。

## 2. 可以运行的 JSON 业务示例

完整的 [persistent_queue.c](../examples/persistent_queue.c) 写入工厂和医院入口的识别记录：

```sh
cmake --build build-shared-linux --target hx_os_persistent_queue
build-shared-linux/bin/hx_os_persistent_queue publish records.db
build-shared-linux/bin/hx_os_persistent_queue consume records.db
```

在两个终端同时执行最后一条命令，可以观察多个消费者处理同一个队列。每次成功取出会领取一条记录；示例打印后确认。真正使用时，把打印换成上传，收到平台成功回复后再确认。Windows 添加 `--config Debug`，运行 `bin/Debug/` 下的程序。

C++11 的 [PersistentQueue.hpp](../include/hx/PersistentQueue.hpp) 提供 open、push、take、ack、release、renew、close，使用 Result 和自动持有的消息对象。

## 3. 交付规则与崩溃恢复

同一个本地队列文件支持多个进程和多个线程。默认处理期限为 30 秒；renew 延长有效领取，release 立即放回。进程崩溃或处理期限到期后，其他消费者能重新取到同一条 ID，但领取凭据会更新。旧凭据返回 CLOSED，不能删除后来领取的消息。

交付方式是至少一次。业务平台需要按稳定 ID 去重。仍在队列中的同一 ID、同一内容可以重复写入，视作同一次提交；同一 ID、不同内容返回 EXISTS。确认删除后，队列不会永久记住这个 ID。提交结果不确定时，使用原来的 ID 重试。

租约保存 UTC 毫秒数，系统时间调整会影响到期判断；本地等待截止时间仍基于单调时钟。队列文件放在本地文件系统，不作为网络共享数据库使用。

领取、确认和续租都在拿到 SQLite 写事务后读取时间。如果等待其他写入者释放锁期间租约已经到期，ack／renew 返回 CLOSED，记录仍在队列里；等待锁不会让旧凭据继续有效。续租成功时从这次取时开始计算新的期限。磁盘同步和函数返回仍会消耗剩余时间，因此成功不承诺返回时至少还剩多少租约时间。

## 4. 同步、批量与容量

单条写入／确认、每个批次都在事务中执行，请求同步完成后才返回成功。新数据库使用 DELETE 回滚日志和 synchronous EXTRA；实际断电保证还取决于操作系统、文件系统和硬件是否兑现同步请求。

push_many、ack_many 每批最多 1024 条，整批提交并统一同步。它与内存消息队列的“完成前缀”不同，是数据库事务中的整批行为。提交失败仍可能存在结果不确定的情况。

默认最多 65,536 条待处理消息，总正文 64 MiB，每条 1 MiB。容量满返回 LIMIT，不丢弃尚未确认的记录。容量在首次创建时保存；NULL options 继承已有容量，显式传入不一致容量会失败。SQLite 页、日志和取出副本的内存额外计算。删除后的页可以复用，但不保证文件立刻缩小。

## 5. 生命周期与原生调用

库不启动后台线程或网络服务。打开／写入／确认的数据库锁竞争等待预算为 5 秒，原生磁盘同步可能超过这个预算。take 不阻塞等待消息；take_until 用短睡眠重试空队列／数据库竞争，支持取消，但不能强制打断任意文件系统工作。

关闭前排除正在调用和新进入的线程；关闭失败保留句柄。消息副本独立持有正文，即使队列已关闭仍可读取，消息内借用的 ID／正文在销毁消息后失效。

PERSISTENT_QUEUE 可单独裁剪，需要 RANDOM、THREAD_EX。SQLite 使用自身分配器，本库对象使用指定 context。队列独立持有数据库结构和连接。直接调用 SQLite 时，通过 [hx_c_platform::sqlite](NATIVE_DEPENDENCIES.zh-CN.md) 创建业务自己的连接，不修改队列结构或日志策略。
