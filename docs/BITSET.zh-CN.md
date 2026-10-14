# 动态位集合

[English](BITSET.md) | [容器](CONTAINERS.zh-CN.md)

## 1. 记录大量编号的状态

`hx_os_bitset` 为每个编号保存一个布尔标志，适合记录任务完成情况、槽位占用、设备选择和有限整数集合。编号从零开始。C++11 包装为 `<hx/Bitset.hpp>` 中的 `hx::Bitset`，通过 `Result` 返回结果，不使用异常。

启用 `HX_OS_ENABLE_CONTAINERS` 并链接 `hx_c_platform::hx_c_platform` 即可，C++ 工程使用 `hx_c_platform::cpp`，不增加第三方依赖。关闭容器模块后，创建和操作返回 `HX_OS_NOT_SUPPORTED`。

[完整 C 示例](../examples/bitset.c) 记录 100 个任务，将编号 0–9 和 42 标记为完成，然后查询第一个未完成的任务。输出如下：

```text
Completed: 11 of 100
Next unfinished job: 10
```

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

| C 入口 | 用途 |
| --- | --- |
| `create(context, bit_count, max_bits, out, error)` | 创建全零位集合；`max_bits=0` 默认最多 1,048,576 位 |
| `resize(bits, bit_count, error)` | 扩大时新增位为零，缩小时丢弃超出范围的位 |
| `set(bits, index, value, error)` / `reset(bits, index, error)` | 设置或清除一个标志 |
| `test(bits, index, out, error)` | 读取一个标志，明确区分编号错误与值为假 |
| `set_range(bits, first, count, value, error)` | 按机器字批量修改半开区间 |
| `fill(bits, value, error)` / `clear(bits, error)` | 修改全部标志；`clear` 清位但不改变大小 |
| `count(bits)` | 统计置位数量；`size(bits)` 返回可用编号数量 |
| `find_next(bits, start, value, out, error)` | 从 `start` 开始，包含起点，查找置位或未置位的编号 |
| `apply(bits, other, operation, error)` | 就地执行 AND、OR、XOR、AND-NOT |
| `intersects(left, right, out, error)` | 判断两组编号是否存在交集 |
| `is_subset(left, right, out, error)` | 判断左侧所有置位是否也在右侧置位 |

表格省略了统一前缀 `hx_os_bitset_`。C++ 方法使用 camelCase，例如 `findNext()` 和 `isSubsetOf()`。

## 3. 大小、错误和资源归属

最大值限制的是逻辑位数，不代表分配器总占用。内部使用 32 位字，按需要在上限内成倍扩容。对象和分配元数据另占空间；扩容期间可能同时保留新旧缓冲。缩小保留已有容量；`destroy` 释放全部存储并清空调用者的指针，C++ 对象自动完成这一步。自定义 context 必须活到对象销毁之后。

扩大后新增位始终为零，包括先前缩小时删除的位置。逻辑大小之外的填充位不会参与计数或查找。内部布局不能直接作为序列化格式使用。

编号和区间越界返回 `HX_OS_RANGE`，超过配置上限返回 `HX_OS_LIMIT`，分配失败返回 `HX_OS_NO_MEMORY`。修改失败时大小和内容保持不变。输出参数仅在成功时修改，创建入口会先把句柄输出初始化为 `NULL`。

`find_next` 没找到时返回 `HX_OS_NOT_FOUND`。`start == size` 表示空范围，`start > size` 是越界。集合运算和关系查询要求两侧大小相同，不等长返回 `HX_OS_INVALID`。允许自身运算，例如与自己 XOR 会清空全部标志。

## 4. 并发和算法

这是普通动态 bitmap，**不是供多线程直接共享写入的原子位图**。只有没有修改和销毁时才能并发读取；共享修改需要外部同步。分配器回调不能重入同一个对象。

单位置读写是常数时间。计数、搜索、范围和集合操作按 32 位字处理，不逐位循环。GCC/Clang 使用位计数和最低置位查找内建函数，其他编译器使用可移植整数实现，不强行要求某个 CPU 指令集。如果编号很大且非常稀疏，现有哈希表或树容器可能比稠密位图更合适。
