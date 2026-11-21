# 二进制缓冲与查找算法

[English](FAST_TOOLS.md) | [通用算法](ALGORITHMS.zh-CN.md)

当工程中需要持有可动态增长的原始二进制字节块（`hx_bytes`）、对数组执行随机打乱（Shuffle）或无放回抽样（Sample）、按 32/64 位整数键做线性时间稳定基数排序（Radix Sort），或者需要在二进制流与日志文本中执行 KMP 子串查找、Aho–Corasick 多模式同时扫描与布隆过滤器（Bloom Filter）成员初筛时，可以使用本页介绍的工具集。

完整可运行的组合示例位于 [fast_algorithms.c](../examples/fast_algorithms.c)：程序使用固定种子初始化伪随机数生成器 `hx_os_rng`，对整数数组依次执行 `hx_os_array_shuffle` 打乱、`hx_os_array_sample` 抽样和 `hx_os_radix_sort_i32` 基数排序，随后用 `hx_os_bytes_find` 查找单模式偏移，用编译好的 `hx_os_matcher` 一次扫描多条告警关键字，最后演示 `hx_os_bloom` 的插入与成员查询。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 `-DHX_OS_BUILD_EXAMPLES=ON` 并只构建 `hx_os_fast_algorithms` 目标即可直接运行（无需命令行参数或输入文件）。运行后将看到排序后的首尾边界为 `-3 .. 9`，单模式 `"error"` 位于字节偏移 `16`，多模式匹配器依次触发 `"warning"` 和 `"error"` 回调，以及布隆过滤器的查询输出。你也可以参照[最小工程配置](USAGE.zh-CN.md#c-project)将该示例源码作为自己工程的 `main.c`。

## 1. 持有二进制字节

`hx_bytes` 是与 `hx_string` 对应的原始二进制缓冲区容器。通过 `HX_BYTES_INIT` 初始化后，即可调用赋值（`assign`）、追加（`append`）、预留容量（`reserve`）和调整大小（`resize`）：
- 接受包含 `0x00`（`NUL`）和任意字节分布的二进制数据，不校验 UTF-8，也不会在末尾自动补字符串终止符。
- 调用 `hx_bytes_resize` 扩大长度时，新增尾部字节会自动清零。
- `assign` 和 `append` 允许传入借用自当前 `hx_bytes` 自身缓冲区的视图切片（即使触发扩容也能安全处理）；若分配失败或超出 `64 MiB` 容量上限，原有内容保持不变。

```c
hx_bytes packet = HX_BYTES_INIT;
const unsigned char header[] = {0, 1, 2, 3};

hx_os_status status = hx_bytes_assign(&packet,
	(hx_os_bytes_view){header, sizeof(header)}, NULL);
if (status == HX_OS_OK)
	status = hx_bytes_append(&packet, (hx_os_bytes_view){"body", 4}, NULL);

hx_bytes_destroy(&packet);
```

C++11 对应提供只可移动的 `hx::Bytes`（调用 `view()` 可获取借用视图，在后续修改或析构前有效）。`hx_bytes` 始终编译在核心库中；它表示一块连续的二进制值，若需按流顺序边写边消费，请使用 [ByteQueue](BYTE_QUEUE.zh-CN.md) 或 [Ring](RING.zh-CN.md)。

## 2. 打乱、抽样和整数基数排序

与[通用算法](ALGORITHMS.zh-CN.md)一样，这里的 C 函数直接操作普通数组或结构体数组，无需先将数据搬入本库容器：

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

- **随机打乱与抽样（`hx_os_array_shuffle` / `hx_os_array_sample`）**：
  - `shuffle` 采用 Fisher–Yates 原地洗牌算法；`sample` 执行无放回抽样，最多选出目标输出容量个元素，并**保持被抽中元素在原数组中的相对先后顺序**（输入与输出数组不得重叠，元素须支持按字节搬移）。
  - 传入调用方持有的 `hx_os_rng` 可避免每次调用都向操作系统请求随机种子；若将 `rng` 传为 `NULL`，函数会在内部用系统随机源自动播种一次（需开启 `HX_OS_ENABLE_RANDOM`）。
  - `hx_os_rng` 采用 `xoshiro256**` 算法（配合 `SplitMix64` 种子初始化与拒绝采样消除取模偏差），适合高性能确定性模拟与抽样，**不可用于生成密码学密钥**（安全随机字节请使用 `hx_os_random_bytes`）。
- **稳定整数基数排序（`hx_os_radix_sort_i32` / `u32` / `i64` / `u64`）**：
  - 采用低位优先（LSD）稳定基数排序，32 位和 64 位整数分别固定执行 4 轮和 8 轮按字节分桶，按真实数值升序排列（正确处理有符号负数的符号位翻转，且不依赖主机大小端字节序）。
  - 需要分配一份与输入数组等大的临时暂存空间（默认预算上限 `16 MiB`，含分配头开销）。若超出预算返回 `HX_OS_LIMIT`、分配失败返回 `HX_OS_NO_MEMORY`，失败时原数组保持不变。
- **C++11 迭代器洗牌与抽样**：`<hx/Algorithms.hpp>` 中的 `hx::algo::shuffle` 接受随机访问迭代器与标准随机数生成器；`hx::algo::sample` 在输入为前向迭代器（`ForwardIterator`）时支持任意输出迭代器，在单遍输入迭代器（`InputIterator`）时则要求输出为随机访问迭代器以执行蓄水池抽样（Reservoir Sampling）：

```cpp
#include <hx/Algorithms.hpp>

int values[] = {5, -9, 0, 12, 2};
int selected[2];
std::mt19937 generator(1234);

hx::algo::shuffle(values, values + 5, generator);
auto end = hx::algo::sample(values, values + 5, selected, 2, generator);
```

返回值 `end` 减去 `selected` 即为实际抽中的元素个数。

## 3. 查找字节模式

`hx_os_bytes_find` 使用 KMP（Knuth–Morris–Pratt）算法在二进制字节流中查找单个模式串，支持包含 `0x00`（`NUL`）的任意字节序列：

```c
size_t offset = 0;
const unsigned char bytes[] = {'a', 0, 'b', 'c'};

hx_os_status status = hx_os_bytes_find(NULL,
	(hx_os_bytes_view){bytes, sizeof(bytes)},
	(hx_os_bytes_view){"\0b", 2}, 0, 0, &offset, NULL);
```

- 上例从偏移 `0` 开始搜索长度为 2 的模式 `"\0b"`，成功命中并将 `offset` 设为 `1`。
- 空模式串（长度为 `0`）直接匹配起始偏移 `start_offset`；未找到匹配时返回 `HX_OS_NOT_FOUND` 且保持 `offset` 原值不变。
- KMP 前缀表预处理需要 `O(模式长度)` 的临时空间（默认预算上限 `16 MiB`），整体搜索时间复杂度为线性 `O(输入长度 + 模式长度)`，扫描过程中不再逐字节分配内存。

## 4. 一次查找多个模式

当需要在一批日志或网络报文中同时查找多个固定特征码或关键字时，可以先将模式列表编译为一个不可变的 `hx_os_matcher`（基于稀疏 Aho–Corasick 自动机，C++11 对应 `hx::Matcher`），然后对任意多条消息重复调用扫描接口：
- **支持重叠与重复模式**：扫描时每命中一处模式，就会同步调用一次用户回调，传入命中的模式序号（对应编译时的输入顺序）、在待查数据中的起始偏移与字节长度。
- **并发共享与生命周期**：编译完成后的 `hx_os_matcher` 是只读不可变的，**允许多个线程并发调用扫描接口共享同一个匹配器实例**（只需确保在所有扫描完成之后再调用销毁函数；回调内部不得销毁当前匹配器，也不得抛异常或 `longjmp`）。
- **容量与结果上限**：单次编译最多支持 `4096` 个模式串、模式总长度不超过 `1 MiB`、默认编译内存预算为 `16 MiB`。单次扫描最多交付 `65,536` 处匹配；若命中次数超出该上限，扫描函数返回 `HX_OS_LIMIT` 并通过输出参数报告已交付的匹配次数。自动机的失败跳转链（Failure Links）与输出链仅维护单份紧凑索引，扫描过程零堆内存分配。

## 5. 布隆过滤器

`hx_os_bloom_create(context, expected_items, false_positive_rate, budget, &bloom, error)`（C++11 对应 `hx::Bloom`）用于创建有界空间复杂度的高速集合成员初筛器：
- **查询语义（无漏报、有受控误报）**：写入任意字节键后，调用 `hx_os_bloom_contains` 查询。若结果为假（`0`），说明该键**绝对未曾加入过**，可直接省去昂贵的磁盘或数据库查询；若结果为真（`1`），表示“可能存在”（存在一定的假阳性误报概率），仍需回查真实数据源确认。
- **容量参数与操作边界**：`expected_items` 仅用于在创建时计算最优位宽与哈希次数，并非硬性插入上限（但实际插入量远超预期值会导致误报率上升）。布隆过滤器不支持删除单个元素，也不自动扩容；调用 `hx_os_bloom_clear` 可将全部位重置为空。
- **预算与并发规则**：目标误报率 `false_positive_rate` 取值范围为 `1e-9 .. 0.5`，默认内存预算为 `16 MiB`（最大允许 `64 MiB`）。内部采用非密码学双重哈希（Double Hashing），插入与查询均为零内存分配。修改操作需在外部串行化；数据不再修改时允许多个线程并发查询。

## 6. 示例和裁剪

在开启 `-DHX_OS_BUILD_EXAMPLES=ON` 后构建并运行 `hx_os_fast_algorithms`（源码见 [fast_algorithms.c](../examples/fast_algorithms.c)），即可一次性验证数组洗牌、无放回抽样、基数排序、KMP／Aho–Corasick 模式查找与布隆过滤器的完整行为。

> [!NOTE]
> **模块编译开关说明**：
> - `hx_bytes` 与确定性伪随机生成器 `hx_os_rng` 始终包含在核心库中，无需额外开关或全局初始化。
> - C 数组洗牌／抽样、基数排序、`hx_os_bytes_find`、`hx_os_matcher` 和 `hx_os_bloom` 依赖 `HX_OS_ENABLE_CONTAINERS=ON`；若关闭该开关，相关调用会返回 `HX_OS_NOT_SUPPORTED`。
> - 当 `shuffle` / `sample` 的 `rng` 参数传 `NULL` 以请求系统随机种子时，还需开启 `HX_OS_ENABLE_RANDOM=ON`。
