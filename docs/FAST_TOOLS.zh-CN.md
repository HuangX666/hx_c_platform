# 二进制缓冲与查找算法

[English](FAST_TOOLS.md) | [通用算法](ALGORITHMS.zh-CN.md)

## 1. 持有二进制字节

`hx_bytes` 是与 `hx_string` 对应的二进制值。通过 `HX_BYTES_INIT` 初始化后，可以赋值、追加、预留和调整大小，接受 NUL 及任意编码，不补字符串终止符。扩展大小会填零。赋值／追加可引用当前值，失败保持不变。容量最多 64 MiB，扩容期间可能同时持有新旧缓冲。持有资源的结构应通过移动／复制接口处理，不能直接按结构复制；修改需要外部串行化。

```c
hx_bytes packet = HX_BYTES_INIT;
const unsigned char header[] = {0, 1, 2, 3};

hx_os_status status = hx_bytes_assign(&packet,
	(hx_os_bytes_view){header, sizeof(header)}, NULL);
if (status == HX_OS_OK)
	status = hx_bytes_append(&packet, (hx_os_bytes_view){"body", 4}, NULL);

hx_bytes_destroy(&packet);
```

C++ `hx::Bytes` 只能移动，`view()` 借用数据直到修改或销毁。字节缓冲始终属于核心，不是队列；顺序消费请用 ByteQueue／Ring。

## 2. 打乱、抽样和整数基数排序

C 算法直接处理普通数组和结构体数组，不要求先放入本库容器。

```c
int32_t values[] = {5, -9, 0, 12, 2};
int32_t selected[2];
size_t written = 0;
hx_os_rng rng;

hx_os_rng_seed(&rng, 1234);
hx_os_status status = hx_os_array_shuffle(values, 5,
	sizeof(values[0]), &rng, NULL);
if (status == HX_OS_OK)
	status = hx_os_array_sample(values, 5, sizeof(values[0]),
		selected, 2, &rng, &written, NULL);
if (status == HX_OS_OK)
	status = hx_os_radix_sort_i32(NULL, values, 5, 0, NULL);
```

打乱使用 Fisher–Yates，抽样最多选择输出容量项，不重复选择，保持被选项在原数据中的顺序，输入与输出不能重叠。元素必须允许按字节搬移。传入自有 RNG 可以避免逐元素系统调用；传 `NULL` 则为本次操作从系统随机数播种一次。示例用固定种子 1234，便于复现。

`hx_os_rng` 使用 xoshiro256** 和 SplitMix64 播种，有界整数通过拒绝采样消除取模偏差。它是快速、确定性的伪随机生成器，不能用于密码学。安全随机字节使用 `hx_os_random_bytes`，由系统提供；不承诺每次都直接读取物理硬件真随机源。

`hx_os_radix_sort_i32/u32/i64/u64` 使用稳定 LSD 基数排序，分别进行四／八个字节轮次，按真实数字升序排列，包含负数。实现通过 `memcpy` 读取数值，不依赖主机字节序。需要一份数组大小的暂存空间，默认预算 16 MiB，包含分配开销。分配失败或超限保持数组不变。小数组可能普通排序更快，复杂度不能代替实际性能测量。

C++ `hx::algo::shuffle` 遵守标准随机访问迭代器要求。C++11 也有 `sample`：前向输入支持普通输出迭代器，单遍输入需要随机访问输出完成蓄水池抽样。输出容量及不会抛异常的元素／迭代器操作由调用者保证。

```cpp
#include <hx/Algorithms.hpp>

int values[] = {5, -9, 0, 12, 2};
int selected[2];
std::mt19937 generator(1234);

hx::algo::shuffle(values, values + 5, generator);
auto end = hx::algo::sample(values, values + 5, selected, 2, generator);
```

`end - selected` 是实际选中的数量。C++ 迭代器入口与 C 的按字节搬移入口独立。

## 3. 查找字节模式

`hx_os_bytes_find` 使用 KMP，接受带长度的任意字节和 NUL。传入起始偏移及可选暂存预算，返回第一处匹配。空模式匹配起始位置，找不到返回 `NOT_FOUND` 且保留原偏移。同步调用期间借用输入。

```c
size_t offset = 0;
const unsigned char bytes[] = {'a', 0, 'b', 'c'};

hx_os_status status = hx_os_bytes_find(NULL,
	(hx_os_bytes_view){bytes, sizeof(bytes)},
	(hx_os_bytes_view){"\0b", 2}, 0, 0, &offset, NULL);
```

结果偏移为 1。预处理需要 O(模式长度) 暂存，默认上限 16 MiB；整体为 O(输入＋模式)，不会逐字节分配。

## 4. 一次查找多个模式

先从字节视图编译不可变 `hx_os_matcher`，然后对多条消息重复查询。稀疏 Aho–Corasick 自动机支持重叠和重复模式，回调报告模式序号、起点及长度。不同查询可以并发共用编译结果，销毁前需排除查询；回调不能销毁对象或抛异常／longjmp。

最多 4096 个模式、模式总字节 1 MiB、默认编译预算 16 MiB、每次最多交付 65536 处匹配。达到结果上限返回 `LIMIT`，同时给出已经交付的数量。失败链和输出链只维护一份，不为每个状态复制完整后缀列表。不启动后台任务，不逐匹配分配。C++ 使用 `hx::Matcher`。

## 5. 布隆过滤器

`hx_os_bloom_create(context, expected_items, false_positive_rate, budget, ...)` 创建有界成员过滤器。加入任意字节键后，用 `hx_os_bloom_contains` 查询。假说明从未加入，真可能是误报，仍需访问真实数据源确认。预期数量只用于计算容量，超过它会增加误报；不提供删除或自动扩容，`clear` 可以清空。

误报率参数范围为 1e-9..0.5，默认内存预算 16 MiB，最大 64 MiB。双重哈希在插入时不分配，属于非密码学哈希，不能作为认证或安全隔离。修改需串行化，不变数据的查询可以并发。C++ 使用 `hx::Bloom`。

## 6. 示例和裁剪

构建／运行 `hx_os_fast_algorithms`，查看打乱、抽样、基数排序、多个告警模式查找和布隆过滤。[fast_algorithms.c](../examples/fast_algorithms.c) 是完整程序。C 算法、匹配器和布隆过滤需要 `HX_OS_ENABLE_CONTAINERS`，关闭后返回 `NOT_SUPPORTED`；系统播种另需 RANDOM。核心字节缓冲和确定性 RNG 不需要全局初始化。
