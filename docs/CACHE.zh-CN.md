# 有界 LRU 缓存

[English](CACHE.md)

当你的程序需要反复查询同一批设备资料、路由元数据或高开销计算结果，并且希望在严格限制最大条目数与内存预算的前提下只保留最近访问过的活跃条目（兼顾可选的单调时钟 TTL 过期淘汰）时，可以使用有界 LRU 缓存 `hx_os_cache`（C++11 对应 `<hx/Cache.hpp>` 中的 `hx::LruCache<T>`）。

完整可运行的示例位于 [lru_cache.c](../examples/lru_cache.c)：程序创建一个最多保存 2 个条目的缓存，先写入 `sensor-a` 和 `sensor-b`，接着调用 `hx_os_cache_get` 读取 `sensor-a`（将其提升为最近使用项），再写入 `sensor-c` 触发容量淘汰（此时最久未使用的 `sensor-b` 被自动淘汰），随后演示写入一个带 `10 ms` 有效期的临时条目并在到期后查询，最后销毁缓存。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 `-DHX_OS_BUILD_EXAMPLES=ON` 并构建 `hx_os_lru_cache` 目标即可直接运行（无需命令行参数或输入文件），预期输出如下：

```text
Recent sensor-a: 20
sensor-b evicted
temporary expired
```

你也可以参照[最小工程配置](USAGE.zh-CN.md#c-project)将该示例源码作为自己工程的 `main.c`。

## 1. 留住最近使用的结果

`hx_os_cache` 内部结合了哈希表（预期 `O(1)` 查找）与双向访问顺序链表，保存深拷贝后的二进制键和固定大小的值：
- **访问顺序更新**：调用 `hx_os_cache_get` 命中未过期条目时，会将该条目移动到最近使用（MRU）端；调用 `hx_os_cache_peek` 则只读取值而不改变 LRU 顺序，也不更新命中／未命中统计。更新已存在的键同样会将其提升为最近使用项。
- **容量与过期淘汰优先级**：当插入新键导致条目总数将要超过 `options.max_items` 时，缓存会优先查找并清理已经过期的旧条目；若当前没有已过期条目，则淘汰位于链表尾部的最久未使用（LRU）有效条目。
- **模块裁剪与定位**：缓存支持按条目设置基于本库单调时钟（Monotonic Clock）的有效期（TTL），**读取命中不会自动续期**，也不启动后台定时清理线程。使用时需开启 `HX_OS_ENABLE_CONTAINERS=ON`；若关闭该开关，缓存 API 符号仍然保留，但创建调用会返回 `HX_OS_NOT_SUPPORTED`。

## 2. C 调用

包含 `<hx_cache.h>`，通过 `hx_os_container_options_init` 配置值大小、`max_items` 与 `max_bytes` 即可创建缓存：

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

// On HX_OS_OK, read *(const int *)value before replacing/removing its entry.
(void)hx_os_cache_destroy(&cache, NULL);
```

- **二进制键规则**：键是显式指定字节长度的字节序列，允许包含内嵌 `NUL`（因此长度为 1 的 `"a"` 与长度为 2 的 `"a\0"` 是两个不同的键；传入 `NULL, 0` 表示合法的零长度空键）。
- **未命中返回值**：当查找的键不存在或已过期时，`hx_os_cache_get` 和 `hx_os_cache_peek` 返回 `HX_OS_NOT_FOUND` 并将输出的 `value` 指针置为 `NULL`。完整的淘汰与过期流程可参考 [C 示例](../examples/lru_cache.c)。

## 3. C++11 调用

C++11 工程包含 `<hx/Cache.hpp>` 即可使用 `hx::LruCache<T>`，所有方法均返回 `hx::Result` 而不抛出异常：

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

`hx::LruCache<T>` 模板仅接受可按字节平凡复制（Trivially Copyable）的值类型 `T`（若 `T` 是指针，缓存只保存借用地址）。如果需要在缓存中存放拥有动态资源的自定义类型，请使用 C 接口并配置 `options.copy` 与 `options.drop` 回调（例如直接调用 `hx_os_string_container_options_init` 即可在缓存中安全存放 `hx_string`）。

## 4. 在写入时设置有效期

除了永不过期的 `hx_os_cache_put` 外，你还可以在写入时指定相对毫秒时长（`put_for`）或单调时钟绝对截止时间（`put_until`），并随时调用 `hx_os_cache_purge_expired` 批量清理已到期条目：

```c
// Keep this result for five seconds from this write.
status = hx_os_cache_put_for(cache, "sensor-01", 9, &temperature, 5000,
	NULL);

// Alternatively, reuse an existing monotonic deadline.
status = hx_os_cache_put_until(cache, "sensor-01", 9, &temperature,
	hx_os_deadline_after_ms(5000), NULL);

size_t removed = 0;
status = hx_os_cache_purge_expired(cache, &removed, NULL);
```

C++ `hx::LruCache<T>` 对应提供 `putFor("sensor-01", 26, 5000)`、`putUntil(key, value, deadline)` 以及返回 `hx::Result<size_t>`（值为本次清理条目数）的 `purgeExpired()`。

| 操作接口（省略 `hx_os_cache_` 前缀） | TTL 与过期清理行为 |
|---|---|
| `put` / `put_take` | 写入为永不过期条目；若更新已有键，会清除该键原先关联的到期时间 |
| `put_for` / `put_take_for` | 传入以毫秒为单位的相对有效期（在值复制完成、发布到索引前采样单调时钟；加法溢出时饱和到最大有限截止时间） |
| `put_until` / `put_take_until` | 传入本库单调时钟的绝对截止时间（`HX_OS_DEADLINE_INFINITE` 表示永不过期；切勿直接传入墙上时钟 Unix 时间戳） |
| `get` | 若命中条目已过期，返回 `HX_OS_NOT_FOUND`、记一次未命中（Miss），并立即物理删除该过期条目；未过期命中不会延长 TTL |
| `peek` | 若命中条目已过期，返回 `HX_OS_NOT_FOUND`，但不物理释放存储，也不更新统计计数 |
| `purge_expired` | 采样一次单调时钟，显式删除当前所有已过期的条目并通过输出参数返回删除数量（`O(n)` 遍历，零内存分配） |
| `size` / `memory` | 返回当前物理保留的条目数与计入预算的字节数（包含尚未触发清理的已过期条目） |
| `erase` | 显式删除指定键（即使该条目已过期也可删除；传入非 `NULL` 输出缓冲区时可接管该值） |

> [!NOTE]
> **惰性过期与立即过期细节：**
> - 由于缓存不启动后台定时器线程，单纯的时间流逝不会自动释放内存；过期条目会在被 `get` 访问、容量满时触发新插入、调用 `purge_expired`、`clear` 或 `destroy` 时被真正释放。如果旧的过期条目占满了 `max_bytes` 字节预算导致新写入返回 `HX_OS_LIMIT`，可以先调用一次 `hx_os_cache_purge_expired` 腾出内存后再重试写入。
> - 传入时长为 `0`、`HX_OS_DEADLINE_NOW` 或已经过去的截止时间表示“立即过期”：该写入依然会完整执行复制／接管与错误检查，但在插入成功后会立即将该新条目移除，且绝不会为了它去淘汰缓存中仍有效的旧条目。
> - 缓存内部维护了一个最早到期时间的保守下界，当所有 TTL 条目尚未到达该下界时，满容量插入不会做无谓的全表扫描；完全不使用 TTL 的缓存则不会在查找或插入时读取系统时钟。

## 5. 所有权、失败与内存边界

- **值的所有权交接**：`put` 会深拷贝键，并按容器选项中的 `copy` 回调构造值（未设置回调时按字节浅拷贝）；`put_take` 仅在成功时接管源值的 `drop` 释放责任（切勿把缓存内部借出的值指针再传给 `put_take`）。当条目因淘汰、覆盖更新、过期清理、`clear` 或 `destroy` 被丢弃时，缓存会对每个值调用一次 `drop`。若调用 `hx_os_cache_erase` 时传入了非 `NULL` 的外部输出缓冲区，缓存会将值与所有权转移给该未初始化缓冲区而不调用 `drop`。
- **借用指针生命周期与并发**：`get` 和 `peek` 返回的 `const void *` 是内部值的**借用指针**，在该条目被覆盖更新、`erase`、容量淘汰、过期清理，或者整个缓存执行 `clear` / `destroy` 时立即失效（读取其他键不会使当前键的指针失效）。因为 `get` 也会修改内部 LRU 链表顺序并可能删除过期项，任何并发访问（包括并发 `get`）都必须由调用方在外部加锁同步。
- **内存预算（`max_bytes`）与淘汰触发条件**：
  - `max_bytes` 硬性限制缓存向 `hx_os_context` 申请的字节总量（含缓存控制块、哈希索引、键副本、值存储、分配头、对齐余量以及写入时的临时候选节点；不含分配器自身账本、用户回调内部额外申请的堆内存或指针指向的外部对象）。
  - 为确保“写入失败时绝不破坏或误删已有条目”，写入或更新时会先在预算内完整构造新候选条目，然后再替换旧值或淘汰满额条目。因此，**容量淘汰仅由条目数达到 `max_items` 触发，而不由 `max_bytes` 字节耗尽触发**——如果连构造新候选条目的临时空间都超出 `max_bytes`，`put` 会直接返回 `HX_OS_LIMIT` 而不会预先丢弃旧条目（分配器自身失败则返回 `HX_OS_NO_MEMORY`）。配置 `max_bytes` 时请预留至少一个候选条目及哈希桶扩容的余量。
- **命中率统计（`hx_os_cache_get_stats`）**：返回饱和累加的 `hits`（命中）、`misses`（未命中）和 `evictions`（有效项容量淘汰）计数。只有 `get` 会更新 `hits`/`misses`；只有因超过 `max_items` 而淘汰**未过期有效项**时才会增加 `evictions`（清理已过期条目不计入 `evictions`）。调用 `clear` 清空数据时会保留已累计的统计值。
