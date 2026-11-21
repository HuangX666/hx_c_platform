# 字节队列的批量操作

[English](BYTE_QUEUE.md) · [README](../README.zh-CN.md)

当串口、Socket 或管道每次只到达一部分零散字节，需要先按顺序累积起来、凑够报文头或完整负载后再整段取出时，可以使用可增长字节队列 `hx_os_byte_queue`（C++11 对应 `<hx/ByteQueue.hpp>` 中的 `hx::ByteQueue`）。它复用容器模块的环形双端存储并在配置上限内按需扩容，提供整块写入、不消费预览（Peek）、定长读取以及按当前可用量部分读写（`read_some` / `write_some`）。

完整可运行的示例位于 [byte_queue_ring.c](../examples/byte_queue_ring.c)：其中的 `example_queue` 函数创建一个有容量上限的字节队列，调用 `hx_os_byte_queue_write` 写入 `"hello"`，再用 `hx_os_byte_queue_read_some` 取出当前可用字节并获得实际读取长度，最后销毁队列。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 `-DHX_OS_BUILD_EXAMPLES=ON` 并构建 `hx_os_byte_queue_ring` 目标即可直接运行（无需命令行参数或输入文件）。程序第一行输出 ByteQueue 的结果（第二行紧接着演示固定环形缓冲）：

```text
ByteQueue: 5 bytes: hello
Ring: efxxxxxx
```

你也可以参照[最小工程配置](USAGE.zh-CN.md#c-project)将该示例源码作为自己工程的 `main.c`。

> [!NOTE]
> `hx_os_byte_queue` 受 `HX_OS_ENABLE_CONTAINERS` 构建开关控制。它只负责字节流的顺序缓冲，不包含应用层消息分帧、互斥锁、阻塞等待或后台线程。多线程并发生产／消费时，请由调用方在外部加锁同步，或使用更高层的并发队列／IPC 接口。

## 1. 写入、预览、取出一段数据

下面的完整 C99 程序向字节队列写入 4 个字节（包含中间的 `0x00`），先用 `hx_os_byte_queue_peek` 预览而不移除数据，再用 `hx_os_byte_queue_read` 正式消费这 4 个字节：

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

运行输出为 `Remaining: 0 bytes`。
- 队列将 `0x00`（`NUL`）视为普通二进制字节处理，不会当作字符串结尾截断，读取时也不会自动追加结尾 `NUL`。
- 调用 `hx_os_byte_queue_read` 时如果将输出缓冲区指针传为 `NULL`，则表示直接丢弃（Discard）指定长度的头部字节而不做内存拷贝。
- 所有函数的最后一个参数均为可选的 `hx_os_error *error`（不需要详细错误诊断时传 `NULL`）。

## 2. 容量与失败行为

- **上限与按需扩容**：`hx_os_byte_queue_create(context, max_bytes, &queue, error)` 中的 `max_bytes` 限制队列最多可容纳的已排队有效负载字节数（传 `0` 时默认使用 `64 KiB`）。底层缓冲区按需扩容；扩容期间会短暂同时持有新旧两块缓冲区（总峰值不超过两倍有效负载上限加容器元数据，可通过 `hx_os_sequence_memory()` 查询库内实时分配字节数）。
- **精确读写契约**：
  - `hx_os_byte_queue_write` 采用全有或全无语义：要么完整写入全部请求字节并返回 `HX_OS_OK`，要么在超出上限时返回 `HX_OS_LIMIT`、在分配失败时返回 `HX_OS_NO_MEMORY`，失败时队列原有数据保持不变。
  - `hx_os_byte_queue_read` 和 `hx_os_byte_queue_peek` 要求队列中已有字节数不少于请求长度：当可读字节不足时返回 `HX_OS_NEED_INPUT`，队列内容与输出缓冲区均保持不变。
  - 长度为 `0` 的读写或预览在合法参数下直接返回 `HX_OS_OK`。字节队列本身没有 `EOF` 或生产者关闭状态。
- **使用限制**：批量字节接口仅接受 `hx_os_byte_queue_create` 创建的纯字节双端队列（拒绝其他元素尺寸、`list`/`vector` 或带 `copy`/`drop` 回调的容器）。传入的读写缓冲区不得与队列内部存储重叠。由于底层采用环形数组，每次读取或预览最多执行两次连续内存块拷贝（`memcpy`），写入在完成必要的扩容后同样最多执行两次块拷贝。任何非空的成功写入或读取都会使该容器上已有的迭代器失效。

## 3. C++11

在 C++11 中包含 `<hx/ByteQueue.hpp>` 即可使用 RAII 包装类 `hx::ByteQueue`。下面的函数演示了写入包含内嵌 `NUL` 的 3 个字节，并将其读出为一个独立持有内存的 `hx::Buffer`：

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

- `queue.read(pointer, bytes)` 将数据读入调用方提供的缓冲区；`queue.read(bytes, context)` 则分配并返回一个拥有内存的 `hx::Buffer`（若分配 `Buffer` 失败，队列中的字节不会被消费）。
- `queue.peek(pointer, bytes, offset)` 支持从指定偏移量预览数据而不消费；`queue.discard(bytes)` 直接丢弃头部指定字节。

## 4. 容量管理与部分读写

为了减少运行期扩容次数，或者在流式解析中“有多少读多少”，可以使用容量预留与部分读写接口：
- **容量预留与查询**：
  - `hx_os_byte_queue_reserve(queue, capacity, error)`：提前分配足以容纳 `capacity` 字节的底层存储。
  - `hx_os_byte_queue_capacity(queue)`：返回当前已分配的底层负载容量。
  - `hx_os_byte_queue_limit(queue)`：返回创建时配置的最大负载上限。
  - `hx_os_byte_queue_space(queue)`：返回剩余可写入上限（即 `limit() - size()`，注意这部分剩余额度不一定已经预分配了物理内存）。
  - `hx_os_byte_queue_clear(queue)`：清空已排队字节，但保留已分配的底层容量。
- **部分读写（`read_some` / `write_some`）**：
  - `hx_os_byte_queue_read_some(queue, output, requested, &completed, error)`：最多读取 `requested` 与当前可读字节数（`size()`）中的较小值。
  - `hx_os_byte_queue_write_some(queue, input, requested, &completed, error)`：最多写入 `requested` 与剩余额度（`space()`）中的较小值。
  - 只要成功搬移了至少 1 个字节（或者 `requested == 0`），这两个函数都会返回 `HX_OS_OK`，并将实际完成的字节数写入必填的 `completed` 输出参数（`output` 传 `NULL` 时，`read_some` 会丢弃可用前缀并同样报告丢弃数量）。
  - 当 `requested > 0` 但队列当前完全为空时，`read_some` 返回 `HX_OS_NEED_INPUT`；当 `requested > 0` 但队列已达 `limit()` 上限时，`write_some` 返回 `HX_OS_LIMIT`（若底层扩容分配失败则返回 `HX_OS_NO_MEMORY`，不会接收部分写入）。失败时 `completed` 置为 `0`，队列原有内容不变。

```c
char block[1024];
size_t received = 0;
hx_os_status status = hx_os_byte_queue_read_some(
	queue, block, sizeof(block), &received, NULL);

if (status == HX_OS_OK)
	printf("Read %zu bytes\n", received);
```

与要求凑满全部长度的 `hx_os_byte_queue_read` 不同，当队列里只有 5 个字节时，上面的 `read_some` 也能立即将这 5 个字节读入 `1024` 字节的缓冲区并将 `received` 设为 `5`。

C++ `hx::ByteQueue` 同步提供 `capacity()`、`limit()`、`space()`、`empty()`、`reserve()`、`clear()`、`readSome()` 和 `writeSome()`（其中 `readSome` 与 `writeSome` 返回 `hx::Result<size_t>`，其值即为实际读写字节数）。

> [!TIP]
> 如果你已经有一块固定大小的外部数组（如栈上或静态缓冲区），希望零堆内存分配地循环使用，并直接获取内部两个连续片段的指针进行零中间拷贝的读写，请使用[固定环形缓冲](RING.zh-CN.md)（完整对比见 [ByteQueue 与 ring 示例](../examples/byte_queue_ring.c)）。
