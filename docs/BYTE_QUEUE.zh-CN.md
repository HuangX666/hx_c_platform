# 字节队列的批量操作

[English](BYTE_QUEUE.md) · [README](../README.zh-CN.md)

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
