# 固定字节环形缓冲

[English](RING.md) · [ByteQueue](BYTE_QUEUE.zh-CN.md) · [README](../README.zh-CN.md)

## 1. 选择存储方式

`hx_os_ring` 使用调用者提供的数组。环形对象和字节数组都可以放在栈上或静态存储区。它不会分配内存、扩容、覆盖未读数据、启动线程或等待数据。模块复用 `HX_OS_ENABLE_CONTAINERS` 构建开关；关闭后，返回状态的操作会报告 `HX_OS_NOT_SUPPORTED`。

希望按需增长、直接复制读写时，使用 `hx_os_byte_queue`；需要固定内存预算，或者直接访问缓冲的两个连续片段时，使用这里的固定环形缓冲。二者都不支持自行并发访问：跨线程共享时，锁必须覆盖完整操作或整段借用期间。没有内部互斥锁不代表这是无锁并发队列。

## 2. 普通读写

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

输出为 `hello`，不需要调用销毁函数。在环形对象或借用片段使用期间，`storage` 必须始终有效。已初始化的 C 环形对象不能复制，也不能直接修改成员。重新初始化会丢弃原有数据，只能在全部操作与借用结束后进行；初始化允许接收未初始化对象，因此不会读取旧状态。

`size()` 是已保存字节数，`capacity()` 是数组的准确长度，`space()` 是二者之差。`clear()` 丢弃数据但保留数组。对象的管理信息占用数组之外的额外内存。零容量对象有效，允许零字节复制，但不能保存数据。

| 操作 | 数据或空间不足 | 失败影响 |
| --- | --- | --- |
| `hx_os_ring_write` | `HX_OS_OUTPUT_FULL` | 原有数据不变 |
| `hx_os_ring_read` | `HX_OS_NEED_INPUT` | 环形内容和目标缓冲都不变 |
| `hx_os_ring_read(..., NULL, bytes, ...)` | `HX_OS_NEED_INPUT` | 仅成功时准确丢弃 `bytes` 字节 |

普通复制接口的输入、输出不能与环形对象或底层数组重叠。这里没有 EOF 或生产者关闭状态。所有长度单位都是字节，与字符编码无关。

## 3. 借用两个片段，省去中间复制

数据绕到数组开头时，逻辑内容分为两个连续片段。流顺序始终是先第一段、再第二段。借用会一次返回两段；未使用的第二段为 `{NULL, 0}`。

```c
hx_os_ring_write_span parts[2];
hx_os_status status = hx_os_ring_write_spans(&ring, parts, NULL);

if (status == HX_OS_OK) {
	/* Example: initialize one byte directly in available ring storage. */
	parts[0].data[0] = 'A';
	status = hx_os_ring_commit(&ring, 1, NULL);
}
```

对应的读取方式是 `hx_os_ring_read_spans()` 配合 `hx_os_ring_consume()`。解析器可以直接查看两段数据，只消费已经识别的前缀。`commit()` 只发布调用者已经填好的前缀，其余部分仍为空闲空间。这里省去的是中间缓冲复制，不代表操作系统或业务流程中的所有复制都消失了。

借用期间需要遵守：

- 同一时刻只允许一个读借用或写借用。结束前再次借用、复制读写或 `clear()` 都返回 `HX_OS_BUSY`。
- `commit(0)` 或 `consume(0)` 显式放弃相应借用，不改变有效字节数。
- commit/consume 成功后，两段指针都失效，即使只提交或消费了部分。继续处理时必须重新借用。
- 完成数量超过借用范围时返回 `HX_OS_RANGE`，原借用仍然有效，可以重试。使用错误方向结束借用时返回 `HX_OS_INVALID`，同样保留原借用。
- 空缓冲的读借用返回 `HX_OS_NEED_INPUT`，满缓冲的写借用返回 `HX_OS_OUTPUT_FULL`；这两种失败不会建立借用。
- 清空、重新初始化、结束对象或数组的生命周期前，先结束借用。函数和析构函数都不会自动提交数据。

## 4. C++11 与完整示例

`<hx/Ring.hpp>` 提供 `hx::Ring::create(storage, bytes)`、`write()`、`read()`、`discard()`、`clear()` 和容量查询。操作返回 `hx::Result`。`readSpans()`、`writeSpans()` 的结果通过 `parts[0]`、`parts[1]` 提供两段数据，需要显式调用 `consume()`、`commit()`。包装类借用数组，支持移动构造，不支持复制或赋值。移动会把尚未结束的借用交给新对象；被移动的原对象变为空对象。析构不会提交数据，也不会释放调用者的数组。

开启 examples 后，构建 `hx_os_byte_queue_ring` 即可运行完整的 [C 示例](../examples/byte_queue_ring.c)。示例同时演示可增长 ByteQueue 的读取，以及绕过数组末尾的固定环形缓冲。预期输出：

```text
ByteQueue: 5 bytes: hello
Ring: efxxxxxx
```
