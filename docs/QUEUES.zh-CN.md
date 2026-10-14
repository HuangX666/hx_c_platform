# 线程消息队列与并发环形队列

[English](QUEUES.md) · [固定字节环形缓冲](RING.zh-CN.md) · [README](../README.zh-CN.md)

## 1. 选择入口

| 需求 | 接口 | 行为 |
| --- | --- | --- |
| 工作线程传递完整消息，没有数据或空间时休眠等待 | `hx_os_message_queue` | 有界、有锁 MPMC，支持截止时间和关闭后排空 |
| 已有工作循环自行决定满队列、空队列时怎么办 | `hx_os_ring_queue` | 有界元素环形队列，提供 `try_push`、`try_pop` |
| 单线程在调用者提供的内存里解析字节 | `hx_os_ring` | 固定字节数组，支持显式借用两个连续片段 |
| 单线程积累需要按需增长的字节流 | `hx_os_byte_queue` | 有上限的动态存储，支持完整与部分字节读写 |

线程消息队列与并发环形队列使用 `HX_OS_ENABLE_QUEUES` 开关；字节容器使用 `HX_OS_ENABLE_CONTAINERS`。`hx_os_queues_available()` 查询是否编入线程队列模块。队列不会创建工作线程、协程运行时或后台任务。

两种线程队列都在创建时准备存储，发送、接收、入队、出队不会逐次分配堆内存。容量单位是消息个数，不是字节。每条消息长度在创建时固定。消息按完整字节值复制；结构体中的指针不会自动深拷贝，也不会自动转移资源释放责任。建议使用普通业务记录，带指针时由应用另行规定所有权交接。需要构造或析构的 C++ 对象不能直接按原始字节发送。

## 2. 生产线程交给处理线程

完整的 [C 设备示例](../examples/message_queue.c) 创建一个最多保存两条 `device_sample` 的队列，启动一个生产线程，主线程处理六条测量数据。生产者只调用一次 `send_many_until()` 提交六条记录；消费者用可保存三条记录的数组，批量接收当前已经可用的完整记录。队列最多只能保存两条，比两端的批次数量都小：发送会等待消费者腾出空间，接收不会等待输出数组凑满。生产者完成后关闭队列。消费者继续取完已接收的消息，直到 `HX_OS_EOF`，随后等待生产线程退出并释放资源。

```c
typedef struct device_sample {
	uint32_t device_id;
	uint32_t sequence;
	double value;
} device_sample;

/* queue was created for sizeof(device_sample); main owns its lifetime. */
device_sample sample = {17, 1, 20.5};
hx_os_status status = hx_os_message_queue_send_until(
	queue, &sample, hx_os_deadline_after_ms(1000), NULL);

if (status != HX_OS_OK)
	fprintf(stderr, "Send failed: %s\n", hx_os_status_name(status));
```

接收线程提供另一份 `device_sample`，调用 `hx_os_message_queue_receive_until()`。它会收到完整记录，或者得到失败状态，不需要自己拼接半条消息。发送成功返回前，记录已经复制完成，因此生产者可以立即复用栈上的这份记录。这是进程内线程通信，不是 JSON 或网络协议。

开启 examples 后，构建 `hx_os_message_queue`，运行配置对应 `bin` 目录下的程序。输出从 `Device 17, sample 1: 20.5` 到 `Device 17, sample 6: 23.0`。示例会报告操作错误。若有限时间的 join 无法完成，静态工作状态及资源句柄继续有效，不会释放线程仍使用的对象。在长期运行的应用里，应保留这些状态，由负责关闭的模块继续重试 join，成功后才能清理。

## 3. 等待、关闭与所有权

| 情况 | 状态与影响 |
| --- | --- |
| `HX_OS_DEADLINE_NOW` 且暂时不能操作 | `HX_OS_WOULD_BLOCK`，不会消费半条消息 |
| 单调时钟截止时间到期 | `HX_OS_TIMEOUT`；单条调用不转移消息，批量发送则保留已完成的前缀 |
| 无限等待 | 支持 `HX_OS_DEADLINE_INFINITE`，close 可以唤醒等待者 |
| 关闭后发送 | `HX_OS_CLOSED` |
| 关闭后接收，仍有消息 | 正常返回下一条已接收的完整消息 |
| 关闭后接收，已经取空 | `HX_OS_EOF` |
| 再次关闭 | 成功，并通知所有等待者 |

等待时不要持有对端完成工作必需的业务锁。获取互斥锁或条件等待后重新上锁，可能超过截止时间，因此这里不是硬实时保证。等待线程的调度顺序不保证 FIFO。`size()` 返回受同步保护的瞬时快照，返回后可能马上变化；是否成功应看发送、接收结果，而不是先查 size 再作推断。

关闭顺序为 **close → 取完已接收消息 → join 所有使用者 → destroy**。`close()` 可以与收发并发。销毁必须从外部排除所有调用者，包括正准备进入操作的线程；检测到活跃等待时返回 `HX_OS_BUSY`，但这个检查不能替代资源所有者的协调。销毁非空队列会丢弃剩余的字节副本，不会释放结构体指针引用的资源。join 超时后，应用仍然持有队列与工作线程状态。

## 4. SPSC、MPSC 与 MPMC 环形队列

`hx_os_ring_queue_create(context, item_bytes, capacity, mode, backend, &queue, error)` 显式选择生产者/消费者约束与实现方式：

| 模式 | 允许的调用者 | `HX_OS_QUEUE_LOCKED` | `HX_OS_QUEUE_ATOMIC` |
| --- | --- | --- | --- |
| `HX_OS_QUEUE_SPSC` | 一个生产者、一个消费者 | 基于互斥锁的尝试操作 | 有无锁进展保证的 SPSC 算法 |
| `HX_OS_QUEUE_MPSC` | 多个生产者、一个消费者 | 基于互斥锁的尝试操作 | 原子槽位预留，不使用互斥锁，但不是严格 lock-free |
| `HX_OS_QUEUE_MPMC` | 多个生产者、多个消费者 | 基于互斥锁的尝试操作 | 原子槽位预留，不使用互斥锁，但不是严格 lock-free |

有锁后端接受任意非零容量。原子后端要求容量至少为二且是二的幂，同时平台必须提供无锁的 64 位原子操作。原子能力不支持时返回 `HX_OS_NOT_SUPPORTED`，容量不合法时返回 `HX_OS_INVALID`。不会悄悄退回互斥锁。需要确认进展保证时，可以查询 `hx_os_ring_queue_is_lock_free()`。

```c
hx_os_ring_queue *queue = NULL;
hx_os_status status = hx_os_ring_queue_create(NULL, sizeof(device_sample),
	256, HX_OS_QUEUE_SPSC, HX_OS_QUEUE_ATOMIC, &queue, NULL);

if (status != HX_OS_OK)
	fprintf(stderr, "Create failed: %s\n", hx_os_status_name(status));
```

`try_push()`、`try_pop()` 在满/空时、有锁后端无法立刻取得互斥锁时，或者下一个原子槽位仍被其他操作预留时，都可能返回 `HX_OS_WOULD_BLOCK`。调用者决定稍后重试、明确报告后丢弃，或者施加背压。不能因为接口带 `try` 就持续无限忙自旋。

MPSC/MPMC 会先预留槽位，再发布其中的内容。如果参与线程在预留期间暂停，其他操作可能被该槽位延迟，即使没有互斥锁。CAS 竞争也可能造成内部重复尝试。这些算法不承诺严格 lock-free 进展，也不承诺单次调用耗时有硬上限。SPSC 不存在多个参与者竞争预留同一个槽位的问题，在所需原子能力得到支持时，提供更强的无锁进展保证。无锁不等于所有业务都更快。

并发环形队列不提供阻塞等待、close、EOF、准确的并发 size 快照或指针清理。需要由业务协调生产者和消费者，停止并 join 后才能销毁。需要等待和关闭唤醒时，优先使用线程消息队列。

## 5. 存储与复制边界

并发队列复制固定长度记录，内部持有预分配的槽位，不暴露可写槽位或零拷贝借用。[固定字节环形缓冲](RING.zh-CN.md) 提供调用者数组与有序读写片段，但整个借用期间必须由外部同步。[ByteQueue](BYTE_QUEUE.zh-CN.md) 提供可增长字节缓存。不能因为底层是环形结构，就让不同线程直接同时访问这些无同步的缓冲。

内存预算可以先按 `item_bytes × capacity` 估算，再加上队列、槽位及对齐元数据。增大容量能吸收短时突发，不能长期弥补消费者处理速度不足。选择后端前，应按实际生产者/消费者数量测量吞吐、延迟和内存占用。

## 6. C++11 包装

包含 `<hx/Queue.hpp>` 后，可以使用 `hx::MessageQueue<T>`、`hx::RingQueue<T>`。两者要求 `T` 可平凡复制，通过 `hx::Result` 返回结果，不使用异常。`MessageQueue<T>::create(capacity)` 提供 `send()`、`receive()`、`trySend()`、`tryReceive()`、`close()`、`size()`。需要限制等待阶段时显式传入有限截止时间；普通 send/receive 默认无限等待。`receive(T&)` 写入调用者对象，直接返回值的重载还要求类型具有不抛异常的默认构造函数。

`RingQueue<T>::create(capacity, mode, backend)` 提供 `tryPush()`、`tryPop()`、`capacity()`、`isLockFree()`，默认选择有锁 MPMC。两个包装类都持有底层队列，不负责 join 线程，也不会让正在使用中的对象变得可以安全析构。其他线程使用期间保持包装对象地址稳定，按需要先关闭消息队列，随后 join 所有调用者，再离开作用域或移动对象。

## 7. 批量发送与接收

批量入口减少业务逐条调用的代码，同时保留完整消息边界。传入按队列固定记录长度排列的连续数组，以及必填的 `size_t *completed`。这个计数会先置零，成功、部分完成和失败时都可以读取。数组与完成计数不能互相重叠，也不能与队列内部存储重叠。对有效且已启用的队列，数量为零、数组为 NULL 的请求成功；已关闭的消息队列也一样。

| C 入口 | 等待与部分完成 |
| --- | --- |
| `hx_os_message_queue_send_many_until` | 在同一个截止时间内尝试发完 `count` 条。返回 `TIMEOUT`、`CLOSED` 或 `WOULD_BLOCK` 时，`completed` 可能已经记录成功接收的前缀数量。 |
| `hx_os_message_queue_receive_many_until` | 至少等到一条后，取出当前最多 `maximum` 条可用记录；不等待输出数组凑满。关闭后先取完已接收记录，再返回 `EOF`。 |
| `hx_os_ring_queue_try_push_many` | 尽可能立即入队；完成至少一条即返回 `OK`，完全没有进展时返回 `WOULD_BLOCK`。 |
| `hx_os_ring_queue_try_pop_many` | 尽可能立即出队；完成至少一条即返回 `OK`，完全没有进展时返回 `WOULD_BLOCK`。 |

每条记录都是完整的，但整个批次不是事务。其他生产者或消费者可能穿插执行。部分发送后重试，必须从 `messages + completed` 开始，只重发剩余条数；把整批再发一次会产生重复记录。批量条数超过队列容量时，需要同时有消费者取出数据，才能持续发送。原来的内存分配与深拷贝约定不变。

```c
size_t sent = 0;
hx_os_status status = hx_os_message_queue_send_many_until(
	queue, samples, sample_count, hx_os_deadline_after_ms(1000),
	&sent, NULL);

if (status != HX_OS_OK)
	fprintf(stderr, "Accepted %zu records, stopped with %s\n",
		sent, hx_os_status_name(status));
```

C++ 同步增加 `MessageQueue<T>::sendMany()`、`receiveMany()`、`trySendMany()`、`tryReceiveMany()`，以及 `RingQueue<T>::tryPushMany()`、`tryPopMany()`。它们返回 `Result<size_t>`，即使 `ok()` 为假也保留完成数量。通过 `value()` 读取数量，通过 `status()` 查看状态，不能因为状态是超时就忽略已经完成的前缀。普通 sendMany/receiveMany 默认无限等待，带 try 的入口使用 NOW。

## 8. 取消一次等待操作

单条和批量的 `*_cancellable_until` 入口接收共享 `hx_os_cancel_token`。非空令牌在调用及在途唤醒结束前占用一个预分配订阅槽。取消会唤醒条件变量，不通过反复轮询实现。批量发送返回 `CANCELLED` 时保留已接收前缀，不会关闭整个队列，也不会取消使用另一令牌的调用；取消与完成竞争时，操作仍可能成功。令牌必须活到调用结束；队列销毁前仍需 close 并 join 所有调用者。

C++ 的 `send`、`receive`、`sendMany`、`receiveMany` 在截止时间后增加可选令牌参数；NULL 保留原有行为。令牌的创建、请求和回收见[线程协调](COORDINATION.zh-CN.md)。
