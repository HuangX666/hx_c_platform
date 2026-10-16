# C 容器

[English](CONTAINERS.md) · [README](../README.zh-CN.md)

包含 `hx_container.h` 或 C 总头 `hx_os.h` 即可使用。实现按 C99 编译，公共接口使用 C 类型和不透明句柄，无需 C++ 运行库。 `HX_OS_ENABLE_CONTAINERS` 默认 ON；关闭后排除序列、树和 uthash 实现，创建接口返回 `HX_OS_NOT_SUPPORTED`，能力位 `HX_OS_CAP_CONTAINERS` 清除。不需要进程级初始化。

## 已提供的容器

需要混合类型时，使用独立的 [variant 模块](VARIANT.zh-CN.md)，可以拥有数字、字符串和自定义结构体。hx_variant_container_options_init 为 vector、queue/deque 和 map 的值提供深复制/清理。owning hx_variant 可以搬移字节；不能把活跃的 hx::Variant C++ 对象交给按字节复制的容器。variant 内部的分配仍不计入 max_bytes。

| 容器 | 实现和操作 |
|---|---|
| vector | 连续数组；下标访问 O(1)，尾部追加均摊 O(1) |
| deque | 环形数组；下标访问和容量充足时的两端操作 O(1) |
| queue | deque 的 FIFO 用法，尾部入队、头部出队 |
| stack | vector 的 LIFO 用法，尾部入栈、出栈 |
| list | 双向链表；两端操作及迭代器步进 O(1) |
| heap | 大顶堆；入堆、出堆 O(log n)，下标零为堆顶 |
| 有序 map/set | 保留原 AVL，可选择红黑树；查找、插入、删除 O(log n)，按键顺序迭代 |
| multimap/multiset | 在 AVL、红黑树或哈希类型上加入 `HX_OS_MAP_MULTI`，保留重复键 |
| 哈希 map/set | uthash 2.4.0；预期查找 O(1)，按插入顺序迭代 |

复杂度不包括元素／键的字节数和回调耗时。链表按下标查找、插入、删除需要 O(n) 遍历；使用迭代器才能实现总计 O(n) 的完整遍历。vector/deque 中间插删为 O(n)，扩容会搬移并分配。哈希冲突可使操作退化到 O(n)；当前哈希没有抵抗恶意碰撞输入的保证。

序列共用 `hx_os_sequence_*` 接口，queue/stack 是便捷别名，不另建运行时实现或 DLL 导出。堆只接受尾部插入和下标零删除，任意下标修改返回 NOT_SUPPORTED。 `hx_os_vector_sort` 使用原地、不稳定的堆排序； `hx_os_vector_lower_bound` 要求 vector 已按同一严格弱序比较器排序。

这些是 C 容器和算法设施，没有 C++ 模板类型系统、完整的迭代器分类/ranges 框架或无锁并发队列。跨线程共享容器需要外部同步；协程 channel 是独立的有界通信设施。

## 选项、分配器与容量限制

使用 `hx_os_container_options_init(&options, sizeof(element_type))` 初始化选项，创建时复制配置。修改原始 options 不会改变已创建容器。默认最多 1,048,576 个元素、每个容器 16 MiB。嵌入式项目应按实际工作量调小；两个上限都必须非零。

`element_alignment=0` 使用通常的基础类型分配对齐。显式对齐值必须是二次幂，且元素大小必须是对齐值的倍数。过度对齐的类型需传正确的对齐值。vector 步长恰为 `element_size`，使用 `sizeof(T)` 保证 T 的数组步长。所有值必须允许按字节搬移，包括扩容、排序过程中。内部自引用结构和必须调用移动构造函数的 C++ 对象不能直接按值存放，可以按明确的外部所有权存放稳定指针。

`max_bytes` 限制库请求的存活分配字节数，包含容器对象、节点／桶元数据、对齐开销、临时值和扩容时同时存在的新旧缓冲区。 `sequence_memory`、`map_memory` 返回同一口径的当前值。该数值不包含自定义分配器自身元数据、复制／析构回调的分配，也不包含指针指向的对象图；深拷贝 hx_string 的字符缓冲区同样不在其中。替换操作为保留旧值需要临时空间，因此即使元素数不变也可能返回 LIMIT。

分配策略由传入的 context 持有，容器分配存在期间 context 保持忙碌。没有全局分配器替换。回调 user 和回调使用的其他 context 是借用，必须活到容器销毁完成。分配器必须允许消费者使用方式所涉及的跨线程调用。容器没有内部锁：修改时排除读者，修改与销毁需要外部串行化。不同容器可独立使用，但共享回调及其状态仍需满足线程安全条件。

数组类含 deque/heap 支持 reserve；链表返回 NOT_SUPPORTED。 clear 保留数组容量，释放链表／map 节点；没有自动缩容。 destroy 释放剩余元素和内存并清空所有者指针；对已为空的所有者再次销毁成功。

## 复制、接管与借用

| 操作 | 成功后的所有权 | 失败时 |
|---|---|---|
| 未设置 copy/drop 的插入 | 复制普通字节 | 来源不变 |
| 设置 copy 的插入 | 构造独立的拥有值 | 来源仍由调用者持有 |
| 只有 drop、没有 copy 的普通插入 | 返回 NOT_SUPPORTED | 来源仍由调用者持有 |
| `_take` 插入 | 接管释放责任 | 调用者仍持有原值 |
| pop/erase 输出非 NULL | 把字节和所有权交给调用者 | 输出不变 |
| pop/erase 输出 NULL | 调用 drop 后删除 | 保留原值 |
| clear/destroy | drop 所有剩余值 | BUSY 时保留容器 |

`_take` 成功不会清零来源。来源必须视为已移出，不能再次 drop，也不能拿容器已经拥有的元素再向同一容器转移所有权。 pop/erase 输出必须是大小和对齐正确、尚未持有资源的外部存储，不能与容器拥有的任何存储重叠。重叠检查只能捕获常见错误。指针元素默认只复制指针；需要深拷贝时设置 copy/drop。 copy 失败必须自行回收目标的部分资源；容器只对成功构造的值调用 drop。

回调不得抛异常、longjmp 或修改同一容器；用于只读查找的比较器也一样。比较结果必须始终一致。修改路径会用 BUSY 拒绝回调重入修改，这不是锁，也不是对任意回调重入的安全保证。回调可操作另一个容器，但不能递归回到当前活动容器。

`sequence_at`、map 查找及迭代器返回借用指针。 `vector_data` 仅向 vector/stack 暴露连续可写数组，其他表示返回 NULL。直接修改必须维持值的所有权和排序前提；修改后应保守地丢弃旧借用指针。成功的结构修改、替换、排序、clear 和 reserve 都使迭代器失效。所有者还存在时，旧代数会返回 INVALID；迭代器不持有容器引用，容器销毁后使用迭代器无效，不能靠代数检查规避悬空访问。 begin/next 到末尾返回 EOF；空容器 pop 和键不存在返回 NOT_FOUND。只有 OK 时才能使用输出。失败修改保留逻辑内容，但容量／哈希元数据可能变化。

## 键与 set

使用 `HX_OS_MAP_HASH` 或 `HX_OS_MAP_ORDERED` 创建 map。键是复制的字节序列，长度单位为字节；允许嵌入 NUL 和零长度键，共同的键长上限为 UINT_MAX。容器不验证 UTF-8，不做编码转换、归一化或 locale 排序；需要时在插入前使用 hx_string 和文本转换接口。

哈希键按字节相等判断，不接受自定义键比较器。有序 map 使用传入比较器，缺省按无符号字节字典序比较。二进制整数键需要比较器才能按数值排序。键只保证字节对齐；以 memcpy 读到局部变量后再按类型比较。比较器收到 options 中借用的 user。替换值保留原始键字节和迭代位置。

将 `element_size` 设为零、copy/drop 及 value 设为 NULL 即得到 set，共用 map 操作；找到键时返回 OK，输出 value 为 NULL。 `replace=0` 对已有键返回 EXISTS，非零允许插入或替换。 map 值遵循与序列相同的复制、接管和释放规则。

## 示例

[独立队列示例](../examples/containers.c) 按 C99 编译，也用于安装后的消费者检查。保存字符串时使用深拷贝预设，不能直接浅拷贝拥有内存的 hx_string：

~~~c
hx_os_container_options options;
hx_os_vector *names = NULL;
hx_string source = HX_STRING_INIT;
hx_os_status status;

hx_os_string_container_options_init(&options, NULL);
options.max_items = 32;
status = hx_os_vector_create(NULL, &options, &names, NULL);
if (status == HX_OS_OK)
	status = hx_string_assign_cstr(&source, "中文", NULL);
if (status == HX_OS_OK)
	status = hx_os_sequence_push_back(names, &source, NULL);
/* On OK the vector owns an independent copy. Inspect status in the host. */
hx_string_destroy(&source);
(void)hx_os_sequence_destroy(&names, NULL);
~~~

使用自定义 context 时，预设和容器构造传同一个 context。把字符串提取到外部存储后，调用者需在使用完毕时调用 `hx_string_destroy`。若用 `_take` 把字符串交给容器，来源应重置为 `HX_STRING_INIT`，不要先析构已交出的字符缓冲区。

## 红黑树和重复键

创建时选择 `HX_OS_MAP_RED_BLACK` 即使用红黑树；原 `HX_OS_MAP_ORDERED` 继续使用 AVL，没有改变旧枚举值或公共结构体布局。与 `HX_OS_MAP_MULTI` 按位或，便可保留重复键。multi 模式下，`replace=0` 新增一个同键项目，`replace=1` 替换其中一个。有序容器的同键项目保持插入顺序，find/erase 操作第一个同键项目；哈希同键项目的查找顺序不作保证。

[完整红黑树程序](../examples/ordered_map.c) 依次插入 `b:20`、`a:10`、`b:21`，输出：

```text
a: 10
b: 20
b: 21
```

关键调用只有创建容器和插入值：

```c
hx_os_map_create(NULL, HX_OS_MAP_RED_BLACK | HX_OS_MAP_MULTI,
	&options, NULL, &map, &error);
hx_os_map_insert(map, "b", 1, &value, 0, &error);
```

`hx_os_map_lower_bound`、`upper_bound`、`equal_range` 用于有序树。`count` 可用于全部后端，`erase_all` 删除所有同键项目，也允许查询键借用容器内部存储。`erase_at` 删除当前项目，并将传入的迭代器更新到下一项；其他迭代器失效。`last`、`previous` 提供反向遍历。`validate` 不分配内存，检查 AVL/RB 不变量和树/链表的一致性，但不能用于验证任意损坏的内存指针。

## 遍历和算法

先声明 iterator/status，再使用循环宏。宏只计算一次容器表达式，遇到 EOF 结束，保留错误状态；主动 break 时状态仍为 OK。修改容器会让普通迭代器失效，边遍历边删除时应显式使用 `erase_at`。

```c
hx_os_sequence_iterator iterator;
hx_os_status status;
HX_OS_SEQUENCE_FOR_EACH(iterator, values, status)
{
	const void *value;
	status = hx_os_sequence_value(&iterator, &value);
	if (status != HX_OS_OK)
		break;
	printf("%d\n", *(const int *)value);
}
```

map 有对应的 `HX_OS_MAP_FOR_EACH`，两类容器都有反向遍历宏。需要提前停止、详细错误或累积结果时，也可使用回调接口。

| 要做的事 | 入口和行为 |
| --- | --- |
| 按条件查询 | `find_if`、`count_if`、`any_of`、`all_of`、`none_of`，后三个找到答案便停止 |
| 逐项访问、累积 | `visit`、`fold`，首次失败停止，保留此前回调效果并报告进度 |
| 复制、筛选、转换 | `clone`、`copy_if`、`transform` 返回新 vector，失败不改变源容器 |
| 删除、替换 | `remove_if`、相邻去重 `unique`、`replace_if`；替换遇到首次复制失败即停止，保留前面已替换项 |
| 调整顺序 | `reverse`、`rotate`、`partition`，线性处理，不额外分配；链表 partition 保持相对顺序 |
| 稳定排序 | `stable_sort`，O(n log n)；vector/deque 使用 O(n) 临时空间，链表使用 O(1) 临时空间归并链接 |
| 部分排序 | vector 的 `partial_sort(k)` 为 O(n log k)；`nth_element` 围绕某个名次分区，并在深度达到限制时回退到堆排序 |
| 有序查询 | vector 的 `lower_bound`/`upper_bound`，要求已按同一比较器排序 |
| 合并、集合运算 | `merge` 支持 MERGE、UNION、INTERSECTION、DIFFERENCE、SYMMETRIC_DIFFERENCE，保留 multiset 的重复次数语义 |
| 移动链表区间 | `list_splice` 直接改链接，不复制、不释放元素，也不分配节点 |

除非另有说明，改变顺序的算法会拒绝 heap，避免破坏堆不变量。谓词不能修改同一个容器。成功调整顺序后迭代器失效；排序前分配失败则不改变容器或迭代器。临时空间计入容器字节预算。transform/copy 回调构造元素失败时，需要自行释放该元素的部分构造资源。新 vector 继承源 context/容量限制；有序 merge 要求元素、回调和 user 策略一致。splice 要求 context 和值策略兼容，检查目标容量，并转移节点的内存计数；把同一个链表的区间移到自身区间内部时不做操作。

AVL 和红黑树没有一方在所有场景下都更快。应结合自己的工作负载比较分配开销和操作耗时，不把元数据变小直接当成运行性能提升。
