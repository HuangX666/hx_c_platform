# 验证记录与目标环境计划

[English](VALIDATION.md) · [README](../README.zh-CN.md)

本文记录截至 2026-09-29 的开发验证。设计覆盖、已有适配源码、编译成功、
目标运行成功是不同状态。本机测试不能证明所有系统版本、文件系统、
CPU、libc 或网络提供方均已通过。

## OBS 补齐第 1 批（2026-09-30）

Windows MSVC x64 Debug 与 WSL GCC 15.2 Debug 全量 CTest 均为 12/12。
系统信息、C++ 平台便利层和既有 C++ 线程池的 ASan/UBSan 测试为 3/3。
两平台 SYSINFO=OFF 的 C/C++ 契约测试通过。公共 C 头文件及全部 C++
便利层头文件独立编译通过，源码宽度检查按仓库明确的 8 列 Tab 执行。
macOS、Win7 运行验证与嵌入式目标验证仍待补充。

## 对象布局验证记录

2026-09-29 对内部布局进行前后比较，环境为 MSVC 19.40 x64、
MSVC 19.29 x86、GCC 15.2 x86_64 和 Clang 21.1.8 x86_64。
每种环境测量 25 个 C 对象、9 个 C++ 结果状态类型和 216 个完整任务类型；
任务矩阵覆盖不同结果类型、函数对象大小及对齐。
检查范围内没有对象变大或改变对齐。这不代表覆盖所有用户类型、标准库或 ABI。

C++ 在结果存储可匹配原子引用计数对齐边界时采用空基类／结果基类，
短小但非空的结果仍保留成员存储，避免 void 额外占位及 32 位对齐空隙。
TLS 键、配置文档和对象池将原有 8 字节对齐的原子成员放到较小指针成员之前。

代表性完整对象大小，单位为字节，箭头表示修改前后：

| 对象 | MSVC x64 | MSVC x86 | Linux GCC/Clang x86_64 |
|---|---|---|---|
| TLS 键 | 40 → 40 | 32 → 24 | 40 → 40 |
| 配置文档 | 120 → 120 | 80 → 72 | 120 → 120 |
| 对象池 | 40 → 40 | 32 → 24 | 40 → 40 |
| 空函数对象、void 任务 | 48 → 40 | 32 → 32 | 40 → 32 |
| 空函数对象、int 任务 | 48 → 48 | 40 → 40 | 40 → 40 |
| 空函数对象、16 字节对齐结果的任务 | 96 → 80 | 80 → 80 | 64 → 64 |
| 空函数对象、64 字节对齐结果的任务 | 320 → 256 | 320 → 256 | 256 → 128 |
| 空函数对象、128 字节对齐结果的任务 | 640 → 512 | 640 → 512 | 512 → 256 |

探针中的高对齐结果类型含有与对齐值等长的字节数组。
表中测量完整任务对象，包括结果、函数对象及管理数据，
不代表分配器实际尺寸档位或进程 RSS。C 原子类型宽度和对齐保持不变。
没有引入 packing、共享位域或新的缓存行填充策略。
已经紧凑的结构、具有功能意义的对齐探针保持原样。

13 个公共 C 头文件与修改前哈希一致，Windows DLL 仍有 235 个导出函数，
与公共声明匹配。C++ 封装内部模板布局发生变化，消费者及插件需一起重新编译，
不能混用不同头文件版本实例化的对象；源码 API 保持一致。
详见 [C++ 存储与重新编译规则](CPP_POOL.zh-CN.md#存储布局与重新编译)。

布局修改后的验证：

- Windows x64 静态 Debug、x64 DLL Release、x86/v142 Release，
  每种配置 10/10 组通过，包含公共头独立编译。
- WSL GCC ASan/UBSan：10/10 组通过；TSan：受影响的 C++ 任务组通过。
- Clang C++11 任务组链接既有兼容的已安装 Linux C 库后通过。
  这是封装验证，不表示本轮重建并验证了完整 Clang C 库。
- Windows DLL 安装后 5/5 个独立 C/C++ 消费者通过。
- 新增结果生命周期用例覆盖 16/32/64/128 字节对齐、move-only 转移、
  未取出结果的析构，以及结果构造前抛异常。既有用例继续覆盖取消、
  构造失败、引用／void 结果、分配回滚及结果保留。
- 5 个改动的自有源文件通过规范宽度检查。

临时比较源码、修改前副本和各工具链测量值保存在本地忽略目录 `build-layout/`。
对象缩小不等同于吞吐提升、缓存行优化、ARM／Apple 布局验证，
也不表示完成了 hx_c_log 的性能验收。

## 容器与 C++ 任务验证记录

配置解析之后新增了 C 容器和可选 C++11 封装，协程已延后。
以下为本地实际编译、运行结果，包含既有基础设施和配置解析的回归组：

| 配置 | 证据 |
|---|---|
| Windows x64/MSVC 19.40 Debug 静态库，全部解析后端 | 10/10 组 |
| Windows x64/MSVC 19.40 Release DLL，全部解析器及 GNU iconv | 10/10 组 |
| Windows x86/MSVC 19.29/v142 Release，内置解析器 | 10/10 组 |
| CMake 3.16.8/NMake/MSVC 19.29 x64，C++11 和全部解析器 | 10/10 组 |
| WSL GCC 15.2 Release 共享库，全部解析器及 GNU iconv | 10/10 组 |
| WSL GCC ASan/UBSan，全部解析器 | 10/10 组 |
| WSL GCC TSan，全部解析器 | 10/10 组 |
| WSL Clang 21.1.8，严格 C++11 封装链接已安装 C 库 | 任务组通过 |
| Windows 与 WSL，关闭容器／配置／网络／循环／池 | 各 7/7 组 |
| Windows 静态／共享及 WSL 共享的安装消费 | 各 5/5 组 |
| Windows 纯 C 构建，关闭测试／示例 | 未启用 CXX，构建通过 |
| Windows DLL 与 Linux so 导出 | 235 个，与公共头一致 |

容器测试覆盖环形缓冲回绕、随机 map 操作与参考模型对照、
有序 AVL 插删及比较次数上限、堆序、vector 排序／查找、
迭代器失效、字节键、拥有值、hx_string 克隆／移动／失败回滚、
对齐及有限的元素／字节上限。
分配失败注入覆盖初始哈希桶和后续扩容，检查旧条目保留及资源完全释放。
预留容量后的普通 POD vector 追加和 deque 入队／出队，
通过分配计数确认不新增分配。这是分配路径证据，不是吞吐基准或硬延迟保证。

C++ 测试覆盖 lambda、普通／成员指针、move-only 值、引用／void 结果、
用户异常、构造／分配失败、队满时在复制可调用对象之前拒绝、
排队／协作取消、独立取消源、同池死锁拒绝、关闭超时与重试、
准备与关闭竞争，以及结果活过线程池销毁的情况。
被执行路径没有 sanitizer 报错，不表示证明了所有线程调度。
公共 C 头及 C++ 封装分别独立、重复包含编译。
提交回滚保留事件创建失败的原生错误信息。

23 个改动的自有源码／构建文件通过规范宽度检查。
workflow 通过 actionlint，最小配置任务现在显式关闭 CONTAINERS。
固定 uthash 头文件和许可证的 SHA256 与保留文件一致，没有修改第三方源码。
新增的中英文容器／线程池文档明确所有权和性能口径。
116 个自有文件通过 UTF-8 无 BOM / LF 检查，
检查过的 README／文档内部链接均能解析到源码树内的文件。

使用 README 的正常构建与 CTest 命令即可重现。
对已构建目录可运行 `ctest -R containers` 或 `ctest -R cpp_pool`，
多配置构建补 `-C Debug`／`Release`。
GCC/Clang Linux 分别配置 `HX_OS_SANITIZER=address,undefined` 和 `thread`；
`python tools/verify_install.py build --config Release` 验证五个安装消费者。
CMake 3.16 验证时，清除旧 CXX=NOTFOUND 缓存影响并显式指定 v142 CXX 路径。

macOS、ARM/musl/uClibc-ng 嵌入式 SDK／板卡仍待目标验证。
Windows 7 SP1/8.1 兼容基线保留；当前 Windows 主机的 v142 编译，
不能替代旧系统运行验证。CI 已配置但未远程执行。
没有执行 hx_c_log 接入或锁定原版基线乘五的性能验收。
下方旧检查点作为历史保留，不应与当前十组测试配置混淆。

## 配置模块验证记录

在下方基础库检查点之后新增配置模块，并在同一批开发主机上验证：

| 配置 | 证据 |
|---|---|
| Windows x64/MSVC 19.40 Debug 静态库，五个第三方后端 | 8/8 组 |
| Windows x64/MSVC 19.40 Release DLL，全部解析器及 GNU iconv | 8/8 组 |
| Windows x86/MSVC 19.29/v142 Release，内置解析器 | 8/8 组 |
| CMake 3.16.8/NMake/MSVC 19.29 x64，全部解析后端 | 8/8 组 |
| WSL GCC 15.2 Release 共享库，全部解析器及 GNU iconv | 8/8 组 |
| WSL GCC ASan/UBSan，全部解析后端含第三方对象 | 8/8 组 |
| WSL GCC TSan，全部解析后端 | config 组，含并发用例 |
| Windows 与 WSL 最小构建，CONFIG=OFF | 各 5/5 组 |
| 安装后 Windows 静态/共享及 WSL 共享消费 | 各 3/3 组 |
| Windows DLL 与 Linux so 公共导出 | 194 个，与头文件一致 |

config 组覆盖 UTF-8 路径和值、JSON 大整数及嵌入 NUL、
代理项错误、XML 混合内容、重复键策略、范围外语法拒绝、
换行规则、资源上限、每个后端的逐次分配失败、
分配器回调重入及四线程并发解析。
YAML/TOML 第三方扩展能力另有用例。
若系统有逗号小数点 locale，则执行 locale 用例；
本次 Windows 执行，找不到指定 locale 的环境跳过该可选用例。
最后的语法修正已在 Windows 和 ASan/UBSan 下复核。

安装消费包括 C99、C++11 和配置示例，公共头分别独立编译。
29 个改动的自有源文件/脚本修正后通过规范宽度检查；
96 个自有文件通过 UTF-8 无 BOM / LF 检查。
五个解析器归档中的 592 个普通文件与解压源码逐字节一致。
更新后的 workflow 通过 actionlint，但未远程执行。

下方旧基础库数字作为历史保留，不代表配置模块最终测试组数。
macOS、旧 Windows 实机以及嵌入式 SDK/板卡仍待验证。
本次未开展配置模糊测试专项、完整语言标准认证、吞吐对照，
也未执行 hx_c_log 接入或性能验收。


## 已执行的基础库配置

| 配置 | 构建／运行证据 |
|---|---|
| Windows x64，MSVC 19.40，静态 Debug，GNU libiconv 1.18 | 7/7 组 |
| Windows x64，MSVC 19.40，共享 Release，GNU libiconv 1.18 | 7/7 组 |
| Windows x86，MSVC 19.29/v142，静态 Release，关闭编码后端 | 7/7 组 |
| CMake 3.16.8 + NMake/MSVC 19.29 x64，关闭编码后端 | 7/7 组 |
| WSL Linux x86_64，GCC 15.2，静态 Debug，GNU libiconv 1.18 | 7/7 组 |
| WSL Linux x86_64，GCC 15.2，共享 Release，GNU libiconv 1.18 | 7/7 组 |
| WSL GCC AddressSanitizer + UndefinedBehaviorSanitizer，系统 iconv | 7/7 组 |
| WSL GCC ThreadSanitizer，关闭编码后端 | 7/7 组 |
| WSL Clang，关闭线程池及编码后端 | 6/6 组适用测试 |
| Windows／WSL 最小配置：关闭网络、循环、池和编码后端 | 各 4/4 组 |
| Windows／WSL 安装后的静态／共享 C99、C++11 消费者 | 每种各 2/2 |
| Windows／Linux 共享库导出 | 170 个函数与公共头一致 |

每个公共头分别在 C99 和 C++11 中独立且重复包含编译，
不依赖 PCH 或 unity。自有目标使用已记录的警告配置并将警告视为错误。
上游 iconv 的警告不混同于自有代码无警告。
已通过技能配套的自有源码／脚本列宽检查，以及 workflow 的 actionlint。
UTF-8/BOM/LF 和文档链接另行检查。

构建目录保留 CTest 的 Testing/Temporary/LastTest.log、构建日志和
安装消费者日志，属于忽略的开发产物。验证脚本及 CI workflow 保存在源码中。
CI 已编写并检查语法，本工作区没有在 GitHub 上执行它。

## 契约覆盖

- core：对齐和上下文所有权、UTF-8 错误、带状态的时钟读取、
  保留通知的事件、超时重试、线程 join 和 TLS key 生命周期。
- data：字符串自引用、复制／移动／格式化、非法边界、内嵌 NUL、
  UTF-16 往返、严格错误／替换／进度；启用后验证 GBK、Shift-JIS、
  EUC-KR、CP1252 向量；池上限／对齐／realloc／缓存、对象池／arena，
  文件追加／sync／短读／EOF 和目录重试。
- 路径：中文、补充平面字符、超过 260 字符的嵌套路径、
  非法 UTF-8 拒绝和资源清理。
- lifecycle：分配器故障注入与回滚、尚未结束的原生 TLS 析构、
  并发分配／realloc／collect，以及上游释放回调期间保持内存上限记账。
- pool：有界队列拒绝、排队取消、一次清理、超时保留、重试、
  任务句柄比池活得更久、拒绝同池自等待。
- net：本地 TCP 接受／连接／复制发送／接收／取消／截止时间，
  直接及异步 UDP 空数据报和截断、localhost DNS、定时器、投递、
  回调重入检查和请求句柄比循环活得更久。
- loop_concurrency：四个投递线程、2000 次完成回调、有界待处理工作、
  容量拒绝、零预算关闭定时器、关闭后拒绝准入。
- options：关闭的构造入口及传统编码明确返回 NOT_SUPPORTED。

尚未覆盖强制普通文件短写故障注入、磁盘写满／重启持久化、真实 UNC 服务、
每一个代码页映射、休眠／唤醒、信号中断压力、全部原生网络提供方、
外部 DNS 长时间停顿或真实宿主插件卸载。
这些是目标验收任务，不能笼统藏在“已跨平台支持”中。

## 平台状态

| 目标 | 设计／源码 | 目标编译 | 目标运行 |
|---|---|---|---|
| 当前 Windows x86/x64 | 已实现 | 通过 | 通过 |
| Windows 7 SP1／8.1 x86/x64 | 保留 API 基线 | 当前宿主 v142 构建 | 待旧系统 |
| WSL Linux x86_64/glibc | 已实现 | 通过 | WSL 内通过 |
| 原生 Linux 设备／服务器 | 共享 POSIX + epoll 实现 | 取决于 SDK | 待目标环境 |
| macOS Intel／Apple Silicon | POSIX + kqueue/Mach 实现 | 待 Apple 主机 | 待验证 |
| ARMv7-A/AArch64 嵌入式 glibc | 共享 Linux 代码及 SDK 工具链 | 待 SDK／CI | 待板卡 |
| 嵌入式 musl | 共享 Linux 代码及能力检测 | 待 SDK／CI | 待板卡 |
| 嵌入式 uClibc-ng | 共享 Linux 代码，需核实能力配置 | 待 SDK | 待板卡 |
| Android | 可复用 Linux/POSIX/epoll 思路 | 未适配 | 未运行 |
| iOS | 可复用 POSIX/Apple 思路 | 未适配 | 未运行 |
| BSD | 可复用 POSIX，需身份／join／后端适配 | 未适配 | 未运行 |
| FreeRTOS／裸机 | 当前范围以外 | — | — |

Android 需明确 bionic API level、应用权限和存储边界。
iOS 需检查部署版本、沙箱、后台执行和生命周期，不假设 fork。
BSD 需要自己的线程标识与有限 join 语义，不能默认走 Linux。
macOS 当前没有 Objective-C，仍需核实 clock_gettime 的 SDK／部署要求。
嵌入式验收需要锁定 CPU／浮点 ABI、字节序、libc 版本及配置、
内核下限、time_t 宽度和原子支持。

## 复现和扩展

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel 4
cd build
ctest -C Debug --output-on-failure
~~~

~~~sh
python tools/verify_install.py build --config Debug
python tools/check_exports.py build-shared/libhx_c_platform.so
~~~

Windows 导出检查使用实际 DLL 路径及 --tool dumpbin。
关闭功能的验证使用 README 中最小构建选项。
检查器使用独立 GCC/Clang 构建目录，HX_OS_SANITIZER 指定
address,undefined 或 thread；不能把 ASan 和 TSan 混在同一构建。

workflow 包含 Windows/Linux/macOS 静态／共享构建、最小配置、检查器、
容器内原生 musl，以及 ARMv7/AArch64 交叉编译加 QEMU。
模拟器结果不能代替真实板卡／SDK 验收。
实际板卡上应构建运行同一个 C 示例及适用的 CTest，
检查 32 位／time64 边界和真实文件系统、网络行为，
记录编译器、SDK、内核、libc 版本与原始结果。

## 性能边界

可选 benchmark 目标已在 Windows 和 WSL 构建运行。
其累计计时仅验证开发测量入口，不是受控验收数据或迁移前后性能结论。
本轮没有进行基线扣除或 hx_c_log 接入性能测试。

| 路径 | 后续测量要关注的现有开销 |
|---|---|
| 原子操作 | 非内联 C 调用；原生 intrinsic／builtin；x86 CAS 循环 |
| 已有容量的字符串追加 | UTF-8 校验与复制，无池锁／全局锁 |
| 字符串自引用／格式化 | 可能需要临时分配 |
| 编码 | 标量校验；传统编码逐码点调用后端 |
| 文件 I/O | 原生调用，打开／查询时转换路径 |
| 事件 set/wait | 内部互斥锁、条件变量、保留谓词 |
| 内存池分配／释放 | 每池互斥锁，增长时才调用上游分配 |
| 任务提交 | 有界队列锁及任务／事件分配 |
| 异步请求提交 | 请求／事件／互斥锁分配，默认复制发送数据 |
| 循环驱动 | 对有界请求／截止时间线性扫描，使用原生等待后端 |

没有在缺乏证据时引入 SIMD 或手写大块复制汇编。
PAUSE/YIELD 和对应原子操作限制在底层。
先在实际目标测量体积、吞吐、尾延迟、分配、锁竞争和系统调用，
再决定是否引入定时器堆、请求存储复用或批量编码转换。
hx_c_log 锁定的五倍及单线程不回退门槛仍是后续独立验收。

## OBS 补齐第 2 批（2026-09-30）

Windows MSVC x64 与 WSL GCC 全量测试均为 14/14；相关 ASan/UBSan
为 3/3，覆盖加载器、C++ 便利入口和版本协商示例。两平台 LIBRARY=OFF
的 C/C++ 检查通过。测试插件为仅暴露小型 C ABI 的独立模块，避免将
非 PIC 静态库对象嵌入 sanitizer 共享模块。macOS 和 Win7 实测仍待补充。

## OBS 补齐第 3 批（2026-09-30）

Windows／WSL 全量 CTest 均为 16/16；ASan/UBSan 相关 data、file_utils、
cpp_platform、text_file_example 为 4/4。两平台 FILE_UTILS=OFF 的 C/C++ 检查通过，Linux
的 FILE_UTILS=ON／CONTAINERS=OFF 组合通过。覆盖 UTF-8 长路径、BOM、
读取上限、输出保留、拒绝复制到自身、备份冲突、cwd 往返、glob、有界删除
和分配器所有权。Windows 因权限不足跳过创建符号链接；Linux 不跟随链接
删除已通过。其他原生目标仍待验证。

## OBS 补齐第 4 批（2026-09-30）

Windows／WSL 全量 CTest 均为 18/18，env、environment_example、
cpp_platform、file_utils 的 ASan/UBSan 为 4/4；两平台 ENV=OFF 检查
通过。覆盖空值与不存在、UTF-8、输出保留、条件替换、库内并发调用，以及
输出分配器回调重入环境读取而不死锁。提取共用 UTF-8／UTF-16 助手后，
Windows 文件路径回归亦通过。

## OBS 补齐第 5 批（2026-09-30）

Windows／WSL 全量 CTest 各 20/20；ASan/UBSan 相关 6/6（paths、env、
file_utils、C++ 封装及两个新增 C 示例）。两平台 PATHS=ON 且 ENV=OFF 各
4/4，PATHS=OFF 各 3/3，含 C99/C++11 公共头独立编译。覆盖 XDG 相对及空值
回退、UTF-8、根路径保留、容量协商、分配失败以及查询不创建目录。Windows
已知目录在未初始化 COM 的测试进程中通过。macOS、Win7、嵌入式实测仍待补。

## OBS 补齐第 6 批（2026-09-30）

Windows／WSL 全量各 22/22；相关 ASan/UBSan 5/5（core、lifecycle、
thread_extra、cpp_platform、thread_options_example）。两平台 THREAD_EX=OFF
各 3/3，含 C99/C++11 独立头编译。覆盖初始化容量哨兵、非法栈大小、Linux
实际栈预留、UTF-8 名称截断、跨线程命名、信号量溢出不部分修改、截止时间超时
和后续重试等待退出。默认创建路径不增加锁或分配。Win7 命名回退和 Apple 行为
仍需目标环境运行验证。

## OBS 补齐第 7 批（2026-09-30）

Windows／WSL 全量各 24/24；相关 ASan/UBSan 7/7、TSan 5/5。包含管道、事件循环
并发、C++ 回调和 C 传输示例，ASan 另覆盖文件与网络回归。验证复制/借用写入、
EOF/断开、保留已有待决 SIGPIPE、分配失败回收、共用容量、跨线程取消与完成竞争、
零操作截止时间、回调重入及关闭重试。两个平台运行同一份 C 传输示例。

两平台 NETWORK=OFF：LOOP=ON/PIPE=ON 各 4/4，LOOP=ON/PIPE=OFF 各 3/3，
LOOP=OFF/PIPE=ON 各 3/3，均编译 C99/C++11 独立公共头。Windows 最小配置的一次
增量链接对 pipe_native 报 LNK1236，保持源码不变重新链接该目标后通过，其后 3/3
测试通过；该次链接失败的根因尚未确定。

启用 HX_OS_BUILD_BENCHMARKS 后构建并运行 hx_os_layout_report，实测大小/对齐
（字节）如下：

| 对象 | MSVC x86 | MSVC x64 | Linux x64 |
| --- | --- | --- | --- |
| hx_os_operation（既有公共类型不变） | 72/8 | 88/8 | 88/8 |
| hx_os_pipe_operation（新增） | 40/8 | 48/8 | 48/8 |
| hx_os_thread_options（新增） | 12/4 | 16/8 | 16/8 |
| hx_os_socket（内部） | 40/8 | 56/8 | 56/8 |
| hx_os_file（内部） | 12/4 | 16/8 | 8/4 |
| hx_os_loop（内部） | 112/8 | 176/8 | 176/8 |
| hx_os_request（内部） | 696/8 | 768/8 | 288/8 |
| hx_os_pipe（内部） | 36/4 | 56/8 | 56/8 |

x86 仅为布局探针编译及运行，不能代表本批全量 x86 运行通过。macOS、Win7 原机、
ARMv7/AArch64 与 musl 仍待目标验证。布局及分配/系统调用审查不代替吞吐性能证据。

## OBS 补齐第 8 批（2026-09-30）

Windows／WSL 全量各 26/26；相关 ASan/UBSan 6/6、TSan 4/4。在 NETWORK 开启时
NET_OPTIONS=OFF/POSITIONED_IO=OFF 各 3/3；NETWORK=OFF/POSITIONED_IO=ON 各
4/4，Windows 同时关闭 FILE_UTILS 及其他便利模块。公共头通过 C99/C++11 独立编译。
覆盖四线程并发定位读写与共享游标不变、首次句柄发布竞争、短读、EOF、范围校验、
追加和只读拒绝、显式地址复用、实际选项长度、对端查询及附属 loop 后禁止设置。

Windows TCP_NODELAY 实测返回 1 字节；测试改为清零存储并检查原生实际长度，
不再假设 sizeof(int)，原始返回语义未改变。内部 file 大小/对齐实测 Windows x86
16/4、Windows x64 24/8、Linux x64 12/4，其余已报告对象不变。这些是布局及正确性
证据，不代表吞吐性能基准。

管道字节数指针修正依据微软文档中的 Win7
[ReadFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile)
与 [WriteFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile)
契约。Win7/macOS/嵌入式运行证据仍待补。

## OBS 补齐第 9 批（2026-09-30）

Windows／WSL 全量各 28/28；ASan/UBSan 5/5（data、file_utils、file_info、
cpp_platform、file_info_example）。两平台 FILE_INFO=OFF/BOM=OFF 各 3/3。
新公共头通过 C99/C++11 独立编译。覆盖初始化容量哨兵、失败输出保留、Windows
只读属性、POSIX owner 模式摘要与链接跟随、目录容量重试/类型、五种 Unicode
BOM 及 UTF-32LE 重叠前缀。POSIX 权限测试使用原生文件系统的独立 /tmp 目录，
不把 Windows 挂载盘的权限模拟行为当成普通 POSIX 文件系统。Windows ACL 权限
明确标为未知。

扩展信息布局在 Windows x86/x64 和 Linux x64 均实测 72/8 字节（大小/对齐）。
既有公共布局不变。macOS、Win7、嵌入式、Windows 链接权限和需要 DT_UNKNOWN
回退的目标文件系统仍需分别验证。

## OBS 补齐第 10 批（2026-09-30）

Windows／WSL 全量各 30/30；相关 ASan/UBSan 与 TSan 各 4/4（config、
config_serialize、cpp_platform、config_serialize_example）。覆盖 JSON 大整数
精确拼写、内嵌 NUL/Unicode、INI 分组、properties 往返、不兼容类型拒绝、
深度/节点/工作量/输出/堆预算、分配失败、不可变文档并发读取及原子保存重读。
数值助手提取后，既有已开启的第三方解析后端全部通过。

double 检查包括正负零、DBL_MIN/DBL_MAX、往返、上溢、完整 token 校验和
inf/nan 规范化。两平台均在小数逗号 locale 下通过，并检查进程 locale 和当前小数
分隔符没有被库改变。WSL 起初未生成可用的替代 locale，使用已有 localedef 及
系统数据在 build-obs-linux/test-locales 下生成私有 de_DE.UTF-8，仅测试命令设置
LOCPATH；未修改系统 locale 或安装依赖。全量、ASan、TSan 均使用该私有数据验证。

CONFIG=ON 而 CONFIG_SERIALIZE/TEXT_NUMBER=OFF 两平台各 3/3，原配置 double
getter 保持可用。CONFIG=OFF/TEXT_NUMBER=ON 各 3/3。Windows 的序列化开启、
FILE_UTILS 关闭组合 4/4，包含保存明确拒绝。C99/C++11 独立头及源码规则检查
通过；没有扩展既有公共结构体。macOS、Win7、嵌入式运行仍待补。

## OBS 补齐第 11 批（2026-09-30）

Windows／WSL 全量 CTest 32/32；ASan/UBSan、TSan 各 6/6，覆盖 timer、pipe、
loop_concurrency、cpp_platform 及两个异步示例。验证固定请求预算、分配失败、
触发时无分配、慢回调合并、跨线程 stop/wait、清理一次、自身关闭拒绝、loop
销毁顺序、原有一次性行为，以及 C++ 异常与捕获数据退役。公共头 C99/C++11
独立编译通过。

TIMER=OFF/LOOP=OFF 两端各 4/4；另测 Windows LOOP=ON/TIMER=OFF 3/3，
Linux NETWORK=OFF/LOOP=ON/TIMER=ON 4/4。布局探针在 Windows x86/x64、
Linux x64 运行，timer 大小／对齐分别为 40/8、64/8、64/8。
macOS、Win7、嵌入式运行仍待目标验证，不承诺硬截止时间。

## OBS 补齐第 12 批（2026-09-30）

Windows／WSL 全量 CTest 各 32/32；ASan/UBSan、TSan 各 3/3，包含 pool、
cpp_pool、lifecycle。THREAD_POOL 关闭两端各 2/2，公共头 C99/C++11 独立编译。
身份测试区分宿主线程、本池工作线程及 cleanup、其他池；自身等待／关闭拒绝，
本池已经完成的任务仍可读取。复用现有 TLS，未增加布局、分配、锁、系统调用
或工作线程路径开销。原生目标运行待验证。

## OBS 补齐第 13 批（2026-09-30）

Windows／WSL 全量 CTest 各 33/33，ASan/UBSan、TSan 各 3/3（core、
error_text、cpp_platform）。NETWORK 关闭两端各 3/3，无网络初始化也能生成
GAI 说明。覆盖 UTF-8、数值回退、越界代码、容量／NUL／哨兵、四线程并发、
C++ 持有文本；公共头 C99/C++11 独立编译。测试局部变量与 Windows SDK 宏冲突
已在最终 Windows 构建前修正。Mach 实现及条件测试待 macOS，Win7 和嵌入式
实测待补。

## OBS 补齐第 14 批（2026-09-30）

Windows 全量 35/35，Linux 全量 37/37，后者包含两个独立进程故障测试；
ASan/UBSan 与 TSan 各 7/7。仅在测试进程用 seccomp 注入 ENOSYS／EIO，
真实设备回退执行通过，失败清零、原生代码、UUID 输出保留通过，无跳过。
另覆盖 1 MiB 填充、四线程并发、UUID 位／格式／重叠／字符串、CRC32 已知向量
与增量、清零边界。公共头 C99/C++11 通过；RANDOM 关闭两端各 3/3，Linux
最小组合还关闭 explicit_bzero 以验证通用清零路径。既有对象布局和热路径不变。
Windows 传统 provider 失败、启动早期缺熵、macOS、Win7 和嵌入式仍待目标验证。

## OBS 补齐第 15 批（2026-09-30）

Windows 全量 37/37，Linux 39/39；ASan/UBSan、TSan 各 4/4，包含 data、
vm、cpp_platform、mapped_file_example。VM 关闭各 3/3；开启 VM、关闭
SYSINFO 各 4/4，Windows 同时关闭 FILE_UTILS。公共头 C99/C++11 独立编译，
Windows x86 本批运行测试及 x86/x64 布局探针通过，布局详见平台工具契约。

覆盖页取整／清零／保护转换、非法权限、分配失败、空／越界／未对齐映射、
文件位置保持、context 持有、原文件关闭后访问、私有写入隔离、共享持久化
冲刷并重开、追加／管道拒绝、C++ 移动与 RAII。允许系统策略以原生错误拒绝
可执行保护，未执行动态生成代码。截断故障、网络文件系统一致性、原生 unmap
失败、Apple hardened runtime、Win7、嵌入式仍待目标验证。

## OBS 补齐第 16 批（2026-09-30）

Windows 全量 39/39，Linux 41/41；ASan/UBSan、TSan 各 4/4（file_info、
file_extra、cpp_platform、file_attributes_example）。FILE_EXTRA 关闭各
3/3；开启且 FILE_INFO 关闭各 4/4，Windows 同时关闭 FILE_UTILS。C99/C++11
公共头独立通过。Windows x86 运行跨进程／元数据用例，三个 ABI 的 file_times
布局实测均为 40/8。

项目测试子程序验证共享／排他冲突、单调超时、超时后重试与其他区间可用、文件
位置不变。初次 Windows 子程序失败原因是窄字符 CRT argv 未保留 UTF-8；
改为读取并转换原生 Unicode 命令行后，中文文件名通过。Shell32 仅作为这个
测试目标的参数转换依赖。元数据覆盖指定字段／访问时间保留、是否传 error
的原生错误一致性、只读属性、POSIX owner/group 位和 /tmp 链接不跟随更新；
容量哨兵、C++ 锁转移通过。macOS、Win7、32 位 POSIX 时间范围及特殊文件系统
仍待目标验证。

## OBS 补齐第 17 批（2026-09-30）

Windows 全量 42/42，Linux 44/44；ASan/UBSan、TSan 各 7/7，覆盖 watch、
loop_fd、pipe、timer、loop_concurrency、cpp_platform 和监听示例。两个模块
关闭各 4/4；Windows 开启 WATCH、关闭 LOOP 5/5；Linux 关闭 NETWORK、
本地 LOOP 和两种监听开启 5/5。公共头 C99/C++11 通过，Windows x86 监听／
不支持桩运行 2/2；布局值记录在平台工具契约。

覆盖分配故障与拒绝不接管清理、中文／补充平面字符、目录限制／重建溢出、
非法 POSIX 文件名、清理一次、自身关闭、超时保留重试、C++ 异常与捕获释放。
外部描述符覆盖关闭原 fd 后的独立持有、就绪 I/O、请求预算、原生不支持类型、
cleanup 重入和 loop 销毁顺序。

初次 WSL 在 Windows 挂载的源码目录监听收不到通知。直接原生 inotify 探针
分别用路径和 /proc/self/fd 订阅，/tmp 两例各返回 96 字节，/mnt/e 两例均为
0；statfs 确认为 tmpfs 与 9p。适配现对 9p 保守拒绝，Linux 测试改用唯一
/tmp 目录，并测试了拒绝行为，没有用延长超时掩盖问题。原生内核缓冲饱和、
取消注册失败重试、缺少 procfs、Apple 原生编译运行、Win7、嵌入式硬件待验证。

## OBS 补齐第 18 批（2026-09-30）

Windows MSVC x64 全量 CTest 46/46，WSL Linux 48/48 通过。
ASan/UBSan、TSan 各 7/7，覆盖环境、管道、系统、进程、C++ 便利层、中文命令行
文件名及捕获示例；公共头 C99/C++11 独立编译通过。
SYSTEM_UTILS／PROCESS 关闭时两端各 5/5；Windows 开启这两项而关闭
LOOP／NETWORK／ENV／PATHS 时 5/5；Linux 关闭 NETWORK／ENV／PATHS 后本地
捕获等 7/7。Windows x86/v142 构建完成，相关四项运行通过，包含既有文件
工具测试及中文命令行示例。

受控测试子程序覆盖空参数、空格、引号、反斜杠、中文、补充平面字符，
完整／空环境、stdin EOF、两路各 320 KiB 输出、超时保留与重试、提高上限、
显式丢弃、二进制与严格文本转换、重复等待、非零退出码以及 Windows 真实
退出码 259。注入分配器第 1 至 64 次调用失败，验证 context 无未释放分配，
可正常清理。环境快照回调重入修改环境，证明回调位于锁外。
CPU 检查验证 AVX2 必须同时有可用 AVX；Linux 仿真未知仍明确报告。

x86 v142 在 /utf-8 下拒绝了既有 snprintf 的非 ASCII 格式文本；改为 ASCII
格式并通过 %s 传入同一中文文件名，保持测试语义并通过，未关闭警告。
Windows 最小配置的一次增量链接报告 COFF 对象无效；重建该库目标后通过。

新增对象大小／对齐：Windows x86 的 string_list 8/4、process 24/8、
process_options 20/4、result 8/4、capture_options 16/4；Windows x64 依次
16/8、32/8、32/8、8/4、24/8；Linux x64 依次 16/8、24/8、32/8、8/4、24/8。
既有公共结构以及 loop/request 布局不变。

Release 百万次除法单轮示例数据：MSVC x64 helper/native 为
2509100/1998700 ns；WSL Clang x64 为 2604750/2521124 ns。辅助接口检查除零，
原始表达式不检查；单轮数据不构成加速或嵌入式性能结论，本批没有加入汇编。
原生 macOS、Win7 控制台继承、ARM/musl、缺少 procfs／系统调用故障注入和
真实嵌入式板卡仍待验证。

## OBS 补齐第 19 批（2026-09-30）

Windows MSVC x64 全量 48/48、WSL Linux 全量 50/50 通过；相关 ASan/UBSan、
TSan 各 7/7，覆盖线程扩展、延迟任务、线程池、C++ 封装、loop 并发及示例。
公共头 C99/C++11 独立编译通过。线程扩展／线程池／延迟模块关闭时，两端
各 4/4；只开线程池而关闭延迟模块，各 5/5；启用线程扩展及延迟任务、关闭
网络时各 6/6，Windows 同时关闭 LOOP。Windows x86 相关 4/4 通过。

覆盖原生亲和性读回、Linux 部分掩码拒绝与恢复、有效／无效调度请求及
权限状态，共享容量、就绪 FIFO 与同 due 排序、不提前执行、未来任务取消、
排空超时后升级取消、工作线程 cleanup、64 项任务取消／关闭／销毁竞争、
任务晚于池存活、C++ 只移动参数／结果及取消、延迟任务分配失败，以及关闭
与延迟任务 prepare 并发。实时优先级只在阻塞工作线程上测试，放行前恢复。

私有对象实测大小／对齐：两端 x64 的 task／pool／延迟元数据为
88/8、104/8、32/8；x86 为 56/8、64/8、24/8。公共布局未改。
普通任务未增加分配次数或锁次数；未来任务增加一个互斥锁、元数据、
有界链表插入／取消扫描、时钟读取与原生定时等待。

Release 基准每轮向一个工作线程分批提交 16384 个空任务，每批 128 个，
包括等待／释放。修改第 18 批实现（9ba7624）前保存了静态基线可执行文件。
前后版本交替顺序各启动五次进程，每次剔除预热后各保留 25 个有效样本。

最终中位耗时：Windows 基线／新版 9003000/9140800 ns（+1.53%），Linux 为
12575598/12133141 ns（-3.52%）。Windows 范围分别
7593800..11159500、7999800..11217300 ns；Linux 分别
11517734..13372403、11323393..13491747 ns。较早一组方向相反：
Windows 10000500/9567700、Linux 11984708/12118367；之后将无未来任务分支
前置到调度辅助函数调用之前。数据反映本机波动，不能宣称稳定提速或普遍
无回退。没有引入汇编或普通任务额外锁／分配。原始数据保留在忽略的构建
目录 batch19-pool*-perf.json 中。macOS、Win7、处理器组、CPU 热插拔、
ARM/musl、真实板卡仍待验证；这不是尚未执行的 hx_c_log 接入性能验收。

## OBS 补齐第 20 批（2026-09-30）

Windows 全量 CTest 51/51、WSL Linux 53/53；ASan/UBSan、TSan 各 8/8，
覆盖本地套接字、双向端点、普通管道、网络、loop 并发、进程、C++ 和示例。
两端 AF_UNIX 测试均实际运行，没有跳过；公共头 C99/C++11 独立编译通过。
两个新模块关闭时各 4/4；NETWORK／PIPE／SYSTEM_UTILS 关闭而独立双向
端点开启时，各 7/7，Windows 同时关闭 LOOP，包含裁剪后的命令行参数入口。
DUPLEX_PIPE／PIPE 关闭而本地套接字开启时，Windows 4/4（LOOP 关），
Linux 6/6（LOOP 开）；Windows x86 相关 4/4 通过。

覆盖 UTF-8／补充平面路径、长度限制、未命名对端、已有路径保留、NOW 接受／
读取超时、连接继续、双向数据、短读循环、IOCP／epoll 读写和取消退役、
loop CONNECT／ACCEPT 明确拒绝、父 network BUSY、文件半关闭、EOF、
关闭写方向后写入失败、分配失败清理和独立网络初始化。
Linux 真实填满监听队列直到 EAGAIN，接受一个连接后重试保留对象成功。
Windows 进程重定向在启动前拒绝套接字文件端点。

初次路径查询发现 UTF-8 验证函数要求非 NULL 的错误偏移输出；本地查询和
共享的裁剪命令行参数入口现补传该指针，保留既有验证契约。

布局：所有实测目标的 local_address 108/1、私有 local_state 112/4；
启用本地支持时，x64 socket 由 56/8 变为 64/8，Windows x86 由 40/8
变为 48/8；仅本地套接字在同一次分配尾部追加状态。
Windows duplex_file 为 x64 32/8、x86 20/4；POSIX 复用不变的 12/4 file。
既有公共布局及 operation/request 大小不变，未引入汇编或第三方依赖。

原生 macOS、Win7 回退／提供程序缺失、嵌入式／musl、防火墙拒绝、
错误对端连接洪泛、原生关闭失败注入仍待验证；没有声称 Windows／WSL
互通或 TCP 回退具有同等性能。


## OBS 补齐第 21 批（2026-09-30）

Windows 全量 57/57，WSL Linux 全量 59/59；ASan/UBSan 相关 8/8。
公共头独立 C99/C++11 编译通过。四项全部关闭，两端各 5/5；Windows x86
相关 5/5。Windows 在 RANDOM、SLEEP_INHIBIT 关闭、REGISTRY 和性能请求开启
时相关 6/6，验证独立链接与裁剪。

Windows 实测覆盖独占临时 HKCU 键、中文/补充平面字符、环境引用字面值、
损坏 UTF-16、类型不匹配、容量不足不改输出、只读写失败、两个独立防睡眠
请求、分配失败回滚、重复激活，以及转移至线程后释放并等待退出。
注册表测试仅删除本次独占创建的叶子，并注册退出清理。混淆工具生成的 C
数据实际编译并解码，覆盖非 ASCII、C++11 constexpr 和空字面量。

Linux 注册表和电源测试验证的是不支持路径；Linux ASan 不代表 Windows
原生实现已通过 ASan。Apple 实现尚未在原生环境编译。macOS、Win7 的性能
请求拒绝、真实睡眠策略、权限故障和嵌入式目标仍待验证；不声称固定 CPU
频率，也不将 Windows 所有权转移测试记成 TSan 证据。

私有 power 状态：Windows/Linux x64 为 56/8，Windows x86 为 32/4；混淆
借用视图分别为 24/8、12/4（大小/对齐）。既有公开布局不变。只在显式创建
时分配，无后台线程、全局分配器、热路径钩子或汇编。格式配置增加对独立
Objective-C 文件的接受，C/C++ 规则保持一致；第三方源码未改，源码规则与
差异空白检查通过。
