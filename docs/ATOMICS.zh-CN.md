# 原子操作与内存序

[English](ATOMICS.md)

当一个线程完成数据计算并希望通过无锁方式将结果安全发布给另一个线程，或者需要在 C99 / C++11 中跨平台维护原子计数器、标志位与状态机时，可以使用 `<hx_os_atomic.h>`。它属于始终编入的核心模块，不随可选模块裁剪。

### 快速运行示例：Release-Acquire 结果发布

完整示例位于 [atomic_publication.c](../examples/atomic_publication.c)：工作线程将计算结果 `42` 写入普通变量 `answer`，随后使用 `HX_OS_RELEASE` 内存序将原子标志 `ready` 置为 `1`；主线程使用 `HX_OS_ACQUIRE` 内存序观测到 `ready == 1` 后安全读取 `answer`，最后 `join` 并销毁工作线程。

按[示例运行方法](../examples/README.zh-CN.md#run)启用示例后，构建并运行 `hx_os_atomic_publication`（无需命令行参数或输入文件）：

```sh
cmake --build build --config Release --target hx_os_atomic_publication
```

预期输出：

```text
answer: 42
```

在接入自己的工程时，可参考[最小工程配置](USAGE.zh-CN.md#c-project)使用[同一份 C 示例](../examples/atomic_publication.c)中的发布模式。

> [!TIP]
> **短自旋与长等待的选择**
> 示例中的 `hx_os_cpu_relax()` 自旋仅用于演示极短的跨线程状态发布。如果等待的是磁盘 I/O、网络收发或设备响应等长耗时操作，请使用事件（`hx_os_event`）或条件变量（`hx_os_cond`），避免忙自旋浪费 CPU。

## 存储类型与对齐规则

`<hx_os_atomic.h>` 提供三种可在 C99 和 C++11 中直接使用的原子存储类型：
- **`hx_os_atomic`**：64 位有符号整数（`int64_t`），占 8 字节并强制按 8 字节对齐（静态初始化宏 `HX_OS_ATOMIC_INIT(v)`）。
- **`hx_os_atomic32`**：32 位有符号整数（`int32_t`），占 4 字节并强制按 4 字节对齐（静态初始化宏 `HX_OS_ATOMIC32_INIT(v)`）。
- **`hx_os_atomic_ptr`**：无类型指针（`void *`），按目标平台原生指针对齐（静态初始化宏 `HX_OS_ATOMIC_PTR_INIT(v)`）。

> [!WARNING]
> **存储生命周期与类型边界**
> - 严禁使用 `#pragma pack` 等方式压缩包含原子类型的结构体对齐，严禁按字节复制（`memcpy`）正在被并发访问的原子存储，也不要在并发期间直接读写结构体的 `.value` 成员。
> - 不要将 C++ `std::atomic<T>` 与 `hx_os_atomic*` 互相强制类型转换（`reinterpret_cast`）或混用同一块内存。
> - `hx_os_atomic_ptr` 仅负责指针值本身的原子读写，不提供自动内存回收、所有权转移或 ABA 问题保护。

## 内存序与操作契约

库定义了五种内存序枚举值：`HX_OS_RELAXED`、`HX_OS_ACQUIRE`、`HX_OS_RELEASE`、`HX_OS_ACQ_REL` 和 `HX_OS_SEQ_CST`。各操作支持的合法内存序如下表所示：

| 原子操作 | 合法的 `hx_os_memory_order` 组合 |
| --- | --- |
| `load` | `HX_OS_RELAXED`、`HX_OS_ACQUIRE`、`HX_OS_SEQ_CST` |
| `store` | `HX_OS_RELAXED`、`HX_OS_RELEASE`、`HX_OS_SEQ_CST` |
| `fetch_add` / `sub` / `or` / `and` / `xor`、`exchange` | 全部五种内存序均可 |
| `compare_exchange`（成功序为 `HX_OS_RELAXED` 或 `HX_OS_RELEASE`） | 失败序必须为 `HX_OS_RELAXED` |
| `compare_exchange`（成功序为 `HX_OS_ACQUIRE` 或 `HX_OS_ACQ_REL`） | 失败序可为 `HX_OS_RELAXED` 或 `HX_OS_ACQUIRE` |
| `compare_exchange`（成功序为 `HX_OS_SEQ_CST`） | 失败序可为 `HX_OS_RELAXED`、`HX_OS_ACQUIRE` 或 `HX_OS_SEQ_CST` |
| `hx_os_atomic_fence` | 全部五种均可（`HX_OS_RELAXED` 仅作为编译器指令重排屏障） |

- **比较并交换（CAS）**：
  - `compare_exchange`（强 CAS）保证值相等时绝不伪失败；`compare_exchange_weak`（弱 CAS）允许在 LL/SC 架构上发生伪失败，通常配合循环重试使用。
  - 写入成功时返回 `1` 并保持 `*expected` 不变；写入失败时返回 `0` 并将当前观测到的实际值写入 `*expected`。`expected` 必须指向独立的线程私有可写变量，不得与目标原子变量重叠或被其他线程并发访问。
- **算术与交换返回值**：所有 `fetch_*` 和 `exchange` 均返回**修改之前的旧值**，算术加减按对应位宽无符号二进制补码规则回绕。
- **非法内存序的保守回退**：若传入了表中未列出的非法枚举值或非法的 CAS 成功/失败内存序组合，函数不会触发断言崩溃或终止进程，而是保守地升级为 `HX_OS_SEQ_CST` 执行。

## 编译器后端与无锁查询

- **GCC / Clang 与 MSVC 实现细节**：
  - 在 GCC / Clang 下，库内部通过常量分支调用对应的 `__atomic_*` 内建函数（避免将运行期枚举直接传给 GCC 内建函数而导致被无条件提升为 `seq_cst`），非 `HX_OS_RELAXED` 栅栏调用 `__atomic_thread_fence`。
  - 在 MSVC 下，底层基于 `Interlocked*` 内建函数实现，实际内存序始终不弱于调用方请求的顺序（MSVC 与原生 x86 下的 `compare_exchange_weak` 等价于强 CAS；非 `HX_OS_RELAXED` 栅栏在栈私有变量上执行完整屏障的 `Interlocked` 操作，`HX_OS_RELAXED` 栅栏使用 `_ReadWriteBarrier`）。
  - 编译器参考文档：[GCC 原子内建](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html)、[Microsoft Interlocked CAS](https://learn.microsoft.com/en-us/cpp/intrinsics/interlockedcompareexchange-intrinsic-functions)。
- **无锁能力查询（`is_lock_free`）**：`hx_os_atomic_is_lock_free()`、`hx_os_atomic32_is_lock_free()` 和 `hx_os_atomic_ptr_is_lock_free()` 返回当前编译目标对正确对齐存储的保守无锁保证。对于缺少原生 64 位原子指令的 32 位 GCC 目标，CMake 会自动检测并链接工具链的 `libatomic`。所有原子接口均不通过 `hx_os_context` 分配内存，也不调用用户回调。
