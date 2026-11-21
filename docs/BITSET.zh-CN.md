# 动态位集合

[English](BITSET.md) | [容器](CONTAINERS.zh-CN.md)

当工程中有一批从 `0` 开始连续编号的任务、槽位或设备 ID，每个编号只需要记录一个布尔状态（如“已完成／未完成”、“已占用／空闲”）时，使用动态位集合 `hx_os_bitset`（C++11 对应 `<hx/Bitset.hpp>` 中的 `hx::Bitset`）可以将每个状态压缩为 1 个二进制位（Bit），并直接按 32 位机器字批量执行区间置位、人口计数（Popcount）、查找下一个空闲位以及集合交并差运算，避免为每个编号分配独立结构体或哈希节点。

完整可运行的 C99 示例位于 [bitset.c](../examples/bitset.c)：程序调用 `hx_os_bitset_create` 创建包含 `100` 个标志位（最大允许扩容到 `1000` 位）的位集合，用 `hx_os_bitset_set_range` 将任务 `0..9` 批量标记为已完成，再用 `hx_os_bitset_set` 单独标记任务 `42`，随后通过 `hx_os_bitset_find_next` 查找第一个尚未完成的任务编号，最后销毁位集合。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 `-DHX_OS_BUILD_EXAMPLES=ON` 并构建 `hx_os_bitset` 目标即可直接运行（无需命令行参数或输入文件），预期输出如下：

```text
Completed: 11 of 100
Next unfinished job: 10
```

你也可以参照[最小工程配置](USAGE.zh-CN.md#c-project)将该示例源码作为自己工程的 `main.c`。

## 1. 记录大量编号的状态

`hx_os_bitset` 按从 `0` 开始的下标管理稠密布尔标志，适合任务完成位图、内存池／连接池槽位分配、设备掩码筛选以及有界非负整数集合。C++11 包装类 `hx::Bitset` 位于 `<hx/Bitset.hpp>`，所有可能失败的操作均通过 `hx::Result<T>` 返回状态，不使用异常。

在 CMake 中保持 `HX_OS_ENABLE_CONTAINERS=ON`（默认开启）并链接 `hx_c_platform::hx_c_platform`（C++ 工程链接 `hx_c_platform::cpp`）即可使用，无额外第三方依赖。若关闭 `HX_OS_ENABLE_CONTAINERS`，创建与操作接口会返回 `HX_OS_NOT_SUPPORTED`。

[完整 C 示例](../examples/bitset.c) 的等价 C++11 写法如下：

```cpp
#include <hx/Bitset.hpp>
#include <cstdio>

int main()
{
	auto result = hx::Bitset::create(100, 1000);
	if (!result)
		return 1;

	auto &completed = result.value();
	if (!completed.setRange(0, 10))
		return 1;
	if (!completed.set(42))
		return 1;

	auto next = completed.findNext(0, false);
	if (!next)
		return 1;

	std::printf("Next unfinished job: %zu\n", next.value());

	return 0;
}
```

## 2. 常用操作

下表列出了 `<hx_bitset.h>`（已包含在 `<hx_container.h>` 与 `<hx_os.h>` 中）提供的核心 C 接口，函数名统一省略 `hx_os_bitset_` 前缀；C++ `hx::Bitset` 提供同名的 `camelCase` 方法（如 `findNext()`、`setRange()`、`isSubsetOf()`）：

| C 接口（省略 `hx_os_bitset_` 前缀） | 功能与参数说明 |
| --- | --- |
| `create(context, bit_count, max_bits, out, error)` | 创建初始全为 `0` 的位集合；`max_bits = 0` 时默认上限为 `1,048,576` 位 |
| `resize(bits, bit_count, error)` | 调整逻辑位数：扩大时新增的高位全部清零，缩小时丢弃超出范围的高位 |
| `set(bits, index, value, error)` / `reset(bits, index, error)` | 将指定下标 `index` 的位设为 `1`/`0`（`reset` 固定清零） |
| `test(bits, index, out, error)` | 读取下标 `index` 的状态（写入 `*out` 为 `0` 或 `1`），明确区分下标越界错误与位值为 `0` |
| `set_range(bits, first, count, value, error)` | 按 32 位机器字批量将半开区间 `[first, first + count)` 设为 `1` 或 `0` |
| `fill(bits, value, error)` / `clear(bits, error)` | 将当前所有有效位全部置为 `value`；`clear` 将所有位清零但保持 `size` 不变 |
| `count(bits)` / `size(bits)` | `count` 统计当前置为 `1` 的总位数；`size` 返回当前可寻址的逻辑总位数 |
| `find_next(bits, start, value, out, error)` | 从下标 `start`（含 `start` 本身）向后搜索第一个值为 `value`（`0` 或 `1`）的下标 |
| `apply(bits, other, operation, error)` | 就地与另一个等长位集合执行按位 `AND`、`OR`、`XOR` 或 `AND_NOT` |
| `intersects(left, right, out, error)` | 判断两个等长位集合是否存在至少一个公共置位（交集非空） |
| `is_subset(left, right, out, error)` | 判断 `left` 中所有置为 `1` 的位在 `right` 中是否也全部为 `1` |

## 3. 大小、错误和资源归属

- **容量上限与内存管理**：`max_bits` 限制的是逻辑位数的最大上限（底层按 32 位机器字分配存储，并在上限内按需成倍扩容；对象头和分配器元数据另计，扩容期间会短暂同时持有新旧缓冲区）。调用 `resize` 缩减位数时会保留已分配的底层字容量，后续再次扩大时，所有新增位（包括曾被缩小裁掉的位置）都保证初始化为 `0`。尾部的未用填充位绝不会混入 `count` 统计或 `find_next` 搜索结果。`hx_os_bitset_destroy` 会释放全部底层内存并将调用方句柄置为 `NULL`（C++ `hx::Bitset` 析构时自动释放）。若传入了自定义 `hx_os_context`，该 `context` 必须活到位集合销毁之后。
- **状态码与失败保持**：
  - 下标或区间超出当前 `size` 时返回 `HX_OS_RANGE`。
  - 创建或 `resize` 位数超过 `max_bits` 时返回 `HX_OS_LIMIT`。
  - 内存分配失败返回 `HX_OS_NO_MEMORY`。
  - 任何修改操作失败时，位集合原有的逻辑大小和位内容均保持不变；查询操作的输出参数仅在返回 `HX_OS_OK` 时更新（`create` 会在入口处先将输出句柄初始化为 `NULL`）。
- **查找与集合运算边界**：
  - `hx_os_bitset_find_next` 未找到目标位时返回 `HX_OS_NOT_FOUND`。当 `start == size` 时视为空搜索范围并直接返回 `HX_OS_NOT_FOUND`；当 `start > size` 时返回 `HX_OS_RANGE`。
  - `apply`、`intersects` 和 `is_subset` 要求左右两个位集合的当前逻辑大小（`size`）完全相等，若大小不同则返回 `HX_OS_INVALID`。允许将同一个位集合同时作为左右操作数传入（例如对自身执行 `HX_OS_BITSET_XOR` 会将全部位清零）。

> [!NOTE]
> `hx_os_bitset` 的内部机器字数组属于内存运行表示，不作为跨平台磁盘／网络序列化格式使用。

## 4. 并发和算法

- **线程安全边界**：`hx_os_bitset` 是普通的动态位图容器，**不是供多线程直接无锁并发置位的原子位图（Atomic Bitmap）**。当没有任何线程修改或销毁位集合时，允许多个线程并发读取；一旦涉及修改或销毁，必须由外部互斥锁同步。
- **按字加速与稀疏场景选择**：单位置读写（`set` / `reset` / `test`）为 `O(1)` 常数时间。`count`、`find_next`、`set_range` 和集合关系运算均按 32 位机器字批量处理（最坏 `O(ceil(bit_count / 32))`），在 GCC/Clang 下自动利用编译器内建指令（如 `__builtin_popcount`、`__builtin_ctz`）加速，在其他编译器下回退到可移植的无分支位运算实现。如果你的业务 ID 跨度极大且高度稀疏（例如随机的 64 位设备号），使用 [C 容器](CONTAINERS.zh-CN.md)中的哈希表或红黑树 `set` 会比稠密位集合更节省内存。
