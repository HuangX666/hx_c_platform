# 通用数组与迭代器算法

[English](ALGORITHMS.md) | [容器](CONTAINERS.zh-CN.md)

当工程里已经有现成的 C 原生数组或自定义结构体数组，需要执行排序、二分查找、条件筛选、映射转换或前缀累加时，可以直接使用 `<hx_algorithm.h>` 提供的通用算法，无需先把数据搬进 `hx_os_vector` 等动态容器。C++11 工程也可以通过 `<hx/Algorithms.hpp>` 中的 `hx::algo` 命名空间直接操作标准迭代器区间与链表容器。

完整可运行的示例位于 [algorithms.c](../examples/algorithms.c)。该程序演示了常见的业务处理流水线：先将设备结构体数组按 ID 升序排列，再用 `hx_os_array_lower_bound` 定位目标设备 `23`，接着通过 `hx_os_array_count_if` 和 `hx_os_array_copy_if` 筛选出所有待处理的设备，最后用 `hx_os_array_fold` 汇总待办任务总数。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 `-DHX_OS_BUILD_EXAMPLES=ON` 并构建 `hx_os_algorithms` 目标即可直接运行（无需命令行参数或外部输入文件）：

```text
Device 17, pending=0
Device 23, pending=1
Device 31, pending=1
Found device 23
Devices needing work: 2
Copied work items: 2
Pending work total: 2
```

你也可以参照[最小工程配置](USAGE.zh-CN.md#c-project)将该示例源码作为自己工程的 `main.c` 进行实验。

## 1. 直接使用已有数据

`<hx_algorithm.h>` 中的 C99 函数直接操作连续内存中的普通数组和结构体数组；C++11 的 `<hx/Algorithms.hpp>` 则在 `hx::algo` 中提供与标准库同名的算法入口，接受符合对应迭代器类别要求的范围。

| 已有数据类型 | 推荐调用方式 |
| --- | --- |
| 常用定长整数数组（`int32_t` / `uint32_t` / `int64_t` / `uint64_t`） | `hx_os_sort_i32(values, count, error)`（另有 `_u32`、`_i64`、`_u64`） |
| 任意 C 标量数组或自定义结构体数组 | `hx_os_array_sort(data, count, sizeof(data[0]), compare, user, error)` |
| C++ 原生数组、`std::array`、`std::vector` 等随机访问范围 | `hx::algo::sort(first, last, compare)` |
| C++ `std::list` 或 `std::forward_list` | `hx::algo::sort(list, compare)`（调用容器自带的稳定节点重连排序） |
| 已有 `hx_os_vector` 容器 | `hx_os_vector_sort`（与 `hx_os_array_sort` 共用底层排序实现） |

> [!NOTE]
> C 语言算法接口依赖 `HX_OS_ENABLE_CONTAINERS=ON` 构建开关，使用时需链接 `hx_c_platform::hx_c_platform`；若关闭该开关，相关 C 函数会返回 `HX_OS_NOT_SUPPORTED`。C++ 头文件 `<hx/Algorithms.hpp>` 中的模板实现不依赖此开关，通过安装包集成时直接链接 `hx_c_platform::cpp` 即可获得头文件路径。

## 2. 给 C 数组排序

对于 32 位和 64 位有符号／无符号整数数组，库内置了免比较器的排序与二分查找快捷函数。下面是一个完整的最小程序，运行后输出 `1 2 3 8`：

```c
#include <hx_algorithm.h>
#include <stdio.h>

int main(void)
{
	int32_t values[] = {8, 3, 1, 2};
	size_t count = sizeof(values) / sizeof(values[0]);
	size_t i;
	hx_os_status status;

	status = hx_os_sort_i32(values, count, NULL);
	if (status != HX_OS_OK)
		return 1;

	for (i = 0; i < count; ++i)
		printf("%d%c", (int)values[i], i + 1 == count ? '\n' : ' ');

	return 0;
}
```

对自定义结构体数组排序时，需要提供比较器回调（返回负数表示小于、零表示相等、正数表示大于）。算法在交换位置时会按 `sizeof(record)` 搬移整条结构体记录，保持内部各字段的对应关系。[设备示例](../examples/algorithms.c) 即以此方式对 `{id, pending}` 记录按 ID 排序并完成后续查询与汇总。

> [!TIP]
> 在编写整数键的比较器时，建议使用 `(a > b) - (a < b)` 而不是直接写成 `a - b`，以避免有符号整数相减溢出导致比较结果反转。

## 3. 按需求选择算法

下表列出了 `<hx_algorithm.h>` 中的核心排序、选择与查询算法（函数名统一省略 `hx_os_array_` 前缀）。设 `n` 为输入元素个数，`k` 为目标前缀长度，`s` 为单个元素的字节大小（`sizeof(element)`）。表中的时间复杂度按元素比较与交换次数计，单次回调与字节复制开销另计。

| 函数名（省略 `hx_os_array_`） | 功能说明 | 底层实现与空间成本 |
| --- | --- | --- |
| `sort` | 原地完整排序（不稳定） | 原地堆排序，时间 `O(n log n)`，零堆内存分配 |
| `stable_sort` | 稳定完整排序（相等键保持原有相对先后顺序） | 自底向上归并排序，时间 `O(n log n)`，需 `O(n × s)` 临时空间 |
| `lower_bound` / `upper_bound` | 在有序数组中查找首个不小于／首个大于目标键的插入下标 | 二分查找，要求输入已按同一规则排序，时间 `O(log n)`，零分配 |
| `binary_search` | 判断有序数组中是否存在等价元素（`found` 输出 `0` 或 `1`） | 二分查找，要求输入已排序，时间 `O(log n)`，零分配 |
| `find_if` / `count_if` | 查找首个满足谓词的元素下标／统计满足谓词的元素总数 | 线性扫描 `O(n)`，`find_if` 命中首个匹配项后立即返回 |
| `minmax` | 一次遍历同时获取首个最小值下标和首个最大值下标 | 成对比较扫描，时间 `O(n)`，零分配 |
| `is_sorted` | 检查数组是否已按非递减顺序排列 | 线性检查，遇到首处逆序立即停止，最坏 `O(n)`，空数组恒为真 |
| `reverse` / `rotate` | 原地反转数组／将元素向左循环旋转指定步数 | 原地交换，时间 `O(n)`，零分配；旋转步数自动对 `n` 取模 |
| `partition` | 将满足谓词的元素移到前面，返回匹配前缀的长度 | 原地双指针分区（不稳定），时间 `O(n)`，零分配 |
| `unique` | 合并相邻等价元素，返回去重后的有效前缀长度 | 线性压缩 `O(n)`，零分配；仅消除相邻重复项，通常先排序再调用 |
| `partial_sort` | 选出最小的 `k` 个元素并在数组头部排好序 | 堆选择，时间 `O(n log(k + 1))`（`k == 0` 时直接返回），零分配 |
| `nth_element` | 将排序后排在第 `nth` 位的元素放到最终位置，左侧不大于它、右侧不小于它 | 三路快速选择并在递归过深时回退堆排序，最坏 `O(n log n)`，需 1 个对齐元素的临时空间 |

> [!IMPORTANT]
> **边界与返回值约定：**
> - `lower_bound` 和 `upper_bound` 可能返回 `count`（表示所有元素均小于目标值，合法插入点在数组末尾）。解引用前必须先检查 `index < count` 并核对键值；若只需判断元素是否存在，请直接调用 `hx_os_array_binary_search`。
> - `find_if` 未找到匹配项，或对空数组调用 `minmax` 时，均返回 `HX_OS_NOT_FOUND` 且保持输出变量不变。`minmax` 的两个输出指针不能指向同一个变量。
> - `partial_sort` 和 `nth_element` 的目标下标越界时返回 `HX_OS_RANGE`；两者均不保证未选中部分的顺序，也不保证相等元素的稳定性。

## 4. 临时内存和记录归属

`hx_os_array_stable_sort` 和 `hx_os_array_nth_element` 需要临时暂存空间（Scratch Memory），因此额外接收 `context` 和 `max_scratch_bytes` 参数：
- `context` 传 `NULL` 时使用默认分配器；`max_scratch_bytes` 传 `0` 时使用默认的 16 MiB 临时内存预算。
- 该预算包含库自身的分配头和对齐填充开销（自定义分配器内部的元数据另计），所有临时空间都会在函数返回前释放。
- 若所需空间超出预算（返回 `HX_OS_LIMIT`）或分配失败（返回 `HX_OS_NO_MEMORY`），原数组内容保持不变。单元素数组的 `nth_element` 无需分配内存；多元素分区时，枢轴（Pivot）副本会自动维持输入数组基址与元素步长共同具备的对齐要求。

使用 C 数组算法时，请确保满足以下内存与生命周期前提：
1. **按字节可搬移（Trivially Relocatable）**：传入的元素步长通常为 `sizeof(array[0])`。所有元素必须支持直接按字节搬移（`memcpy` / `memmove`），库不会推断 C++ 构造／析构函数或容器的 `copy`/`drop` 回调。含有自引用指针或非平凡移动语义的 C++ 对象请改用第 5 节的 C++ 迭代器接口。
2. **去重尾部的处理**：`hx_os_array_unique` 仅通过字节覆盖将不重复的前缀向前压缩并返回新长度 `new_count`。`[new_count, count)` 区间内的尾部字节属于未指明状态，可能残留着重复的字节表示，**不能**再当作独立拥有资源的对象去访问或二次释放。如果元素持有外部动态资源且需要在去重时自动析构被淘汰项，请使用带 `drop` 回调的容器接口 `hx_os_sequence_unique`。
3. **比较器与并发约束**：比较器必须满足严格弱序（Strict Weak Ordering）；若数据包含浮点数 `NaN`，需在比较器中明确定义其排序位置。比较器与谓词回调不得修改输入元素，也不得抛出 C++ 异常或调用 `longjmp`。所有算法均在当前线程同步执行，不含内部锁；多线程共享同一数组时需在外部同步。

## 5. C++ 遵循标准迭代器规则

在 C++11 中包含 `<hx/Algorithms.hpp>` 即可使用 `hx::algo` 下的泛型算法。接口遵循标准库左闭右开区间 `[first, last)` 约定，返回值形态也与 `<algorithm>` 一致（如 `find` 返回迭代器、`binary_search` 返回 `bool`、`sort` 返回 `void`）：

```cpp
#include <hx/Algorithms.hpp>
#include <iterator>

int values[] = {8, 3, 1, 2};

hx::algo::sort(std::begin(values), std::end(values));
auto found = hx::algo::find(std::begin(values), std::end(values), 3);
```

| 算法入口 | 最低迭代器类别要求与说明 |
| --- | --- |
| `sort`、`partial_sort`、`nth_element` | 随机访问迭代器（`RandomAccessIterator`）；不能直接传入 `std::list` 迭代器 |
| `lower_bound`、`upper_bound`、`binary_search`、`equal_range` | 前向迭代器（`ForwardIterator`），输入必须按同一比较器有序；非随机访问迭代器上步进仍需 `O(n)` |
| `reverse` | 双向迭代器（`BidirectionalIterator`） |
| `rotate`、`partition`、`unique`、`remove_if` | 前向迭代器，且解引用类型支持移动或交换赋值 |
| `find`、`find_if`、`count`、`count_if` | 输入迭代器（`InputIterator`） |

此外，头文件还提供 `min_element`、`max_element`、`minmax_element`、`is_sorted`、`copy_if`、`transform` 和 `equal`。使用时需注意：
- 与标准库一样，`unique` 和 `remove_if` 仅把保留元素移到前面并返回逻辑新尾迭代器；若操作的是 `std::vector` 等可变长容器，还需接着调用 `container.erase(new_end, container.end())`（或直接使用第 8 节的 `hx::algo::erase_if`）。
- `copy_if` 与 `transform` 写入输出迭代器时，调用方需确保目标容量充足；C++11 三迭代器版本的 `equal(first1, last1, first2)` 要求第二个范围至少与第一个范围一样长。
- 对 `std::list` 和 `std::forward_list` 排序时，请直接调用整容器重载 `hx::algo::sort(items, compare)`。该重载委托给链表自身的稳定节点重连排序，不复制元素也不分配新节点。
- 为遵循本库无异常（No-Exception）且不隐藏内存分配失败的原则，`hx::algo` 未提供隐式申请堆内存的 `stable_sort` 或 `inplace_merge` 迭代器封装；若需带内存预算的稳定排序，请对连续数组调用 C 接口 `hx_os_array_stable_sort`。完整的 C++ 示例可参考 [algorithms_cpp.cpp](../examples/algorithms_cpp.cpp)（构建目标为 `hx_os_algorithms_cpp`）。

## 6. 直接筛选、转换、比较和累计 C 记录

除原地重排外，`<hx_algorithm.h>` 还提供面向普通 C 数组的筛选、转换、集合归并与前缀扫描函数（下表同样省略 `hx_os_array_` 前缀）：

| 函数名（省略 `hx_os_array_`） | 典型工程场景 | 行为与时间复杂度 |
| --- | --- | --- |
| `copy_if` | 将离线设备筛选复制到待处理数组 | 保持原有相对顺序写入目标缓冲区，`O(n)` |
| `transform` | 将原始采样记录转换为上报结构体 | 支持输入与输出元素步长不同，`O(n)` |
| `remove_if` | 就地剔除已完成的任务记录 | 稳定前移保留项并返回剩余元素个数，`O(n)` |
| `replace_if` | 将所有匹配记录替换为指定的新记录 | 原地按字节覆盖匹配项并返回替换次数，`O(n)` |
| `any_of` / `all_of` / `none_of` | 检查是否存在异常设备／是否全部就绪 | 输出布尔结果（`0` 或 `1`），条件确定后立即短路返回，最坏 `O(n)` |
| `fold` | 汇总总字节数或更新统计结构体 | 按顺序对每个元素调用回调以更新调用方的累加器，`O(n)` |
| `equal` / `mismatch` | 比对两份有序／无序记录列表或定位首处差异 | 显式传入两侧长度，时间 `O(min(n, m))` |
| `equal_range` | 获取有序数组中某键值的全部出现区间 | 返回左闭右开下标区间 `[first, last)`，`O(log n)` |
| `merge`（含 `HX_OS_MERGE` / `HX_OS_SET_*`） | 合并两路有序数组，或计算有序集合的并、交、差、对称差 | 线性归并 `O(n + m)`，遵循 multiset 重复元素计数语义 |
| `scan` | 计算分片报文的起始偏移量（前缀和）或每步累计快照 | 支持包含当前项（Inclusive）或排除当前项（Exclusive）模式，`O(n)` |

以上操作均在调用方提供的缓冲区上完成，**不会**在库内部额外分配堆内存。记录复制与 `scan` 快照均为浅字节拷贝；`replace_if` 的替换源记录必须位于输入数组之外。`equal` 和 `mismatch` 通过比较器判断逻辑相等，不会直接按字节比较结构体中的对齐填充位（Padding）；当两侧长度不同时，`equal` 直接判定为假，`mismatch` 在任一侧到达末尾时返回共同前缀长度。对空数组调用 `any_of`、`all_of`、`none_of` 的结果分别为 `0`、`1`、`1`。

常用定长整数数组的二分查找同样可以直接调用：

```c
int found = 0;

status = hx_os_binary_search_i32(values, count, 3, &found, NULL);
if (status == HX_OS_OK && found)
	puts("Found 3");
```

调用前请确保 `values` 已按数值升序排列。对应的 `_u32`、`_i64`、`_u64` 版本参数顺序完全一致。

## 7. 容量、进度和回调失败

像 `copy_if`、`transform`、`merge` 和 `scan` 这类向目标数组写入结果的算法，都会接收以**元素个数**为单位的输出容量 `capacity` 以及 `hx_os_array_progress` 进度结构体指针：
- 函数入口会先将 `progress` 中的 `left_consumed`、`right_consumed` 和 `written` 清零（即使参数校验失败也可安全读取零值；单输入算法的 `right_consumed` 恒为 `0`）。`fold` 也会在入口先将 `completed` 计数清零。
- **输出缓冲区写满（`HX_OS_OUTPUT_FULL`）**：当返回 `HX_OS_OUTPUT_FULL` 且 `progress.written == capacity` 时，表示目标数组已满，已成功写入的前缀完整保留。调用方只需将各输入指针向前推进对应的 `consumed` 个元素，并提供新的输出空间即可无缝续传。注意 `copy_if` 可能扫描多条输入才产出一个输出；当输出槽位耗尽时，下一个需要输出槽的元素不会被计入 `consumed`，续传时会再次对它调用谓词。
- **区分回调返回的 `HX_OS_OUTPUT_FULL`**：如果 `transform` 或 `scan` 返回 `HX_OS_OUTPUT_FULL`，但此时 `progress.written < capacity`，说明该状态码是由用户回调自己返回的错误，应按下方“回调失败”处理，而非目标数组写满。

> [!WARNING]
> **缓冲区不重叠与回调失败清理规则：**
> - **禁止输入输出重叠**：除 `transform` 允许输入与输出使用**完全相同的起始地址和相同的元素大小**（原地转换）外，任何输出缓冲区、累加器或进度变量都不得与输入数组发生内存重叠。
> - **回调中途失败**：若 `transform` 的回调在某条记录上返回错误，该记录不计入 `progress`，但目标槽位（或原地转换时的当前输入记录）可能已被部分改写，回调自身必须负责清理这部分半构造资源。`fold` 和 `scan` 在回调失败时保留累加器已发生的修改（仅成功完成的步数计入进度）。重试前需由业务代码先妥善恢复这些部分状态。

`hx_os_array_scan` 支持两种前缀快照模式：
- `HX_OS_SCAN_EXCLUSIVE`：在每次调用 `fold` 回调更新累加器**之前**，先将当前累加器复制到输出槽。
- `HX_OS_SCAN_INCLUSIVE`：在每次调用 `fold` 回调更新累加器**之后**，再将更新后的累加器复制到输出槽。

例如，输入长度数组为 `[2, 3, 5]`、累加器初值为 `0` 时，`HX_OS_SCAN_EXCLUSIVE` 输出各段的起始偏移 `[0, 2, 5]`，而 `HX_OS_SCAN_INCLUSIVE` 输出累计长度 `[2, 5, 10]`，两种模式下累加器的最终值均为 `10`。若累加过程中需要防止无符号整数溢出，请在回调内显式检查（如 `value > UINT64_MAX - sum` 时返回 `HX_OS_RANGE`）。

## 8. 更多 C++11 便利入口

`hx::algo` 同时提供了 `any_of`、`all_of`、`none_of`、`mismatch`、`merge`、四种有序集合运算（`set_union`、`set_intersection`、`set_difference`、`set_symmetric_difference`）、`replace_if`，以及 C++11 `<numeric>` 常用算法 `accumulate`、`inner_product`、`partial_sum`、`adjacent_difference` 和 `iota`，签名与前置条件均与标准库保持一致。

如果你希望直接从容器中物理删除满足条件的元素（省去手动组合 `remove_if` + `container.erase` 的样板代码），可以使用专为 `std::vector`、`std::list` 和 `std::forward_list` 提供的 `hx::algo::erase_if` 重载：

```cpp
std::vector<int> finished = {0, 4, 0, 8};

auto removed = hx::algo::erase_if(finished, [](int pending) noexcept {
	return pending == 0;
});

auto total = hx::algo::accumulate(finished.begin(), finished.end(), 0);
```

执行后 `removed` 为 `2`，`finished` 中保留 `[4, 8]`，`total` 为 `12`。`erase_if` 会调用容器自身的析构与擦除逻辑并返回被删除的元素个数，不会额外分配结果数组。请确保传入的谓词、元素移动／赋值与析构操作均满足 `noexcept` 约定。

## 9. 随机处理与专门查找

如果你需要对数组执行 Fisher–Yates 随机打乱（Shuffle）、无放回随机抽样（Sample）、稳定整数基数排序（Radix Sort），或者需要在二进制流中进行 KMP 单模式查找、Aho–Corasick 多模式匹配与布隆过滤器（Bloom Filter）查询，请参阅[二进制缓冲与查找算法](FAST_TOOLS.zh-CN.md)。
