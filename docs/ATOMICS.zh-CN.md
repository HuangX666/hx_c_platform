# 原子操作与内存序

[English](ATOMICS.md)

hx_os_atomic.h 提供的 C99 类型和函数可直接用于 C++11。既有 hx_os_atomic 继续占 8 字节、按 8 字节对齐；独立 hx_os_atomic32 占 4 字节、按 4 字节对齐；指针类型按原生指针对齐。不要压缩结构体对齐、复制正在使用的存储、并发访问原始 value 成员，也不能把 std::atomic 强转成本库类型。发布前完成初始化，外层对象负责生命周期。指针原子操作不负责回收、所有权转移和 ABA 保护。

常见的数据发布使用 release store 配 acquire load，见 [同一份 C 示例](../examples/atomic_publication.c)。C++ 可以直接用这套 API；纯 C++ 对象也可用自己的 std::atomic 存储，不能混用同一块存储。较长等待应使用事件或条件变量，cpu_relax 不是阻塞等待替代品。

| 操作 | 合法内存序 |
| --- | --- |
| load | relaxed、acquire、seq_cst |
| store | relaxed、release、seq_cst |
| fetch add/sub/or/and/xor、exchange | 五种均可 |
| CAS 成功为 relaxed 或 release | 失败只能 relaxed |
| CAS 成功为 acquire 或 acq_rel | 失败 relaxed 或 acquire |
| CAS 成功为 seq_cst | 失败 relaxed、acquire 或 seq_cst |
| fence | 五种均可；relaxed 只做编译器屏障 |

强 CAS 不允许伪失败，弱 CAS 允许，通常在循环内重试。成功返回 1，expected 不变；失败返回 0，将观测值写入 expected。expected 是单独的可写存储，不与原子对象重叠，不被其它线程并发访问。fetch/exchange 返回修改前的值，加减按整数位宽取模。非法枚举或非法内存序组合保守退回 seq_cst，CAS 两个序都退回，不会隐式终止进程。

GCC/Clang 按常量内存序分支调用内建函数；直接传运行期整数可能被 GCC 统一加强成 seq_cst，不能只写一个枚举转换。MSVC 使用 Interlocked，始终不弱于请求序；其弱 CAS 和原生 x86 一样使用强 CAS。MSVC 非 relaxed 栅栏在私有栈变量上使用完整屏障的 Interlocked，relaxed 使用 _ReadWriteBarrier。 GCC/Clang relaxed 使用空编译器内存屏障，其余使用 __atomic_thread_fence。栅栏须配合适当的原子通信，不能修复普通数据竞争，volatile 不能替代同步。

编译器依据： [GCC 原子内建](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html)、 [Microsoft Interlocked CAS](https://learn.microsoft.com/en-us/cpp/intrinsics/interlockedcompareexchange-intrinsic-functions)。

is_lock_free 返回针对正确对齐存储的保守编译目标保证。缺少原生 64 位原子能力的 GCC 目标由 CMake 检测并链接工具链 libatomic。这些接口不通过 hx context 分配，不调用用户回调，但运行时回退可能有锁。无锁不等于无等待，也不保证所有信号上下文可用。本批没有加入未经测量的汇编优化，保留原有 CPU 等待提示。

现有调用点已核查：线程 join、线程池 close、watch close 的 acquire 争用与 release 释放配对；join 结果另有自己的 release/acquire 发布。IOCP 唤醒去重使用 acq_rel CAS、release 复位及既有队列锁；Apple run-loop 指针使用 release 发布、acquire 获取，后续 relaxed 读取发生在 join 后。不依赖隐藏的 seq_cst。

原子属于核心，不随可选模块裁剪。GCC TSan 不支持建模独立栅栏；仅在该构建下保留其警告并允许 atomic.c 编译。发布测试使用实际 acquire/release 原子操作及失败 acquire CAS，不依赖无法建模的栅栏。TSan 通过不能当作栅栏证明，也不能代替 ARM 目标运行。
