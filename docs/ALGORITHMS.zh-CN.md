# 通用数组与迭代器算法

[English](ALGORITHMS.md) | [容器](CONTAINERS.zh-CN.md)

**先运行一个完整例子。** 已经有 C 数组或结构体数组，要排序、查询或筛选时，可以直接使用通用算法，无需搬进另一个容器。

打开 [algorithms.c](../examples/algorithms.c)。按设备 ID 排序 → lower_bound 找 23 → count_if 统计待处理项 → copy_if 复制它们 → fold 累计数量。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 examples，并只构建 `hx_os_algorithms`。此例无命令行参数、无需输入文件。

**会得到什么：**输出 ID 顺序 17、23、31，找到设备 23，待处理设备和累计任务数均为 2。修改记录字段后，同时修改比较器和谓词。

下面的短片段用于解释接口；复制运行时以完整示例为准。也可按[最小工程配置](USAGE.zh-CN.md#c-project)把完整源码作为自己的 main。

## 1. 直接使用已有数据

`<hx_algorithm.h>` 中的 C99 函数直接操作普通数组，也支持你自己的结构体数组，不需要先创建 `hx_os_vector`。C++11 的 `<hx/Algorithms.hpp>` 在 `hx::algo` 中提供标准算法名称，接受符合对应要求的标准迭代器。

| 已有数据 | 调用方式 |
| --- | --- |
| 常用整数数组 | `hx_os_sort_i32(values, count, error)`，另有 u32/i64/u64 |
| C 整数数组或结构体数组 | `hx_os_array_sort(data, count, sizeof(data[0]), compare, user, error)` |
| C++ 数组、`std::array`、vector 等随机访问范围 | `hx::algo::sort(first, last, compare)` |
| C++ list 或 forward_list | `hx::algo::sort(list, compare)` 调用容器的稳定节点排序 |
| 已有 C 库 vector | 保留 `hx_os_vector_sort`，与普通数组入口共用排序实现 |

C 接口需要启用 `HX_OS_ENABLE_CONTAINERS` 并链接 `hx_c_platform::hx_c_platform`；关闭后返回 `HX_OS_NOT_SUPPORTED`。C++ 标准算法头不依赖这个开关，使用安装包时可通过 `hx_c_platform::cpp` 获得头文件路径。

## 2. 给 C 数组排序

下面是完整程序，输出 `1 2 3 8`。常用整数数组不需要自己编写比较器；`i32`、`u32`、`i64`、`u64` 都有对应的排序和二分查询快捷入口。

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

结构体数组的比较器读取所选字段，返回负数、零或正数；整数建议使用 `(a > b) - (a < b)`，避免直接相减溢出。算法会移动整条记录，保持各字段的对应关系。[设备示例](../examples/algorithms.c) 对 `{id, pending}` 按 ID 排序，通过 lower-bound 查询设备 23，再筛出待处理设备并累计任务数量。复用已有 examples 配置构建 `hx_os_algorithms`，输出如下：

```text
Device 17, pending=0
Device 23, pending=1
Device 31, pending=1
Found device 23
Devices needing work: 2
Copied work items: 2
Pending work total: 2
```

## 3. 按需求选择算法

下表入口统一省略 `hx_os_array_` 前缀。`n` 是输入数量，`k` 是要选择的前缀数量，`s` 是完整元素的字节大小。复杂度按元素操作次数计算，比较和字节搬移的成本另算。

| 入口 | 得到什么 | 当前实现及成本 |
| --- | --- | --- |
| `sort` | 完整排序，不稳定 | 原地堆排序，O(n log n)，不分配内存 |
| `stable_sort` | 相同键保留原来的先后顺序 | 自底向上归并排序，O(n log n)，O(n × s) 临时空间 |
| `lower_bound` / `upper_bound` | 第一个不小于 / 大于查询键的插入位置 | 要求输入有序，O(log n) 次比较，不分配内存 |
| `binary_search` | `found` 为 0 或 1 | 要求输入有序，O(log n)，不分配内存 |
| `find_if` / `count_if` | 首个匹配下标 / 匹配数量 | O(n)，find 找到后提前停止 |
| `minmax` | 首个最小值和首个最大值的下标 | 一次遍历，O(n)，不分配内存 |
| `is_sorted` | 是否已经按非递减顺序排列 | 最坏 O(n)，遇到首个逆序即停止 |
| `reverse` / `rotate` | 反转记录 / 向左旋转指定数量 | O(n)，不分配内存；旋转数量对 n 取模 |
| `partition` | 匹配项排到前面，返回前缀长度 | O(n)，不分配内存，不稳定 |
| `unique` | 压缩相邻重复项，返回新长度 | O(n)，不分配内存；非相邻重复项不会消失 |
| `partial_sort` | 最小的 k 项在前面排好序 | k > 0 时 O(n log(k + 1))，不分配内存 |
| `nth_element` | 指定排序名次的记录放到对应位置 | 三路分区，深度到限回退堆排序；最坏 O(n log n)，一个对齐记录的临时空间 |

上下界可能返回 `count`，它是合法的插入位置，不表示找到记录。读取结果前要检查下标和键；只关心是否存在时，可以直接使用 `binary_search`。`find_if` 未找到、空数组的 `minmax` 返回 `HX_OS_NOT_FOUND`，输出参数不变。`minmax` 的两个输出变量必须不同。空数组的 `is_sorted` 为真。选择下标越界返回 `HX_OS_RANGE`；`partial_sort` 的前缀数量为零时不做工作。两种选择算法都不保证后半部分有序，也不保证稳定性。

## 4. 临时内存和记录归属

`stable_sort` 和 `nth_element` 接受 `context`、`max_scratch_bytes`。context 传 `NULL` 使用默认分配器，上限传 `0` 使用默认 16 MiB 临时预算。预算包含库的分配头和对齐开销，分配器自身的元数据另算。临时存储在返回前释放。遇到 `HX_OS_LIMIT` 或 `HX_OS_NO_MEMORY` 时，原数组保持不变。单元素的名次选择不分配内存；pivot 副本维持数组地址与元素步长共同具备的对齐。

传入完整元素步长，通常就是 `sizeof(array[0])`。调用者提供有效的连续存储；库检查乘法及地址范围溢出，但无法验证指针后面究竟分配了多少内存。所有记录必须允许按字节搬移。库不会推断构造、析构、copy/drop 回调或资源转移。需要非平凡移动和析构的 C++ 对象应使用 C++ 迭代器接口。

`unique` 压缩普通记录并返回有效前缀长度，尾部内容不作保证，可能包含重复的字节表示，不能继续作为额外的拥有值使用。被删除值需要释放资源时，使用现有 `hx_os_sequence_unique` 和对应 drop 策略。排序本身则保留每一条记录。

比较器必须给出严格弱序，谓词不能修改输入，回调不能抛异常或 longjmp。浮点数包含 NaN 时，需要明确合适的比较规则。所有调用同步完成，不启动线程、不加内部锁。共享修改要在外部同步，回调的 user 在调用期间保持有效，输出变量不能与输入数组重叠。

## 5. C++ 遵循标准迭代器规则

```cpp
#include <hx/Algorithms.hpp>
#include <iterator>

int values[] = {8, 3, 1, 2};

hx::algo::sort(std::begin(values), std::end(values));
auto found = hx::algo::find(std::begin(values), std::end(values), 3);
```

范围是 `[first, last)`。返回值沿用标准算法：`find` 返回迭代器，`binary_search` 返回 bool，`sort` 返回 void。包装不捕获异常，也不把异常转换为 `Result`。元素比较、移动、交换、赋值和输出迭代器操作必须遵守本项目的不抛异常约定。

| 操作 | 迭代器要求 |
| --- | --- |
| `sort`、`partial_sort`、`nth_element` | 随机访问，不能直接传 list 迭代器 |
| `lower_bound`、`upper_bound`、`binary_search`、`equal_range` | 至少前向迭代器，范围必须按比较器有序；非随机访问时推进迭代器仍可能需要 O(n) |
| `reverse` | 双向迭代器 |
| `rotate`、`partition`、`unique`、`remove_if` | 至少前向迭代器，值支持对应修改操作 |
| `find`、`find_if`、`count`、`count_if` | 输入迭代器 |

头文件还提供 `min_element`、`max_element`、`minmax_element`、`is_sorted`、`copy_if`、`transform`、`equal`，遵循各自的 C++11 要求。`unique`、`remove_if` 返回逻辑新尾，使用可变长度容器时需要继续 erase 尾部。`copy_if`、`transform` 的输出空间由调用者保证。C++11 的三迭代器版本 `equal` 要求第二个范围足够长。

`std::list`、`std::forward_list` 使用 `hx::algo::sort(items, compare)`。这个明确的整容器重载调用稳定的成员排序，重新连接已有节点，不复制值、不分配节点；它不代表任意 list 迭代器区间都能调用随机访问排序。

当前未提供 `hx::algo::stable_sort` 或 `inplace_merge` 别名，避免引入无法通过错误结果报告分配失败的隐式临时内存路径。C 数组可以使用上述带预算的稳定排序。[C++ 设备示例](../examples/algorithms_cpp.cpp) 的目标为 `hx_os_algorithms_cpp`，演示数组排序、查询和链表排序。这些入口提供直接调用和共用实现，不宣称比标准库普遍更快。

## 6. 直接筛选、转换、比较和累计 C 记录

这些入口也直接使用普通数组，不要求先创建本库容器。表中统一省略 `hx_os_array_` 前缀。

| 入口 | 实际用途 | 结果和成本 |
| --- | --- | --- |
| `copy_if` | 把离线设备复制到待处理列表 | 按原顺序写入调用者数组，O(n) |
| `transform` | 把设备记录转换成上报记录 | 输入输出元素大小可以不同，O(n) |
| `remove_if` | 就地移除处理完的项目 | 稳定压缩，返回剩余数量，O(n) |
| `replace_if` | 用一个新值替换匹配的普通记录 | 返回替换数量，O(n) |
| `any_of` / `all_of` / `none_of` | 是否有设备断线？任务是否全部完成？ | 输出布尔结果，提前结束，最坏 O(n) |
| `fold` | 累计总字节数或更新统计结构体 | 按顺序修改调用者的累加器，O(n) |
| `equal` / `mismatch` | 比较人员列表或找到第一处差异 | 同时传入两边长度，O(min(n,m)) |
| `equal_range` | 找到有序数组中某个键的所有出现位置 | 返回半开区间 `[first,last)`，O(log n) |
| `merge` 的 MERGE/SET_* 模式 | 合并有序数据，求新增和删除的人员 ID | O(n+m)，沿用已有 multiset 重复次数规则 |
| `scan` | 计算每条报文起始偏移或每一步累计值 | 包含当前项或排除当前项的累加器快照，O(n) |

以上操作都不由库额外分配内存。复制记录和生成快照只是浅字节复制，不暗中 clone/drop。删除拥有资源的值时，使用带所有权回调的容器接口。`replace_if` 的替换记录必须放在输入数组之外。`equal`、`mismatch` 按比较器判断逻辑相等，不直接拿结构体填充字节作判断；一个或两个数组结束时，`mismatch` 返回共同前缀长度，长度不同则 `equal` 为假。空数组的 any/all/none 分别为假、真、真。

整数查找也可以直接调用：

```c
int found = 0;

status = hx_os_binary_search_i32(values, count, 3, &found, NULL);
if (status == HX_OS_OK && found)
	puts("Found 3");
```

输入须已经按数值排序。无符号和 64 位版本参数顺序相同，覆盖对应整数类型的完整取值范围。

## 7. 容量、进度和回调失败

`copy_if`、`transform`、`merge`、`scan` 接收以**记录数**计的输出容量和 `hx_os_array_progress`。其中 `left_consumed`、`right_consumed`、`written` 在入口先清零，参数验证失败时也可以读取零进度；单输入算法的 `right_consumed` 为零。`fold` 同样先清零 `completed`，其他普通查询的输出仍然仅在成功时修改。

返回 `HX_OS_OUTPUT_FULL` 且 `progress.written == capacity` 时，表示输出缓冲已满，已经生成的前缀保留。根据各输入的 consumed 数量前移输入指针，并提供新的输出空间即可继续。筛选可能读取多条记录才输出一条；需要新输出槽的记录不会在容量耗尽时被消费，重试时可能再次计算这条记录的谓词。若 transform/scan 返回 OUTPUT_FULL 但 `written < capacity`，则是回调自己的返回值，应按下面的回调失败规则处理，不能直接当成输出缓冲耗尽。容量不足和参数错误都不会触发隐式扩容。

输出不能和输入重叠。唯一例外是 `transform` 的输入输出起始地址完全相同、元素大小也相同，回调必须能够处理输入和目标为同一地址。部分重叠会被拒绝。scan 的累加器与输入、输出都必须分开。进度和数量变量不能放在任何数据缓冲内部。

transform 失败记录不计入进度，但它的目标空间可能已经被修改，回调负责清理当前记录的部分构造资源；精确原地转换也可能修改失败的输入记录。此前成功的输出继续有效。fold/scan 保留累加器已经发生的修改，包括失败回调自身的部分修改，只把成功回调计入进度。回调提供的原生错误详情会保留。回调错误后的重试，需要业务先处理这些部分效果，不能照搬 OUTPUT_FULL 时的直接续传方式。

`scan` 的 `HX_OS_SCAN_EXCLUSIVE` 在每次 fold 前复制累加器，`HX_OS_SCAN_INCLUSIVE` 在 fold 后复制。长度 `[2,3,5]`、初值零时，分别产生偏移 `[0,2,5]` 或累计值 `[2,5,10]`，最终累加器都是 10。溢出和失败规则由 fold 回调明确，例如累计无符号字节数时先检查 `value > UINT64_MAX - sum`。快照中的指针继续借用原资源，不会变成独立深拷贝。

## 8. 更多 C++11 便利入口

`hx::algo` 还提供 `any_of`、`all_of`、`none_of`、`mismatch`、`merge`、四种 `set_*`、`replace_if`，以及 C++11 数值算法 `accumulate`、`inner_product`、`partial_sum`、`adjacent_difference`、`iota`，沿用标准签名和前置条件。C++11 的三迭代器 `mismatch` 要求第二个范围足够长；有序集合运算要求输入已排序；数值算法使用所选类型自身的算术规则，不会自动检测溢出。

需要真正删除元素而不只是得到逻辑新尾时，可以使用明确支持 `std::vector`、`std::list`、`std::forward_list` 的 `erase_if` 重载：

```cpp
std::vector<int> finished = {0, 4, 0, 8};

auto removed = hx::algo::erase_if(finished, [](int pending) noexcept {
	return pending == 0;
});

auto total = hx::algo::accumulate(finished.begin(), finished.end(), 0);
```

这里 `removed` 为 2，`finished` 剩下 `[4,8]`，`total` 为 12。重载返回删除数量，执行容器正常析构，不创建额外结果数组；迭代器失效遵循对应容器的规则。谓词、元素移动/赋值、析构和迭代器操作不得抛异常，库不会安装异常到状态码的转换层。不会根据名字猜测任意其他容器的 erase 行为。

## 9. 随机处理与专门查找

C 打乱／抽样、稳定整数基数排序、C++11 迭代器抽样、二进制 KMP、多模式匹配和布隆过滤见[二进制与查找工具](FAST_TOOLS.zh-CN.md)。它们补充通用比较算法，直接使用调用者已有的数组或迭代器区间。
