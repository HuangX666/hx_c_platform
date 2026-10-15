# 字节队列的批量操作

[English](BYTE_QUEUE.md) · [README](../README.zh-CN.md)

**先运行一个完整例子。** 串口或网络每次只给一部分字节，需要先积累再按顺序取出时，可以用 ByteQueue。它本身不会解析消息。

打开 [byte_queue_ring.c](../examples/byte_queue_ring.c)。`example_queue` 创建有上限的队列 → `write` 放入 hello → `read_some` 取出当前字节并返回实际数量 → `destroy`。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 examples，并只构建 `hx_os_byte_queue_ring`。此例无命令行参数、无需输入文件。

**会得到什么：**第一行是 `ByteQueue: 5 bytes: hello`。程序随后还演示固定 Ring。换成业务时按返回的字节数处理数据；缓冲不自带字符串结束符或线程同步。

下面的短片段用于解释接口；复制运行时以完整示例为准。也可按[最小工程配置](USAGE.zh-CN.md#c-project)把完整源码作为自己的 main。

`hx_os_byte_queue` 用于积累字节流，再取出完整的一段。它复用现有 deque 存储和 CONTAINERS 开关，不自带消息分帧、锁、阻塞等待或后台线程。多个线程同时生产／消费时，由调用者同步，或者使用更上层的 IPC 接口。

## 1. 写入、预览、取出一段数据

下面是完整 C99 程序：写入四个字节，先预览，再取出：

```c
#include <hx_container.h>
#include <stdio.h>

int main(void)
{
	hx_os_byte_queue *queue = NULL;
	unsigned char packet[4] = {1, 0, 2, 3};
	unsigned char result[4];
	hx_os_status status = hx_os_byte_queue_create(NULL, 4096, &queue, NULL);
	if (status != HX_OS_OK)
		return 1;

	status = hx_os_byte_queue_write(queue, packet, sizeof(packet), NULL);
	if (status == HX_OS_OK)
		status = hx_os_byte_queue_peek(queue, 0, result, 4, NULL);
	if (status == HX_OS_OK)
		status = hx_os_byte_queue_read(queue, result, 4, NULL);
	if (status == HX_OS_OK)
		printf("Remaining: %zu bytes\n", hx_os_byte_queue_size(queue));

	hx_os_byte_queue_destroy(&queue, NULL);

	return status == HX_OS_OK ? 0 : 1;
}
```

预期输出为 `Remaining: 0 bytes`。NUL 是普通数据；读取时输出指针传 NULL 表示丢弃指定字节。各操作最后一个参数是可选错误记录。

## 2. 容量与失败行为

创建时的上限是已排队的有效字节数，传零默认 64 KiB。缓冲按需增长；扩容期间可能同时持有旧、新缓冲，总量不超过两倍有效字节上限加分配元数据。`hx_os_sequence_memory()` 可查询实际库内分配成本。有需要时，可通过现有 sequence 的 reserve 接口提前预留，避免后续扩容。

写入要么完整成功，要么返回 LIMIT／NO_MEMORY 并保留已有内容。读取和预览都是精确长度：范围还不够时返回 NEED_INPUT，不修改队列和输出。零字节操作在有效范围上成功。该队列不提供 EOF 或生产者关闭状态。

默认工厂创建纯字节存储，不带元素回调。批量操作拒绝其他元素类型、list／vector，以及带复制／释放回调的元素。输入输出不能指向队列自己持有的存储。非空写入或读取成功会使现有容器迭代器失效。读取、预览最多进行两次块复制；写入在必要扩容后同样如此。这描述实现方式，不代表已经测得吞吐提升。

## 3. C++11

包含 `<hx/ByteQueue.hpp>`。下面的函数返回自己持有内存的三个字节，保留中间的 NUL：

```cpp
hx::Result<hx::Buffer> queued_bytes() noexcept
{
	auto created = hx::ByteQueue::create(4096);
	if (!created)
		return hx::Failure(created.error());

	auto &queue = created.value();
	auto written = queue.write("a\0b", 3);
	if (!written)
		return hx::Failure(written.error());

	return queue.read(size_t(3));
}
```

`read(pointer, bytes)` 使用调用者的存储；`read(bytes, context)` 返回拥有内存的 `Buffer`。`peek(pointer, bytes, offset)` 只预览，`discard(bytes)` 丢弃指定字节。获取拥有内存的结果时，分配失败不会消费队列。便利类拥有原生队列，析构无需等待；并发访问仍需调用者同步。

## 4. 容量管理与部分读写

`hx_os_byte_queue_reserve(queue, capacity, error)` 可以提前预留有效负载存储，减少后续扩容。`capacity()` 返回已经分配的有效负载容量；`limit()` 返回创建时设置的上限；`space()` 是 `limit()-size()`，因此不一定都已经分配到内存。`clear()` 清除数据但保留容量。原有布局及完整读写契约不变。

`hx_os_byte_queue_read_some(queue, output, requested, &completed, error)` 最多读取请求长度与现有数据量中的较小值。`write_some()` 最多写入剩余有效负载上限允许的字节数。即使只完成一部分，两者仍返回 `HX_OS_OK`，实际数量写入 `completed`。需要完整处理请求时，调用者必须检查这个数量；库不会暗中重试。

非零长度请求遇到空队列时，读取返回 `HX_OS_NEED_INPUT`；遇到满队列时，写入返回 `HX_OS_LIMIT`。失败时 `completed` 为零，原有内容不变。分配失败不会接收部分写入。零字节请求成功并返回零。`completed` 必填，不能与数据或队列存储重叠。读取目标为 NULL 时，丢弃可用前缀。

```c
char block[1024];
size_t received = 0;
hx_os_status status = hx_os_byte_queue_read_some(
	queue, block, sizeof(block), &received, NULL);

if (status == HX_OS_OK)
	printf("Read %zu bytes\n", received);
```

与要求全部长度的 `read` 不同，队列只有五字节时，上例也能立刻取出这五字节，不必凑满 1024 字节。字节数据不会自动补字符串结尾的 NUL。

C++ 同步提供 `capacity()`、`limit()`、`space()`、`empty()`、`reserve()`、`clear()`、`readSome()`、`writeSome()`。带 `Some` 的方法返回 `Result<size_t>`，其中的值就是完成字节数。普通 `read()`、`write()` 仍要求完整范围。

需要使用调用者提供的固定数组，并直接借用两个连续片段时，使用[固定环形缓冲](RING.zh-CN.md)。可增长存储可能移动，因此借用与提交入口独立放在固定 ring 中。可参考完整的 [ByteQueue 与 ring 示例](../examples/byte_queue_ring.c)。
