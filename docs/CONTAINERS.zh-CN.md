# C 容器

[English](CONTAINERS.md) · [README](../README.zh-CN.md)

当 C99 工程需要动态增长的数组、双端队列、链表、二叉堆、有序树（AVL／红黑树）或哈希映射，并且要求明确限制元素上限与内存预算（避免无界增长耗尽系统内存）时，可以使用 `<hx_container.h>`（或总头文件 `<hx_os.h>`）。所有容器均使用纯 C99 实现，通过不透明句柄与类型化选项配置，不依赖任何 C++ 运行时，也无需进程级全局初始化。

最简单的入门示例位于 [containers.c](../examples/containers.c)：程序初始化整型元素的容器选项，创建一个有界 FIFO 队列（`hx_os_queue`），入队整数 `42` 后再出队到本地变量并销毁容器。按[示例运行方法](../examples/README.zh-CN.md#run)启用 `-DHX_OS_BUILD_EXAMPLES=ON` 并构建 `hx_os_containers` 目标即可直接运行（无需命令行参数或外部文件），预期输出为：

```text
Queue result: 42
```

如果你需要按键自动排序并支持重复键，还可以构建并运行 `hx_os_ordered_map`（对应源码 [完整红黑树程序](../examples/ordered_map.c)），它会按 `a`、`b`、`b` 的键序遍历红黑树并完整保留两个 `b` 对应的值。初次接触时，也可以先阅读[保存两个整数和依次取出任务](USAGE.zh-CN.md#containers)中的快速上手教程，或参考[最小工程配置](USAGE.zh-CN.md#c-project)将示例作为自己工程的 `main.c`。

> [!NOTE]
> 容器模块由 `HX_OS_ENABLE_CONTAINERS` 构建开关控制（默认 `ON`）。关闭该开关后会裁掉序列、平衡树和 `uthash` 实现代码，同时清除 `HX_OS_CAP_CONTAINERS` 能力位，所有容器创建函数都会返回 `HX_OS_NOT_SUPPORTED`。
>
> **相关数据结构导航**：
> - 固定容量淘汰与 TTL 过期缓存：[LRU 缓存](CACHE.zh-CN.md)
> - 稠密布尔标志位与按字集合运算：[动态位集合](BITSET.zh-CN.md)
> - 零分配固定缓冲区与双段借用读写：[固定环形缓冲](RING.zh-CN.md)
> - 跨线程并发消息投递（由 `HX_OS_ENABLE_QUEUES` 独立控制，无需协程运行时）：[线程／环形队列](QUEUES.zh-CN.md)

## 已提供的容器

如果同一个容器内需要混合存放数字、字符串和自定义结构体，可以配合独立的 [variant 模块](VARIANT.zh-CN.md) 使用：调用 `hx_variant_container_options_init` 即可为 `vector`、`queue`/`deque` 和 `map` 的值配置好深拷贝与自动释放回调。注意 C 层的 `hx_variant` 支持按字节搬移，但不要把 C++ 封装对象 `hx::Variant` 直接传给按字节复制的 C 容器接口。

| 容器类型 | 底层结构与操作时间复杂度 |
|---|---|
| `vector` | 连续动态数组；按下标随机访问 `O(1)`，尾部追加均摊 `O(1)`，中间插删 `O(n)` |
| `deque` | 环形分块／连续双端数组；按下标访问 `O(1)`，容量充足时首尾两端推入／弹出 `O(1)` |
| `queue` | 基于 `deque` 的先进先出（FIFO）别名，尾部入队、头部出队 |
| `stack` | 基于 `vector` 的后进先出（LIFO）别名，尾部入栈与出栈 |
| `list` | 双向链表；首尾操作与迭代器步进 `O(1)`，按下标定位需 `O(n)` 线性遍历 |
| `heap` | 基于连续数组的大顶堆；入堆与弹出堆顶 `O(log n)`，下标 `0` 始终为当前最大元素 |
| 有序 `map` / `set` | 默认 AVL 树，可选红黑树（`HX_OS_MAP_RED_BLACK`）；查找、插入、删除 `O(log n)`，按键升序迭代 |
| `multimap` / `multiset` | 在 AVL、红黑树或哈希类型上按位或 `HX_OS_MAP_MULTI`，允许保留多个等价键 |
| 哈希 `map` / `set` | 基于 `uthash 2.4.0`；平均查找、插入、删除 `O(1)`（极端哈希冲突下退化为 `O(n)`），按插入顺序迭代 |

关于各容器的行为边界：
- 表中复杂度按元素操作次数计，不包含键／值字节拷贝或自定义回调的耗时。对 `list` 进行完整遍历时请使用迭代器接口以保持总体 `O(n)`，避免在循环中反复调用 `O(n)` 的下标访问。
- 所有序列容器共用 `hx_os_sequence_*` 操作集；`queue` 和 `stack` 是语义清晰的便捷别名，不单独增加运行时实现或 DLL 导出符号。
- `heap` 仅允许在尾部插入和从下标 `0` 弹出堆顶；尝试修改任意下标的元素会返回 `HX_OS_NOT_SUPPORTED`，以防止破坏堆序不变量。
- `hx_os_vector_sort` 采用原地不稳定的堆排序；`hx_os_vector_lower_bound` 要求目标 `vector` 已按同一个严格弱序比较器排好序。
- 普通序列与映射容器内部**不带互斥锁**。多线程共享读写同一容器时必须由调用方在外部同步；若需开箱即用的跨线程安全队列，请使用[队列契约](QUEUES.zh-CN.md)中的并发队列。

## 选项、分配器与容量限制

创建任何容器前，先调用 `hx_os_container_options_init(&options, sizeof(element_type))` 初始化配置。容器在创建时会保存一份选项副本，后续修改局部 `options` 变量不会影响已创建的容器。
- **默认容量上限**：每个容器默认最多容纳 `1,048,576` 个元素（`max_items`），库内活跃分配上限为 `16 MiB`（`max_bytes`）。在内存受限的嵌入式设备上，建议根据实际业务规模将这两个非零上限调小。
- **对齐与可搬移要求**：`element_alignment = 0` 表示采用常规基础类型分配对齐；若显式指定对齐值，则必须是 2 的幂且 `element_size` 必须是对齐值的整数倍。所有存入容器的值都必须允许按字节搬移（在 `vector`/`deque` 扩容或排序时会发生内存移动）。含有内部自引用指针的结构体或依赖移动构造函数的 C++ 对象不能直接按值存入，应当改为存放具有明确外部生命周期的稳定指针。
- **内存预算核算（`max_bytes`）**：`max_bytes` 严格约束容器自身向 `hx_os_context` 申请的存活字节数，涵盖容器控制块、链表／树节点、哈希桶元数据、对齐开销、操作临时值以及数组扩容时短暂并存的新旧缓冲区。`hx_os_sequence_memory()` 和 `hx_os_map_memory()` 返回的正是这一口径的实时字节数。
  - 注意：自定义分配器自身的账本开销、`copy`/`drop` 回调内部额外申请的堆内存（例如深拷贝 `hx_string` 或 `hx_variant` 内部的字符缓冲区）以及指针元素指向的外部对象图，均**不计入**容器的 `max_bytes` 预算。
  - 替换已有元素时，为了保证“新值构造失败不破坏旧值”，库会先在临时空间中构造新值，因此即便元素总数不变，剩余字节预算不足以容纳一份临时元素时也会返回 `HX_OS_LIMIT`。
- **分配器上下文与销毁**：容器在存活期间会持有传入的 `hx_os_context`（此时销毁该 `context` 会返回 `HX_OS_BUSY`）。回调的 `user` 指针及其引用的外部状态均为借用，必须活到容器完全销毁之后。数组类容器（含 `vector`、`deque`、`heap`）支持 `hx_os_sequence_reserve` 预留容量，`list` 调用 `reserve` 则返回 `HX_OS_NOT_SUPPORTED`。`clear` 会清空所有元素（数组类保留已分配容量，链表与 `map` 释放节点）；`destroy` 释放全部剩余元素与底层存储并将调用方的句柄指针置为 `NULL`（对已为 `NULL` 的句柄重复调用 `destroy` 安全且返回 `HX_OS_OK`）。

## 复制、接管与借用

容器支持纯字节拷贝、通过 `copy`/`drop` 回调管理深层资源，以及通过 `_take` 后缀函数直接转移所有权：

| 操作方式 | 成功后的所有权归属 | 失败时的状态保持 |
|---|---|---|
| 普通插入（未设置 `copy`/`drop`） | 容器按字节浅拷贝一份元素 | 源数据不变，容器内容不变 |
| 普通插入（设置了 `copy` 和 `drop`） | 容器调用 `copy` 构造独立拥有的副本 | 源数据仍由调用方持有，容器内容不变 |
| 普通插入（只设置了 `drop`，未设置 `copy`） | 拒绝复制并返回 `HX_OS_NOT_SUPPORTED` | 源数据仍由调用方持有 |
| `_take` 插入（如 `push_back_take`） | 容器直接接管源值的字节与后续 `drop` 释放责任 | 调用方继续持有原值并负责释放 |
| `pop` / `erase`（传入非 `NULL` 输出缓冲区） | 将元素字节与所有权转移给调用方的输出缓冲区（不触发 `drop`） | 输出缓冲区不变，原元素保留在容器内 |
| `pop` / `erase`（输出缓冲区传 `NULL`） | 容器对该元素调用一次 `drop` 并将其实体移除 | 原元素保留在容器内 |
| `clear` / `destroy` | 对容器内所有剩余元素逐一调用 `drop` | 若因回调重入返回 `HX_OS_BUSY`，容器内容保持不变 |

> [!WARNING]
> **所有权交接与迭代器失效的关键规则：**
> 1. **`_take` 转移后的源变量处理**：`_take` 插入成功后，库不会自动清零调用方的源结构体字节。调用方必须将源变量视为“已移出（moved-from）”，例如将已交出的 `hx_string` 重置为 `HX_STRING_INIT`，**切勿**再对它调用析构函数；同时也绝不能把容器内部借出的元素指针再传给同一容器的 `_take` 接口。
> 2. **`pop` / `erase` 输出缓冲区**：接收弹出元素的外部缓冲区必须未持有活跃资源，且不能与容器内部存储重叠。若 `copy` 回调在构造过程中失败，回调自身必须清理已分配的半成品资源；容器只会对完整构造成功的元素调用 `drop`。
> 3. **禁止回调重入**：`copy`、`drop` 和比较器回调均不得抛出异常、调用 `longjmp` 或修改当前正在操作的容器（包括在只读查找的比较器中修改容器）。修改路径检测到重入写入会返回 `HX_OS_BUSY`。
> 4. **借用指针与迭代器失效**：`hx_os_sequence_at`、`map` 查找及迭代器返回的都是内部元素的**借用指针**（`hx_os_vector_data` 仅对 `vector`/`stack` 返回连续可写首地址，对其他容器返回 `NULL`）。任何成功的结构修改（插入、删除、替换、排序、`clear`、`reserve`）都会使已有借用指针和迭代器失效：若容器仍存活，使用旧代数的失效迭代器会返回 `HX_OS_INVALID`；若容器已销毁，迭代器不会延长容器寿命，继续使用属于悬空访问。遍历到达末尾返回 `HX_OS_EOF`，而空容器 `pop` 或查找不存在的键返回 `HX_OS_NOT_FOUND`。

## 键与 set

创建映射容器时，通过 `hx_os_map_kind` 选择哈希表（`HX_OS_MAP_HASH`）、AVL 平衡树（`HX_OS_MAP_ORDERED`）或红黑树（`HX_OS_MAP_RED_BLACK`）：
- **二进制键语义**：键是显式指定字节长度（上限 `UINT_MAX`）的字节序列，容器在插入时会复制键字节。允许零长度键和内嵌 `NUL` 字节。容器不对键执行 UTF-8 校验、字符编码转换、Unicode 归一化或本地语言（Locale）排序；如有需要，请在插入前先使用 `hx_string` 与文本转换接口规范化。
- **键比较规则**：
  - `HX_OS_MAP_HASH` 按字节完全相等判重，不接受自定义键比较器。
  - 有序树（AVL／红黑树）使用传入的 `key_compare` 回调；若传 `NULL`，默认按无符号字节字典序（`memcmp` 风格）排序。因此，若使用二进制整数作为有序树的键并希望按数值大小排序，请务必提供自定义比较器。由于内部键存储仅保证单字节对齐，在比较器中读取多字节类型键时，请先用 `memcpy` 复制到局部变量再比较，避免未对齐内存访问。
- **构建 `set`（纯键集合）**：将 `options.element_size` 设为 `0`，`copy`/`drop` 设为 `NULL`，并在插入时将 `value` 指针传为 `NULL`，即可将 `map` 作为 `set` 使用。查找命中时返回 `HX_OS_OK`（输出的 `value` 指针为 `NULL`）。
- **插入或更新（`replace` 参数）**：单键模式下，`replace = 0` 遇到已存在的键会返回 `HX_OS_EXISTS` 且不修改原值；`replace != 0` 则用新值替换旧值（保留原有的键字节与树／哈希表迭代位置）。

## 示例

[独立队列示例](../examples/containers.c) 展示了基础整数队列的用法。当需要在容器中保存动态字符串（`hx_string`）时，请使用内置的 `hx_os_string_container_options_init` 预设配置深拷贝与释放回调，切勿直接按字节浅拷贝拥有堆内存的 `hx_string`：

```c
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

// On HX_OS_OK the vector owns an independent copy of the string.
hx_string_destroy(&source);
(void)hx_os_sequence_destroy(&names, NULL);
```

如果使用自定义 `hx_os_context`，请将同一个 `context` 同时传给 `hx_os_string_container_options_init` 和 `hx_os_vector_create`。若后续通过 `pop` / `erase` 将容器内的 `hx_string` 提取到外部变量，调用方在使用完毕后需对该变量调用 `hx_string_destroy`。

## 红黑树和重复键

在创建有序映射时传入 `HX_OS_MAP_RED_BLACK` 即可启用红黑树后端（原有的 `HX_OS_MAP_ORDERED` 继续使用 AVL 树后端，保持 ABI 与枚举值完全兼容）。将映射类型与 `HX_OS_MAP_MULTI` 按位或，即可开启多值映射（`multimap` / `multiset`）：
- 在 `HX_OS_MAP_MULTI` 模式下，`replace = 0` 会追加插入一个同名键条目；`replace = 1` 则替换其中一个已有的同名键条目。
- 有序树中的等价键严格保持插入时的先后顺序，`hx_os_map_find` 和单次 `hx_os_map_erase` 均定位到第一个匹配的条目；哈希表在多键模式下的同键查找顺序则不作固定保证。

[完整红黑树程序](../examples/ordered_map.c) 依次插入 `b: 20`、`a: 10`、`b: 21`，运行输出如下：

```text
a: 10
b: 20
b: 21
```

核心创建与插入调用非常直观：

```c
hx_os_map_create(NULL, HX_OS_MAP_RED_BLACK | HX_OS_MAP_MULTI,
	&options, NULL, &map, &error);
hx_os_map_insert(map, "b", 1, &value, 0, &error);
```

常用的范围与辅助接口包括：
- **有序范围查询**：`hx_os_map_lower_bound`、`hx_os_map_upper_bound` 和 `hx_os_map_equal_range`（仅适用于 AVL 和红黑树）。
- **统计与批量删除**：`hx_os_map_count` 统计指定键的出现次数（适用于所有后端）；`hx_os_map_erase_all` 一次删除所有等价键（即使传入的查询键指针正借用自当前容器内部也能安全处理）。
- **边遍历边删除**：`hx_os_map_erase_at`（以及序列容器的 `hx_os_sequence_erase_at`）在删除当前元素的同时，会将传入的迭代器原地更新为下一个元素；容器的其余迭代器则会失效。
- **反向遍历与完整性校验**：`hx_os_map_last` 与 `hx_os_map_previous` 支持从尾到头逆序遍历；`hx_os_map_validate` 在不分配内存的前提下校验 AVL／红黑树平衡不变量与链表一致性。

## 遍历和算法

使用 `HX_OS_SEQUENCE_FOR_EACH` 和 `HX_OS_MAP_FOR_EACH` 宏（以及对应的 `_REVERSE` 反向宏）可以简洁地遍历容器。宏内部只对容器指针求值一次：正常遍历结束时会将 `status` 规范为 `HX_OS_OK`，中途若在循环体内主动 `break`，`status` 同样保持为 `HX_OS_OK`；若迭代器自身报错则保留错误码。

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

此外，容器模块直接内置了丰富的序列算法。比如只需找出最小的 `k` 项或中位数时，可以使用 `partial_sort` 或 `nth_element`，无需对整个容器做完整排序。下表列出了各容器算法的适用场景与复杂度（设输入长度为 `n`、`m`，前缀长度为 `k`，元素字节数为 `s`；表中简写入口省略 `hx_os_sequence_` 前缀）：

| 算法类型 | C API 入口 | 适用场景 | 时间复杂度与额外空间 |
| --- | --- | --- | --- |
| 原地不稳定堆排序 | `hx_os_vector_sort` | 在不分配任何临时内存的前提下对 `vector` 完整排序 | 最坏 `O(n log n)`，`O(1)` 临时空间 |
| 自底向上稳定归并排序 | `hx_os_sequence_stable_sort` | 排序后相等键必须保持原有先后顺序 | 时间 `O(n log n)`；`vector`/`deque` 需 `O(n × s)` 临时空间，`list` 通过重连节点仅需 `O(1)` 空间 |
| 有界堆前缀排序 | `hx_os_vector_partial_sort` | 仅需取出最小的 `k` 项并按升序排列 | `k > 0` 时 `O(n log(k + 1))`（`k == 0` 无操作），`O(1)` 临时空间 |
| 三路快速选择（超深回退堆排） | `hx_os_vector_nth_element` | 求中位数或分位阈值，不要求其余元素全部有序 | 平均接近线性，最坏 `O(n log n)`；需 1 个元素的临时空间（仅支持 `vector`） |
| 有序二分查找 | `hx_os_vector_lower_bound`、`hx_os_vector_upper_bound` | 在已按同一比较器排序的 `vector` 中查找键或插入点 | 时间 `O(log n)`，`O(1)` 临时空间 |
| 线性谓词扫描 | `find_if`、`count_if`、`any_of`、`all_of`、`none_of` | 查找／统计满足条件的元素或做全称／存在判断 | 最坏 `O(n)`，`O(1)` 临时空间；`find_if`/`any_of`/`all_of`/`none_of` 得出结论后立即短路 |
| 稳定筛选与相邻去重 | `remove_if`、`unique` | 就地剔除不需要的元素或压缩相邻重复项 | 时间 `O(n)`，`O(1)` 临时空间；被移除元素会自动触发 `drop` 回调 |
| 反转、旋转与谓词分区 | `reverse`、`rotate`、`partition` | 原地颠倒顺序、循环左移或将匹配项归拢到前部 | 时间 `O(n)`，`O(1)` 临时空间；`list` 分区稳定，数组类分区不保证稳定 |
| 克隆、筛选复制与映射转换 | `clone`、`copy_if`、`transform` | 保持源容器不变，生成独立的新 `vector` 容器 | 时间 `O(n)`；新容器最多占用 `O(n × 输出元素大小)` |
| 按条件批量替换 | `replace_if` | 将所有满足谓词的元素替换为指定新值 | 时间 `O(n)`，需 2 个元素的临时空间；中途复制失败时保留此前已完成的替换 |
| 逐项访问与折叠累积 | `visit`、`fold` | 对每个元素执行回调或向累加器汇总统计值 | 时间 `O(n)`，`O(1)` 内部临时空间；回调首次失败时保留已有副作用并报告已完成步数 |
| 两路有序归并与集合运算 | `merge`（支持 `HX_OS_MERGE` 及 4 种 `HX_OS_SET_*` 模式） | 合并两个有序序列，或求有序多集（Multiset）的并、交、差、对称差 | 时间 `O(n + m)`，结果最多占 `O((n + m) × s)` |
| 链表区间节点拼接 | `hx_os_list_splice` | 在两个配置兼容的 `list` 之间零拷贝转移节点区间 | 定位端点最坏 `O(n + m)`，随后 `O(1)` 重连指针；不分配新节点，也不触发值的 `copy`/`drop` |

> [!TIP]
> - 如果业务数据是连续编号的布尔标志（如任务 ID `0..N` 的完成状态），使用[位集合算法](BITSET.zh-CN.md)（`hx_os_bitset_count`、`find_next`、`apply`、`intersects`、`is_subset`）可以按 32 位机器字批量处理，比存放整型集合更省内存。
> - 容器算法的临时空间同样计入该容器的 `max_bytes` 预算；排序前若临时空间分配失败，原容器内容和迭代器均保持不变。`hx_os_list_splice` 要求源链表与目标链表的 `context` 及元素策略兼容，并在转移节点时同步划转两边的内存账本。
