# 线程消息队列与并发环形队列

[English](QUEUES.md) · [固定字节环形缓冲](RING.zh-CN.md) · [README](../README.zh-CN.md)

当采集线程持续产生定长业务记录（如传感器采样、日志事件或识别结果），并需要安全地交给后端线程入库或上传时，可以使用本库的进程内并发队列。队列保证每次收发都以**完整消息记录**为单位交接，业务代码无需自己处理半条结构体的拼接。

### 快速运行示例

完整示例位于 [message_queue.c](../examples/message_queue.c)：创建一个容量为 2 条记录的消息队列，由生产线程批量发送 6 条传感器采样，主线程分批接收并在队列关闭后排空剩余消息，最后 `join` 生产线程并销毁队列。

按[示例运行方法](../examples/README.zh-CN.md#run)启用示例后，构建并运行 `hx_os_message_queue`（无需命令行参数或输入文件）：

```sh
cmake --build build --config Release --target hx_os_message_queue
```

预期输出设备 `17` 的 6 条采样数据（从 `Device 17, sample 1: 20.5` 到 `Device 17, sample 6: 23.0`）。在接入自己的工程时，可直接参考[最小工程配置](USAGE.zh-CN.md#c-project)，将 `device_sample` 结构体和处理逻辑替换为你的业务数据结构。

## 1. 选择入口

根据线程拓扑和是否需要阻塞等待，在四种队列/缓冲区中选择：

| 应用场景 | 接口类型 | 核心行为 |
| --- | --- | --- |
| 工作线程之间传递完整结构体消息，队列满或空时休眠等待 | `hx_os_message_queue` | 有界、带互斥锁与条件变量的 MPMC 队列，支持单调时钟截止时间、取消及关闭后排空 |
| 已有事件循环或工作线程自行决定队列满/空时的退避策略 | `hx_os_ring_queue` | 有界定长元素并发环形队列，提供非阻塞 `try_push` / `try_pop` |
| 单线程（或外部加锁）在调用方自有数组中零拷贝解析字节 | `hx_os_ring` | 固定字节环形缓冲，支持显式借用两个连续读写片段 |
| 单线程（或外部加锁）累积需要按需扩容的字节流 | `hx_os_byte_queue` | 有容量上限的动态字节队列，支持完整与部分字节读写 |

- **编译开关**：线程消息队列（`hx_os_message_queue`）与并发环形队列（`hx_os_ring_queue`）由 `HX_OS_ENABLE_QUEUES` 控制（可通过 `hx_os_queues_available()` 在运行期查询）；字节容器由 `HX_OS_ENABLE_CONTAINERS` 控制。所有队列均只管理数据存储与同步，不会在后台暗中创建工作线程或协程运行时。
- **创建期一次性预分配**：两种并发队列都在创建时一次性分配好固定槽位内存，后续每次 `send` / `receive` / `try_push` / `try_pop` **零堆内存分配**。容量单位是**消息条数**（而非字节数），单条消息字节长度在创建时固定。

> [!WARNING]
> **按值字节复制与指针所有权边界**
> 队列按固定长度直接复制消息字节（`memcpy`）。如果消息结构体中包含指针字段，队列不会自动深拷贝指针指向的数据，也不会在销毁非空队列时自动释放指针指向的外部资源。请优先传递平凡可复制（trivially copyable）的纯值结构体；若必须传递指针，需由应用层明确约定所有权交接与异常清理逻辑。需要运行构造/析构函数的非平凡 C++ 对象不能直接作为原始字节放入队列。

## 2. 生产线程交给处理线程

在完整的 [C 设备示例](../examples/message_queue.c) 中，队列容量设为 `2` 条 `device_sample`，生产线程调用一次 `hx_os_message_queue_send_many_until()` 提交全部 `6` 条记录，主线程则用长度为 `3` 的数组分批接收当前可用的完整记录：
- 尽管发送批次（6 条）和接收缓冲（3 条）都大于队列容量（2 条），生产者会在队列满时自动休眠等待消费者腾出空间，而消费者只要取到至少 1 条可用记录就会立即返回处理，不会死等输出数组凑满。
- 生产者发送完毕后调用 `hx_os_message_queue_close()` 关闭队列；消费者继续循环读取队列中剩余的消息，直到收到 `HX_OS_EOF`，随后 `join` 生产线程并销毁队列。

单条发送的典型写法如下：

```c
typedef struct device_sample {
	uint32_t device_id;
	uint32_t sequence;
	double value;
} device_sample;

// queue was created for sizeof(device_sample); main owns its lifetime.
device_sample sample = {17, 1, 20.5};
hx_os_status status = hx_os_message_queue_send_until(
	queue, &sample, hx_os_deadline_after_ms(1000), NULL);

if (status != HX_OS_OK)
	fprintf(stderr, "Send failed: %s\n", hx_os_status_name(status));
```

接收线程准备另一个 `device_sample` 变量并调用 `hx_os_message_queue_receive_until()`。只要发送成功返回，数据就已经完整复制到队列槽位中，生产线程可以立即复用栈上的局部变量 `sample`。

## 3. 等待、关闭与所有权

`hx_os_message_queue` 在不同截止时间和生命周期阶段的状态码如下：

| 场景 | 返回状态与行为契约 |
| --- | --- |
| 传入 `HX_OS_DEADLINE_NOW` 且当前队列满/空 | 立即返回 `HX_OS_WOULD_BLOCK`，绝不会写入或消费半条消息 |
| 有限单调时钟截止时间到期 | 返回 `HX_OS_TIMEOUT`；单条发送/接收不转移数据，批量发送则通过 `completed` 保留已入队的前缀条数 |
| 传入 `HX_OS_DEADLINE_INFINITE` | 无限期等待，直到有空间/消息可用，或被 `close()` / 取消令牌唤醒 |
| 队列关闭后调用 `send` | 立即返回 `HX_OS_CLOSED` |
| 队列关闭后调用 `receive`（队列中仍有存量消息） | 正常返回 `HX_OS_OK` 并取出下一条完整消息 |
| 队列关闭后调用 `receive`（存量消息已全部取空） | 返回 `HX_OS_EOF` |
| 重复调用 `hx_os_message_queue_close()` | 幂等返回 `HX_OS_OK`，并唤醒所有正在等待的生产者和消费者 |

> [!IMPORTANT]
> **标准关闭与销毁顺序**
> 1. **`close()`**（可与收发操作并发调用，用于通知生产者和消费者停止并唤醒阻塞等待）
> 2. **排空存量消息**（消费者循环读取直到返回 `HX_OS_EOF`）
> 3. **`join` 所有使用该队列的生产者与消费者线程**（若有限时间 `join` 返回 `HX_OS_TIMEOUT`，必须保留队列及线程状态并稍后重试，切勿提前销毁仍在被后台线程访问的队列）
> 4. **`destroy()`**（必须在外部确保没有任何线程正在调用或即将进入队列接口；若检测到仍有活跃等待者，会返回 `HX_OS_BUSY`）

等待期间请勿持有对端线程消费或生产所必需的业务锁。`hx_os_message_queue_size()` 返回受锁保护的瞬时条数快照，仅用于监控观测；请直接根据 `send` / `receive` 的返回状态编写分支逻辑，不要采用“先查 `size()` 再假定不会阻塞”的写法。

## 4. SPSC、MPSC 与 MPMC 环形队列

当业务自己管理工作循环、不需要队列内部执行条件变量睡眠时，可以使用 `hx_os_ring_queue_create(context, item_bytes, capacity, mode, backend, &queue, error)` 显式指定并发模式与后端实现：

| 并发模式 | 允许的并发调用方 | `HX_OS_QUEUE_LOCKED` 后端 | `HX_OS_QUEUE_ATOMIC` 后端 |
| --- | --- | --- | --- |
| `HX_OS_QUEUE_SPSC` | 单生产者、单消费者 | 基于互斥锁的非阻塞尝试操作 | 具备无锁（Lock-Free）进展保证的 SPSC 环形算法 |
| `HX_OS_QUEUE_MPSC` | 多生产者、单消费者 | 基于互斥锁的非阻塞尝试操作 | 原子槽位预留算法（不使用互斥锁，但非严格 Lock-Free） |
| `HX_OS_QUEUE_MPMC` | 多生产者、多消费者 | 基于互斥锁的非阻塞尝试操作 | 原子槽位预留算法（不使用互斥锁，但非严格 Lock-Free） |

- **容量与平台原子能力要求**：
  - `HX_OS_QUEUE_LOCKED` 接受任意非零容量。
  - `HX_OS_QUEUE_ATOMIC` 要求 `capacity` 至少为 `2` 且必须是 **`2` 的幂**（不合法返回 `HX_OS_INVALID`），同时要求目标平台支持无锁 64 位原子操作（不支持时明确返回 `HX_OS_NOT_SUPPORTED`，绝不暗中退回互斥锁）。可通过 `hx_os_ring_queue_is_lock_free()` 查询进展保证。

```c
hx_os_ring_queue *queue = NULL;
hx_os_status status = hx_os_ring_queue_create(NULL, sizeof(device_sample),
	256, HX_OS_QUEUE_SPSC, HX_OS_QUEUE_ATOMIC, &queue, NULL);

if (status != HX_OS_OK)
	fprintf(stderr, "Create failed: %s\n", hx_os_status_name(status));
```

- **理解 `HX_OS_WOULD_BLOCK` 与进展特征**：当队列满/空、`HX_OS_QUEUE_LOCKED` 后端未能立即拿到互斥锁，或 `HX_OS_QUEUE_ATOMIC` 后端的下一个槽位正被其他线程预留写入时，`try_push()` / `try_pop()` 会立即返回 `HX_OS_WOULD_BLOCK`。在 `MPSC` / `MPMC` 原子后端中，线程会先通过 CAS 预留槽位再复制数据；如果某个线程在预留槽位后被操作系统抢占挂起，后续经过该槽位的操作也会暂时返回 `HX_OS_WOULD_BLOCK`。收到 `HX_OS_WOULD_BLOCK` 时，调用方应结合退避、事件通知或背压策略处理，切勿编写无休止的空转死循环。
- **生命周期**：`hx_os_ring_queue` 不提供阻塞等待、`close()` 或 `HX_OS_EOF`。销毁前必须由应用层先停止并 `join` 所有生产者和消费者线程。

## 5. 存储与复制边界

并发队列（`hx_os_message_queue` 与 `hx_os_ring_queue`）管理预分配的定长记录槽位，不暴露内部可写指针。如果你需要的是单线程字节流缓存，请分别使用：
- [固定字节环形缓冲（`hx_os_ring`）](RING.zh-CN.md)：在调用方提供的字节数组上提供最多两段连续读写切片借用（跨线程共享时必须由外部全程加锁）。
- [动态字节队列（`hx_os_byte_queue`）](BYTE_QUEUE.zh-CN.md)：支持在上限内按需扩容的字节流缓冲。

队列内存占用可按 `item_bytes * capacity` 加上少量的队列控制头与槽位对齐开销估算。增大容量有助于吸收短时流量突发，但如果消费者长期处理速度低于生产者，仍需通过业务背压或扩容消费者解决。

## 6. C++11 包装

包含 `<hx/Queue.hpp>` 即可使用无异常的 `hx::MessageQueue<T>` 和 `hx::RingQueue<T>`（要求 `T` 为平凡可复制类型）：
- **`hx::MessageQueue<T>`**：通过 `MessageQueue<T>::create(capacity)` 创建，提供 `send()`、`receive()`、`trySend()`、`tryReceive()`、`close()` 和 `size()`。普通 `send` / `receive` 默认使用无限截止时间，也可显式传入单调时钟截止时间。`receive(T&)` 直接写入调用方变量；不带参数的 `receive()` 重载返回 `Result<T>`（要求 `T` 具有 `noexcept` 默认构造函数）。
- **`hx::RingQueue<T>`**：通过 `RingQueue<T>::create(capacity, mode, backend)` 创建（默认为有锁 MPMC），提供 `tryPush()`、`tryPop()`、`capacity()` 和 `isLockFree()`。

两个 C++ 包装类独占持有底层队列句柄，但析构时不会替宿主 `join` 工作线程。请确保所有生产者与消费者线程均已停止并 `join` 后，再移动或析构队列包装对象。

## 7. 批量发送与接收

批量接口可以在一次加锁或一次系统交互内交接多条定长记录，显著降低高频小消息的调用开销。调用时传入连续的 `T` 数组指针以及必填的 `size_t *completed` 输出参数（函数入口处会先将 `*completed` 置为 `0`）：

| C 批量接口 | 等待行为与部分完成规则 |
| --- | --- |
| `hx_os_message_queue_send_many_until` | 在给定截止时间内尝试发送全部 `count` 条记录。若因 `HX_OS_TIMEOUT`、`HX_OS_CLOSED`、`HX_OS_WOULD_BLOCK` 或 `HX_OS_CANCELLED` 中途停止，`*completed` 会准确记录已成功入队的前缀条数。 |
| `hx_os_message_queue_receive_many_until` | 阻塞等待至多到**至少有 1 条记录可用**，然后一次性取出当前最多 `maximum` 条可用记录并立即返回（不会死等把输出数组填满）。队列关闭后先取完存量记录，最后返回 `HX_OS_EOF`。 |
| `hx_os_ring_queue_try_push_many` | 非阻塞地尽可能多入队；只要成功入队至少 `1` 条即返回 `HX_OS_OK`，`0` 条进展时返回 `HX_OS_WOULD_BLOCK`。 |
| `hx_os_ring_queue_try_pop_many` | 非阻塞地尽可能多出队；只要成功出队至少 `1` 条即返回 `HX_OS_OK`，`0` 条进展时返回 `HX_OS_WOULD_BLOCK`。 |

```c
size_t sent = 0;
hx_os_status status = hx_os_message_queue_send_many_until(
	queue, samples, sample_count, hx_os_deadline_after_ms(1000),
	&sent, NULL);

if (status != HX_OS_OK)
	fprintf(stderr, "Accepted %zu records, stopped with %s\n",
		sent, hx_os_status_name(status));
```

> [!TIP]
> **部分发送后的重试游标推进**
> 单条记录保证完整，但整批操作不是原子事务（多个生产者/消费者并发时可能交错入队）。当 `send_many_until` 返回超时或部分完成时，下一次重试必须从 `samples + sent` 处开始、剩余条数设为 `sample_count - sent`；切勿从头重发整批数组，否则会导致前 `sent` 条记录重复入队。

C++ 对应提供 `MessageQueue<T>::sendMany()`、`receiveMany()`、`trySendMany()`、`tryReceiveMany()` 以及 `RingQueue<T>::tryPushMany()`、`tryPopMany()`。它们返回 `Result<size_t>`，即使 `ok()` 为 `false`（如超时），`value()` 中依然保留已完成的条数。

## 8. 取消一次等待操作

如果希望只唤醒并取消某一次特定的消息队列等待，而不关闭整个队列，可以调用单条或批量的 `*_cancellable_until` 接口（如 `hx_os_message_queue_send_cancellable_until`、`hx_os_message_queue_receive_cancellable_until`），并传入共享的 `hx_os_cancel_token *`：
- 非空令牌在等待期间借用一个预分配的取消订阅槽，取消请求会直接唤醒条件变量（无需轮询）。
- 当等待被取消并返回 `HX_OS_CANCELLED` 时，队列本身保持打开状态，使用其他令牌的并发调用不受影响；批量发送在返回 `HX_OS_CANCELLED` 时同样会在 `completed` 中保留已成功入队的前缀条数。
- C++ 的 `send`、`receive`、`sendMany` 和 `receiveMany` 均在 `deadline` 参数后支持传入可选的 `hx_os_cancel_token *`（默认 `NULL`）。取消令牌的创建与管理详见[线程协调](COORDINATION.zh-CN.md)。
