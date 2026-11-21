# 固定字节环形缓冲

[English](RING.md) · [ByteQueue](BYTE_QUEUE.zh-CN.md) · [README](../README.zh-CN.md)

当工程中已经有一块固定大小的内存（例如栈上数组、静态全局数组或预分配的物理缓冲块），希望在**零堆内存分配**的前提下循环复用这块空间，或者希望直接借用内部空闲／可读的两个连续内存片段（Spans）进行原位填充与流式解析（省去中间临时缓冲区的拷贝）时，可以使用固定字节环形缓冲 `hx_os_ring`（C++11 对应 `<hx/Ring.hpp>` 中的 `hx::Ring`）。

完整可运行的示例位于 [byte_queue_ring.c](../examples/byte_queue_ring.c)：其中的 `example_ring` 函数使用一个 8 字节数组初始化环形缓冲，先写入 `"abcdef"` 并消费前 4 个字节 `"abcd"`（使剩余的 `"ef"` 停留在数组尾部），接着调用 `hx_os_ring_write_spans` 直接借用跨越数组末尾的两个空闲连续片段填入 `'x'` 并调用 `hx_os_ring_commit` 提交，最后通过 `hx_os_ring_read_spans` 借用两段可读数据打印并调用 `hx_os_ring_consume` 消费。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 `-DHX_OS_BUILD_EXAMPLES=ON` 并构建 `hx_os_byte_queue_ring` 目标即可直接运行（无需命令行参数或输入文件）。其中第二行输出即为固定环形缓冲的结果：

```text
ByteQueue: 5 bytes: hello
Ring: efxxxxxx
```

你也可以参照[最小工程配置](USAGE.zh-CN.md#c-project)将该示例源码作为自己工程的 `main.c`。

## 1. 选择存储方式

`hx_os_ring` 完全借用调用方传入的外部字节数组，`hx_os_ring` 结构体本身和底层数组都可以直接放在栈上或静态存储区。它绝不调用堆分配器、不会自动扩容、不会覆盖尚未消费的旧字节，也不启动后台线程或阻塞等待。该模块复用 `HX_OS_ENABLE_CONTAINERS` 构建开关；若关闭该开关，返回状态码的操作会返回 `HX_OS_NOT_SUPPORTED`。

| 对比维度 | `hx_os_ring`（固定环形缓冲） | `hx_os_byte_queue`（[可增长字节队列](BYTE_QUEUE.zh-CN.md)） |
| --- | --- | --- |
| 内存来源 | 借用调用方提供的固定数组，零堆内存分配 | 由容器分配器管理，在上限内按需动态扩容 |
| 访问方式 | 支持普通复制读写 + **双连续片段（Spans）直接借用与提交** | 支持精确复制读写、预览与 `read_some`/`write_some`（因扩容可能搬移地址，不提供裸片段借用） |
| 适用场景 | 固定内存预算、嵌入式零分配缓冲、直接在环形空间内原位组装或解析报文 | 未知峰值到达节奏、希望自动扩容暂存的字节流 |

> [!IMPORTANT]
> `hx_os_ring` 和 `hx_os_byte_queue` 都是单线程数据结构，不含内部互斥锁（没有内部锁并不意味着它是无锁并发队列）。若在多线程之间共享同一个 `hx_os_ring`，外部互斥锁必须完整覆盖每一次读写调用，或者覆盖从 `write_spans`/`read_spans` 开始直到 `commit`/`consume` 结束的**整段借用期间**。

## 2. 普通读写

如果只需要常规的字节拷贝读写，包含 `<hx_ring.h>` 并调用 `hx_os_ring_init`、`hx_os_ring_write` 和 `hx_os_ring_read` 即可：

```c
#include <hx_ring.h>
#include <stdio.h>

int main(void)
{
	unsigned char storage[256];
	hx_os_ring ring;
	char text[6] = {0};
	hx_os_status status;

	status = hx_os_ring_init(&ring, storage, sizeof(storage), NULL);
	if (status == HX_OS_OK)
		status = hx_os_ring_write(&ring, "hello", 5, NULL);
	if (status == HX_OS_OK)
		status = hx_os_ring_read(&ring, text, 5, NULL);
	if (status == HX_OS_OK)
		puts(text);

	return status == HX_OS_OK ? 0 : 1;
}
```

运行后输出 `hello`。由于不涉及任何堆内存分配，使用完毕后无需调用销毁函数。
- **底层数组生命周期**：在 `ring` 对象或任何借用片段仍在使用期间，`storage` 数组必须始终保持有效。切勿按值拷贝已初始化的 `hx_os_ring` 结构体或直接手工修改其内部字段。若需重新初始化一个 `hx_os_ring`，必须先结束所有正在进行的借用；重新调用 `hx_os_ring_init` 会直接丢弃原有数据计数。
- **容量查询与清空**：`hx_os_ring_size(&ring)` 返回当前已排队的可读字节数，`hx_os_ring_capacity(&ring)` 返回底层数组的准确字节长度，`hx_os_ring_space(&ring)` 返回剩余可写字节数（即 `capacity - size`）。`hx_os_ring_clear(&ring, error)` 会丢弃所有已排队字节但保留底层数组绑定。允许初始化容量为 `0` 的环形缓冲（仅能执行 `0` 字节复制）。

| 操作接口 | 空间或数据不足时的返回值 | 失败时的状态保持 |
| --- | --- | --- |
| `hx_os_ring_write(&ring, data, bytes, error)` | `HX_OS_OUTPUT_FULL`（剩余空间小于 `bytes`） | 环形缓冲内原有数据不变，不写入部分字节 |
| `hx_os_ring_read(&ring, out, bytes, error)` | `HX_OS_NEED_INPUT`（可读数据小于 `bytes`） | 环形缓冲与目标 `out` 缓冲区均保持不变 |
| `hx_os_ring_read(&ring, NULL, bytes, error)` | `HX_OS_NEED_INPUT`（可读数据小于 `bytes`） | 仅在数据充足时准确丢弃头部 `bytes` 个字节 |

普通复制读写传入的输入／输出缓冲区不能与 `hx_os_ring` 结构体自身或底层 `storage` 数组发生内存重叠。

## 3. 借用两个片段，省去中间复制

当环形缓冲的读写游标绕过底层数组末尾（Wrap Around）时，逻辑上连续的可写空闲空间或可读数据在物理内存上会分成前后两段（先是数组尾部的第一段 `parts[0]`，紧接着是绕回数组开头的第二段 `parts[1]`；若未发生回绕，则未使用的第二段固定为 `{NULL, 0}`）。

通过 `hx_os_ring_write_spans` 和 `hx_os_ring_read_spans`，调用方可以一次性借出这两段连续内存的指针与长度，直接在环形缓冲内部填充数据或运行解析器，从而省去先拷入临时线性缓冲区的中间复制开销：

```c
hx_os_ring_write_span parts[2];
hx_os_status status = hx_os_ring_write_spans(&ring, parts, NULL);

if (status == HX_OS_OK) {
	// Example: initialize one byte directly in available ring storage.
	parts[0].data[0] = 'A';
	status = hx_os_ring_commit(&ring, 1, NULL);
}
```

读取侧遵循对称的模式：先调用 `hx_os_ring_read_spans(&ring, parts, error)` 检视 `parts[0]` 和 `parts[1]` 中的可读字节，解析完已识别的前缀后，调用 `hx_os_ring_consume(&ring, bytes, error)` 正式消费对应字节数并腾出空间。同理，写入侧调用 `hx_os_ring_commit(&ring, bytes, error)` 只会将实际已填好的前缀长度发布为可读数据，其余未提交部分仍作为空闲空间保留。

> [!WARNING]
> **片段借用（Span Borrow）生命周期与状态机规则：**
> 1. **单借用互斥**：同一时刻最多只允许存在一个活动借用（无论是读借用还是写借用）。在当前借用通过 `commit` 或 `consume` 结束之前，再次发起借用、调用普通复制读写或调用 `clear` 都会被拒绝并返回 `HX_OS_BUSY`。
> 2. **放弃借用**：传入 0 字节（`hx_os_ring_commit(&ring, 0, NULL)` 或 `hx_os_ring_consume(&ring, 0, NULL)`）可显式取消当前借用而不改变缓冲内的有效字节数。
> 3. **提交／消费后指针立即失效**：一旦 `commit` 或 `consume` 成功返回（即便只提交或消费了借用长度的一部分），此前拿到的 `parts[0]` 和 `parts[1]` 指针即告失效；若需继续读写，必须重新调用 `write_spans` 或 `read_spans`。
> 4. **越界与方向错误恢复**：若传入 `commit` / `consume` 的字节数超过了本次借用提供的总长度，函数返回 `HX_OS_RANGE`；若在读借用上调用了 `commit`（或在写借用上调用了 `consume`），函数返回 `HX_OS_INVALID`。这两种错误都不会销毁当前借用，调用方修正参数后仍可继续完成提交或消费。
> 5. **空／满时不建立借用**：对空缓冲调用 `read_spans` 返回 `HX_OS_NEED_INPUT`，对满缓冲调用 `write_spans` 返回 `HX_OS_OUTPUT_FULL`；这两种情况均不会进入借用状态。
> 6. **无自动提交**：在清空、重新初始化或让 `ring` 及底层数组离开作用域之前，必须先完成或取消活动借用；任何函数或 C++ 析构函数都不会自动替你 `commit` 未提交的数据。

## 4. C++11 与完整示例

C++11 头文件 `<hx/Ring.hpp>` 提供了 `hx::Ring::create(storage, bytes)` 以及 `write()`、`read()`、`discard()`、`clear()`、`size()`、`capacity()` 和 `space()`，所有可能失败的操作均返回 `hx::Result<T>`。
- 调用 `readSpans()` 和 `writeSpans()` 会返回包含 `parts[0]` 与 `parts[1]` 的片段结构，处理完毕后显式调用 `consume(bytes)` 或 `commit(bytes)`。
- `hx::Ring` 仅借用外部数组，支持移动构造（若原对象正处于借用状态，移动构造会将该未完成借用一并转移给新对象，原对象变为空壳），禁止复制构造或赋值。`hx::Ring` 析构时既不会自动提交数据，也不会释放外部传入的数组。

在启用 `-DHX_OS_BUILD_EXAMPLES=ON` 后构建并运行 `hx_os_byte_queue_ring` 即可查看完整的 [C 示例](../examples/byte_queue_ring.c)，输出如下：

```text
ByteQueue: 5 bytes: hello
Ring: efxxxxxx
```
