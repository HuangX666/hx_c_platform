# 有界 LRU 缓存

[English](CACHE.md)

## 1. 留住最近使用的结果

`hx_os_cache` 保存复制后的二进制键和固定大小的值。成功的 `get` 会把该项调整为最近使用；`peek` 只读取，不改变顺序。成功插入新项后，如果数量超过 `max_items`，优先清理过期项；没有过期项时，淘汰最久没有使用的一项。更新已有键也会使它成为最近使用项。

适合缓存最近查询的设备资料或计算结果。它不是数据库、持久化存储或线程安全消息队列，支持按条目设置有效期，使用单调时钟，不启动后台线程；读取不会延长有效期。使用时开启 `HX_OS_ENABLE_CONTAINERS`；关闭模块后缓存 API 仍存在，创建返回 `HX_OS_NOT_SUPPORTED`。

## 2. C 调用

```c
#include <hx_cache.h>

hx_os_container_options options;
hx_os_cache *cache = NULL;
int temperature = 26;
const void *value = NULL;

hx_os_container_options_init(&options, sizeof(temperature));
options.max_items = 128;
options.max_bytes = 256 * 1024;

hx_os_status status = hx_os_cache_create(NULL, &options, &cache, NULL);
if (status == HX_OS_OK)
	status = hx_os_cache_put(cache, "sensor-01", 9, &temperature, NULL);
if (status == HX_OS_OK)
	status = hx_os_cache_get(cache, "sensor-01", 9, &value, NULL);

/* On success, read *(const int *)value before replacing/removing its entry. */
(void)hx_os_cache_destroy(&cache, NULL);
```

键是带明确长度的字节序列，可以包含 NUL；`"a"` 和长度为 2 的 `"a\0"` 是不同的键。`NULL, 0` 表示空键。找不到键时返回 `HX_OS_NOT_FOUND`，并把输出指针设为 NULL。

完整的 [C 示例](../examples/lru_cache.c) 创建只能保存两项的缓存，读取传感器 A 后再加入 C，演示 B 被淘汰，并存入一个有效期为 10 ms 的临时值，在到期后查询。使用现有已开启 examples 的构建目录，编译 `hx_os_lru_cache` 目标即可运行。

## 3. C++11 调用

```cpp
#include <hx/Cache.hpp>
#include <cstdio>

int main()
{
	auto cache = hx::LruCache<int>::create(128, 256 * 1024);
	if (!cache)
		return 1;

	auto saved = cache.value().put("sensor-01", 26);
	if (!saved)
		return 1;

	auto value = cache.value().get("sensor-01");
	if (!value)
		return 1;

	std::printf("Temperature: %d\n", *value.value());

	return 0;
}
```

便利类使用 `Result`，不使用异常，只接受可以按字节复制的值。值中的指针仍按借用处理。需要保存拥有资源的值时，使用 C 接口的 `copy`/`drop` 回调；现有字符串容器选项也可以直接用于保存 `hx_string`。

## 4. 在写入时设置有效期

```c
/* Keep this result for five seconds from this write. */
status = hx_os_cache_put_for(cache, "sensor-01", 9, &temperature, 5000,
	NULL);

/* Alternatively, reuse an existing monotonic deadline. */
status = hx_os_cache_put_until(cache, "sensor-01", 9, &temperature,
	hx_os_deadline_after_ms(5000), NULL);

size_t removed = 0;
status = hx_os_cache_purge_expired(cache, &removed, NULL);
```

C++ 对应 `putFor("sensor-01", 26, 5000)`、`putUntil(key, value, deadline)` 和 `purgeExpired()`；后者返回 `Result<size_t>`，结果是本次清理数量。与普通 `put` 相同，调用后先检查结果再继续。

| 操作 | 过期行为 |
|---|---|
| `put` / `put_take` | 保存为永不过期，更新时移除已有的有效期。 |
| `put_for` / `put_take_for` | 传入毫秒相对时长，在值复制完成、提交到索引之前采样时钟。溢出时饱和到最大的有限截止时间。 |
| `put_until` / `put_take_until` | 传入本库单调时钟的绝对截止时间；`HX_OS_DEADLINE_INFINITE` 表示永不过期。不能传墙上时钟时间戳。 |
| `get` | 已过期时返回 `NOT_FOUND`，记一次未命中并真正移除条目；成功读取不续期。 |
| `peek` | 已过期时返回 `NOT_FOUND`，不释放存储，也不改变统计。 |
| `purge_expired` | 显式清理在一次时钟采样时已经过期的全部条目，并返回数量。 |
| `size` / `memory` | 返回实际保留的条目数和分配字节，包括尚未清理的过期项。 |
| `erase` | 显式删除仍保留的存储，过期项也可以删除；非 NULL 输出可接走该值。 |

时长为 0、`HX_OS_DEADLINE_NOW` 或已经过去的截止时间表示立即过期。写入仍遵守复制／转移所有权及失败契约，但成功后立即移除新值，不会为了这个值淘汰仍有效的旧项。没有周期定时器，时间经过本身不会释放内存；查询过期键、显式清理、容量清理、清空或销毁时才执行释放。系统休眠期间的计时遵循本库单调时钟契约。

容量不足时优先移除过期项，再考虑淘汰仍有效的 LRU 项。内部保存最早到期时间的保守下界，尚未到期时不会反复扫描，但存在 TTL 的满缓存插入最坏仍可能扫描 O(n)。`purge_expired` 为 O(n)，不分配内存。完全没有 TTL 的缓存保持不读取时钟的路径。过期清理不增加 `evictions`，该统计仍只表示容量不足导致的有效项淘汰。

读时钟失败会在逻辑写入或过期清理提交前报告。写入失败不会预先清理旧项；如果旧过期项占满字节预算，可以显式清理后重试。成功的 `purge_expired` 可能减少数量，但不改变仍有效的值。

## 5. 所有权、失败与内存边界

- `put` 复制键，并按现有容器回调规则构造值；没有回调时按字节复制。`put_take` 只在成功时转移释放责任，来源不能是缓存已经持有的值。
- 淘汰、更新、清空和销毁会对每个被丢弃的值调用一次 `drop`。`erase` 传入非 NULL 输出时转移该值，不调用 `drop`；输出区域必须未初始化，并且位于缓存之外。
- 返回的值是借用指针，到该项被更新、删除、淘汰、到期清理或者缓存被清空、销毁时失效。没有后台线程，因此时间经过本身不会释放指针，但过期内容也不能继续视为有效业务数据。读取其他项不会使其失效。涉及修改的并发访问必须由调用者同步，`get` 也会修改访问顺序。回调不能重入当前缓存。
- `max_bytes` 严格限制缓存向 context 申请的内存，包括缓存和私有 context 对象、索引、键副本、值、分配头、对齐余量及插入临时空间；不包含分配器自身的管理开销、回调额外分配的内存或指针指向的资源。`memory()` 返回当前计入预算的字节数。
- 预算需要同时容纳旧内容和完整的新候选项，也要为可能的哈希桶扩容留空间。临时空间超预算时，`put` 返回 `HX_OS_LIMIT`，不提前淘汰旧项；分配器失败返回 `HX_OS_NO_MEMORY`。插入失败保留已有内容及访问顺序，索引容量可能发生变化。
- 淘汰由 `max_items` 触发，不由字节预算触发。这样可以避免“旧项已经丢弃，新项又分配失败”。单项自身超过预算时，即使缓存为空也无法存入。
- `get_stats` 返回命中、未命中和淘汰计数，到 `UINT64_MAX` 后饱和。只有 `get` 更新命中／未命中；只有容量触发的有效项淘汰更新淘汰数，过期清理不计入。`clear` 保留累计统计。

实现复用现有哈希表，并用双向链表维护使用顺序。扣除键哈希和比较成本，查找平均 O(1)，命中不分配内存；带 TTL 的查询需要读取时钟，到期移除可能执行用户的 drop 回调；处理键需要 O(键长度)，值的复制成本取决于回调，哈希扩容可能分配内存。不承诺它一定比直接使用 map 更快。
