# 可运行示例目录：从具体任务开始

[English](README.md) · [入门教程](../docs/USAGE.zh-CN.md) · [返回首页](../README.zh-CN.md)

本目录下的每个演示程序都围绕一个具体开发任务编写，完整展示**准备输入 → 调用接口 → 检查错误 → 释放资源**的全过程。你可以先挑一个与业务最接近的示例直接编译运行，跑通后再把其中的输入数据和处理逻辑替换成自己的业务代码。

> [!NOTE]
> 如果你使用的是预编译 SDK 安装包，这些示例源码位于安装目录的 `share/doc/hx_c_platform/examples` 下。请按照 [SDK 安装包接入指南](../docs/INSTALL.zh-CN.md) 复制对应的 `.c` / `.cpp` 源码及同目录配套头文件进行编译；下方的 `--target` 构建命令适用于完整源码仓库。

<a id="run"></a>

## 1. 按需编译单个示例程序

在库源码根目录下执行以下命令（请将 `build` 替换为你当前工具链对应的构建目录，如 `build-msvc` 或 `build-linux`）：

- **启用完整 SDK 模块时**：首次构建前请先按 [依赖构建指南](../docs/DEPENDENCIES.zh-CN.md) 编译好第三方原生 SDK。
- **只想跑纯 C 基础示例时**：也可直接采用 [首页的精简 CMake 配置](../README.zh-CN.md#section-2)，将选定的示例文件作为 `main.c` 编译。

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --target hx_os_binary_stream
```

> [!TIP]
> - 开启 `HX_OS_BUILD_EXAMPLES=ON` 后，CMake 会自动检测系统中的 C++ 编译器；若编译器与标准库支持 C++20 `<coroutine>`，还会自动加入 C++20 协程示例（无需额外开关）；若环境中只有 C 编译器，则仅生成 C 示例。
> - 如果你使用的是 Debug 配置，请将构建命令和下方运行路径中的 `Release` 一并替换为 `Debug`。切勿混用 Windows 与 WSL 的构建目录。

编译完成后，进入构建目录运行程序（这样示例生成的临时演示文件都会留在构建目录内）：

```powershell
# Windows (Visual Studio 多配置生成器)
cd build
.\bin\Release\hx_os_binary_stream.exe
```

```sh
# Linux / WSL（或 Windows Ninja 单配置生成器）
cd build
./bin/hx_os_binary_stream
```

`hx_os_binary_stream` 不需要额外输入文件，运行后会输出 `device=42 temperature=23.5`（先按大端序写入设备编号和温度，再按相同字节序读回）。运行其他示例时，只需把构建目标名与可执行文件名替换成下方表格中的对应名称即可。

<a id="inputs"></a>

## 2. 准备测试输入文件（针对文件与配置类示例）

以下命令均在上一节进入的 `build` 目录下执行。先创建一个仅包含 10 个 ASCII 字符 `0123456789` 的测试文件 `demo.txt`：

```powershell
# Windows PowerShell
Set-Content -LiteralPath demo.txt -Value '0123456789' `
    -NoNewline -Encoding ascii
```

```sh
# Linux / WSL
printf '0123456789' > demo.txt
```

随后即可将 `demo.txt` 作为命令行参数传给文件类示例。例如运行偏移读取示例：
- Windows：`.\bin\Release\hx_os_positioned_read.exe demo.txt`
- Linux：`./bin/hx_os_positioned_read demo.txt`
- 预期输出：`34 35 36 37`（即从字节偏移 4 开始读取的 4 个字符 `4567` 的十六进制值）。

| 示例目标名 | 命令行参数 | 运行后会发生什么 |
| --- | --- | --- |
| `hx_os_text_file` | `demo.txt` 或 `demo.txt copied.txt` | 读取并打印文本；传入第 2 个路径时会将内容另存为副本（覆盖目标文件） |
| `hx_os_read_lines` | `demo.txt` | 只读逐行扫描；可将 `demo.txt` 改为多行文本观察逐行输出 |
| `hx_os_file_attributes` | `demo.txt` | 更新 `demo.txt` 的最后修改时间，保留访问时间 |
| `hx_os_find_files` | `../examples "*.c"` | 只读递归扫描目录；通配符模式需加引号以防被 Shell 提前展开 |
| `hx_os_watch_directory` | 一个已存在的目录路径 | 监听该目录 5 秒；可在另一终端内创建或修改文件以观察事件输出 |
| `hx_os_config_save` | 不传参数，或 `saved.json` | 不传参时向终端打印 JSON；传路径时将配置原子保存到该文件 |
| `hx_os_config_edit` | 不传参数，或 `--restore` | 在当前目录读写 `preferences.ini`；覆盖已有文件时自动生成备份 |
| `hx_os_buffered_file` | 不传参数 | 在后台异步写出 3 条记录到当前目录的 `samples.jsonl` |
| `hx_os_persistent_queue` | 先执行 `publish records.db`，再执行 `consume records.db` | 创建/更新 SQLite 队列文件 `records.db`，消费并确认（ACK）后移除消息 |

> [!WARNING]
> - 测试 `hx_os_config_edit --restore` 前，需先不带参数运行至少两次以生成旧版本备份（首次保存全新文件时没有旧文件可供备份）。
> - 运行带写入或属性修改的示例时，请始终使用 `demo.txt` 等临时文件，切勿直接传入生产配置文件。

<a id="data"></a>

## 3. 字符串、数组与容器示例

| 应用场景与专题文档 | 源码与 CMake 目标 | 预期输出摘要 |
| --- | --- | --- |
| [将二进制字节编码为 Base64 并还原](../docs/ENCODING.zh-CN.md) | [encoding.c](encoding.c) · `hx_os_encoding` | `Base64: AAH+/w==`；还原 4 字节 |
| [按 Unicode 规则执行大小写折叠](../docs/UNICODE.zh-CN.md) | [unicode_tools.c](unicode_tools.c) · `hx_os_unicode_tools` | `strasse`（将德语 `Straße` 折叠为小写） |
| [零拷贝切分字段、严格解析整数与自然排序](../docs/TEXT_UTILITIES.zh-CN.md) | [text_utilities.c](text_utilities.c) · `hx_os_text_utilities` | `Port: 443, offset: -42` |
| [C99 花括号 `{}` 格式化打印](../docs/FORMAT.zh-CN.md) | [format.c](format.c) · `hx_os_format` | `张三: 3.75` |
| [使用 FIFO 队列存取整数](../docs/CONTAINERS.zh-CN.md) | [containers.c](containers.c) · `hx_os_containers` | `Queue result: 42` |
| [红黑树有序映射（支持重复键遍历）](../docs/CONTAINERS.zh-CN.md) | [ordered_map.c](ordered_map.c) · `hx_os_ordered_map` | `a: 10`, `b: 20`, `b: 21` |
| [C 结构体数组排序、二分查找、过滤与聚合](../docs/ALGORITHMS.zh-CN.md) | [algorithms.c](algorithms.c) · `hx_os_algorithms` | 按 ID 升序 `17, 23, 31`；统计 2 项待处理 |
| [C++ 数组与链表迭代器算法](../docs/ALGORITHMS.zh-CN.md) | [algorithms_cpp.cpp](algorithms_cpp.cpp) · `hx_os_algorithms_cpp` | 输出排序后的 ID 序列 |
| [动态字节队列与固定两段式环形缓冲](../docs/RING.zh-CN.md) | [byte_queue_ring.c](byte_queue_ring.c) · `hx_os_byte_queue_ring` | `ByteQueue: 5 bytes: hello`; `Ring: efxxxxxx` |
| [用动态位图（Bitset）追踪编号任务完成度](../docs/BITSET.zh-CN.md) | [bitset.c](bitset.c) · `hx_os_bitset` | `Completed: 11 of 100` |
| [带容量上限与过期时间的 LRU 缓存](../docs/CACHE.zh-CN.md) | [lru_cache.c](lru_cache.c) · `hx_os_lru_cache` | `sensor-b` 被淘汰；`temporary` 到期失效 |
| [随机抽样、基数排序与多模式字节搜索](../docs/FAST_TOOLS.zh-CN.md) | [fast_algorithms.c](fast_algorithms.c) · `hx_os_fast_algorithms` | `First error: byte 16` |
| [Varint、ZigZag 变长整数与 UUID v7](../docs/INTEGER_TOOLS.zh-CN.md) | [integer_tools.c](integer_tools.c) · `hx_os_integer_tools` | `300 uses 2 bytes; decoded: 300` |
| [UTF-8 正则分组捕获与文本替换](../docs/REGEX.zh-CN.md) | [regex.c](regex.c) · `hx_os_regex` | `Door_01: #42` |
| [在单个 Variant 变量中保存多种类型与结构体](../docs/VARIANT.zh-CN.md) | [variant.c](variant.c) · `hx_os_variant` | 依次存取整数、字符串与自定义结构体 |

<a id="config"></a>

## 4. 配置、参数包与文本解析示例

| 应用场景与专题文档 | 源码与 CMake 目标 | 预期输出摘要 |
| --- | --- | --- |
| [从内置 TOML 配置中读取服务端口](../docs/CONFIGURATION.zh-CN.md) | [configuration.c](configuration.c) · `hx_os_configuration` | `Server port: 8080` |
| [序列化打印 JSON 或原子保存到指定文件](../docs/CONFIGURATION.zh-CN.md) | [config_save.c](config_save.c) · `hx_os_config_save` | 打印 JSON，或传入路径时输出 `Saved` |
| [生成 INI 配置、自动备份与显式恢复](../docs/CONFIG_EDIT.zh-CN.md) | [config_edit.c](config_edit.c) · `hx_os_config_edit` | `Saved preferences.ini` |
| [C 结构体与嵌套配置文档双向序列化](../docs/SERIALIZATION.zh-CN.md) | [serialization.c](serialization.c) · `hx_os_serialization` | 默认输出 JSON，可通过参数切换 yaml/xml/toml |
| [用声明宏同时定义结构体与字段映射](../docs/SERIALIZATION.zh-CN.md) | [codec_macros.c](codec_macros.c) · `hx_os_codec_macros` | 输出序列化 JSON 及 `port = 8080` |
| [手工构造深层混合类型配置树并写出](../docs/SERIALIZATION.zh-CN.md) | [nested_document.c](nested_document.c) · `hx_os_nested_document` | 输出格式化后的嵌套 JSON |
| [修改运行时可变设置并导出只读快照](../docs/COMMON_UTILITIES.zh-CN.md) | [settings.c](settings.c) · `hx_os_settings` | 打印各字段读取结果与 JSON |
| [按名称传递字符串、整数与二进制数据块](../docs/PARAMETERS.zh-CN.md) | [parameters.c](parameters.c) · `hx_os_parameters` | `Device 42 (camera): 4 binary bytes` |
| [解析自定义脚本语法并跳过嵌套扩展块](../docs/PARSING.zh-CN.md) | [parse_script.c](parse_script.c) · `hx_os_parse_script` | `Title: Camera` |
| [展开配置中的运行时常量与条件分支](../docs/PREPROCESSING.zh-CN.md) | [preprocess_config.c](preprocess_config.c) · `hx_os_preprocess_config` | 输出展开后的 JSON（`Camera, 8080, true`） |

<a id="threads"></a>

## 5. 线程、协程、并发队列与信号通知示例

| 应用场景与专题文档 | 源码与 CMake 目标 | 预期输出摘要 |
| --- | --- | --- |
| [向 C++11 线程池提交计算任务并获取结果](../docs/CPP_POOL.zh-CN.md) | [thread_pool.cpp](thread_pool.cpp) · `hx_os_cpp_thread_pool` | `Task result: 42` |
| [启动有栈协程并通过 Channel 传递计算结果](../docs/COROUTINES.zh-CN.md) | [go.cpp](go.cpp) · `hx_os_go` | `Answer: 42` |
| [C99 无栈协程状态机](../docs/COROUTINES.zh-CN.md) | [coroutine.c](coroutine.c) · `hx_os_coroutine` | 逐步输出递增的 `Count` 值 |
| [C++20 `co_await` 无栈协程（按需启用）](../docs/COROUTINES.zh-CN.md) | [coroutine20.cpp](coroutine20.cpp) · `hx_os_cpp_coroutine20` | `Answer: 42` |
| [生产者线程批量入队、主线程批量消费](../docs/QUEUES.zh-CN.md) | [message_queue.c](message_queue.c) · `hx_os_message_queue` | 接收并打印 6 条设备采样记录 |
| [协作式取消、`WaitGroup` 与读写锁配合](../docs/COORDINATION.zh-CN.md) | [coordination.c](coordination.c) · `hx_os_coordination` | `Processed ... records, then stopped cooperatively.` |
| [多线程并发下的单次初始化（Once）](../docs/ONCE.zh-CN.md) | [once.c](once.c) · `hx_os_once` | 初始化逻辑仅执行 1 次，4 个工作线程共享结果 |
| [使用 `release` / `acquire` 原子操作发布数据](../docs/ATOMICS.zh-CN.md) | [atomic_publication.c](atomic_publication.c) · `hx_os_atomic_publication` | `answer: 42` |
| [高精度秒表、指数退避重试与令牌桶限流](../docs/CONTROL.zh-CN.md) | [control.c](control.c) · `hx_os_control` | 打印 3 次退避延迟及总耗时 |
| [使用全局观察者订阅处理器上的所有信号](../docs/SIGNALS.zh-CN.md) | [signal_observer.c](signal_observer.c) · `hx_os_signal_observer` | `ready: 42 items`; `saved: 42 items` |
| [在信号回调中就地修改参数并传回调用方](../docs/SIGNALS.zh-CN.md) | [signal_mutable.c](signal_mutable.c) · `hx_os_signal_mutable` | `Observer/Caller: volume = 60` |
| [层级耗时区间统计与 P50/P95/P99 分位数](../docs/PROFILER.zh-CN.md) | [profile_statistics.c](profile_statistics.c) · `hx_os_profile_statistics` | 12 次样本的均值、标准差与分位数 |
| [命名同步过程调用、文本切分与时间换算](../docs/COMMON_UTILITIES.zh-CN.md) | [common_tasks.c](common_tasks.c) · `hx_os_common_tasks` | `procedure result: 42` |

<a id="io"></a>

## 6. 文件、二进制流与系统平台对象示例

| 应用场景与专题文档 | 源码与 CMake 目标 | 预期输出摘要 |
| --- | --- | --- |
| [验证基础库链接与 UTF-8 / UTF-16 转换](../docs/CONTRACTS.zh-CN.md) | [hello.c](hello.c) · `hx_os_hello` | 打印平台名、UTF-8 字节数、码点数与 UTF-16 字节数 |
| [通过事件循环完成一次异步定时器请求](../docs/PLATFORM_UTILITIES.zh-CN.md) | [async_timer.c](async_timer.c) · `hx_os_async_timer` | `Timer completed through ...`（显示当前平台后端） |
| [读取文本文件并可选另存为副本](../docs/PLATFORM_UTILITIES.zh-CN.md) | [text_file.c](text_file.c) · `hx_os_text_file` | 打印输入文件内容 |
| [带长度限制的 UTF-8 逐行读取](../docs/LINE_READER.zh-CN.md) | [read_lines.c](read_lines.c) · `hx_os_read_lines` | 逐行打印内容（自动剥离行尾换行符） |
| [按指定偏移量无状态读取文件片段](../docs/PLATFORM_UTILITIES.zh-CN.md) | [positioned_read.c](positioned_read.c) · `hx_os_positioned_read` | 输入 `demo.txt` 输出 `34 35 36 37` |
| [将现有文件映射为只读内存并计算 CRC32](../docs/PLATFORM_UTILITIES.zh-CN.md) | [mapped_file.c](mapped_file.c) · `hx_os_mapped_file` | 打印映射字节大小与 CRC32 校验值 |
| [显式修改文件修改时间并保持访问时间不变](../docs/PLATFORM_UTILITIES.zh-CN.md) | [file_attributes.c](file_attributes.c) · `hx_os_file_attributes` | `Modification time updated; access time preserved.` |
| [边递归扫描目录边流式处理匹配路径](../docs/FILE_SEARCH.zh-CN.md) | [find_files.c](find_files.c) · `hx_os_find_files` | 打印匹配路径列表与文件总数 |
| [监听指定目录的文件变更事件（持续 5 秒）](../docs/PLATFORM_UTILITIES.zh-CN.md) | [watch_directory.c](watch_directory.c) · `hx_os_watch_directory` | 打印 `Watching` 及捕获到的文件变更事件 |
| [借用底层原生文件句柄查询文件大小](../docs/NATIVE_INTEROP.zh-CN.md) | [native_file.c](native_file.c) · `hx_os_native_file` | `Native query: ... bytes` |
| [使用二进制流按大端序写入并读回数值](../docs/STREAMS.zh-CN.md) | [binary_stream.c](binary_stream.c) · `hx_os_binary_stream` | `device=42 temperature=23.5` |
| [为 JSON 消息添加长度前缀帧并解析](../docs/FRAMES.zh-CN.md) | [framed_messages.c](framed_messages.c) · `hx_os_framed_messages` | `{"device":"door-1","request":42}` |
| [使用有界后台文件写入器异步落盘 JSONL](../docs/STREAMS.zh-CN.md) | [buffered_file.c](buffered_file.c) · `hx_os_buffered_file` | 生成 `samples.jsonl`，打印 `Saved 3 device readings` |
| [查询操作系统版本与当前进程 CPU 使用率](../docs/PLATFORM_UTILITIES.zh-CN.md) | [system_info.c](system_info.c) · `hx_os_system_info` | 打印操作系统名称与进程 CPU 采样值 |
| [为客户 EXE 显式配置 Windows WER 崩溃转储](../docs/LOCAL_DUMPS.zh-CN.md) | [local_dumps.c](local_dumps.c) · `hx_os_local_dumps` | 无参显示帮助；`query` 只读查询；修改需管理员授权 |
| [使用高分辨率时钟测量短等待真实耗时](../docs/CONTROL.zh-CN.md) | [high_resolution_time.c](high_resolution_time.c) · `hx_os_high_resolution_time` | 请求等待 20 ms 并打印实际测得的纳秒耗时 |
| [在关键任务期间向系统申请防休眠](../docs/LINUX_DESKTOP.zh-CN.md) | [sleep_inhibit.c](sleep_inhibit.c) · `hx_os_sleep_inhibit` | 打印申请/释放成功或返回 `HX_OS_NOT_SUPPORTED` |

<a id="sdk"></a>

## 7. 密码学、归档压缩与外部服务示例

| 应用场景与专题文档 | 源码与 CMake 目标 | 预期输出摘要 |
| --- | --- | --- |
| [使用口令加密 UTF-8 文本并解密还原](../docs/CRYPTO.zh-CN.md) | [encrypt_text.c](encrypt_text.c) · `hx_os_encrypt_text` | `Decrypted: Hello, UTF-8!` |
| [分块流式计算 SHA-256 摘要](../docs/INCREMENTAL_HASH.zh-CN.md) | [incremental_hash.c](incremental_hash.c) · `hx_os_incremental_hash` | 输出 `"abc"` 的 64 位十六进制 SHA-256 摘要 |
| [在内存中压缩 JSON 字节流并解压还原](../docs/UTILITY_EXTENSIONS.zh-CN.md) | [compress_memory.c](compress_memory.c) · `hx_os_compress_memory` | 打印压缩前后字节数及还原后的 JSON |
| [打包、列出或解包文件与目录归档](../docs/ARCHIVE.zh-CN.md) | [archive_files.c](archive_files.c) · `hx_os_archive_files` | 打印归档条目清单及处理文件数 |
| [解析 URL 并安全编码查询参数（Query）](../docs/UTILITY_EXTENSIONS.zh-CN.md) | [url_query.c](url_query.c) · `hx_os_url_query` | 打印百分号编码后的完整 URL 与解码参数 |
| [用 GET 获取 HTTP/HTTPS 响应](../docs/UTILITY_EXTENSIONS.zh-CN.md) | [http_json.c](http_json.c) · `hx_os_http_json` | 打印 HTTP 状态码及响应正文 |
| [SQLite 持久化消息入队、消费与 ACK 确认](../docs/PERSISTENT_QUEUE.zh-CN.md) | [persistent_queue.c](persistent_queue.c) · `hx_os_persistent_queue` | 输出 3 行 `Handle recognition-...` 处理结果 |
| [通过串口发送一行 JSON 指令并读取设备回复](../docs/UTILITY_EXTENSIONS.zh-CN.md) | [serial_message.c](serial_message.c) · `hx_os_serial_message` | 打印设备响应（无串口设备时返回错误或超时） |
| [父子进程通过命名共享内存交接 JSON 数据](../docs/UTILITY_EXTENSIONS.zh-CN.md) | [shared_memory.c](shared_memory.c) · `hx_os_shared_memory` | `Child received: ...` |
| [直接调用随包安装的 SQLite / OpenSSL / curl 原生 API](../docs/NATIVE_DEPENDENCIES.zh-CN.md) | [native_dependencies.c](native_dependencies.c) · `hx_os_native_dependencies` | 打印各底层原生 SDK 调用结果 |

<a id="network"></a>

## 8. 客户端/服务端双端网络通信示例

如果想要枚举本机 IP 网卡并发送组播发现报文，可以构建 `hx_os_network_interfaces` 和 `hx_os_multicast_discovery`（双终端命令与 Keepalive、双栈、广播配置详见 [网络选项与设备发现](../docs/NETWORK_OPTIONS.zh-CN.md)）。

下方表格中的 5 套完整业务示例均包含独立的服务端与客户端程序。为了方便你在单机上快速验证，仓库提供了配套的 Python 启动脚本（仅用于自动拉起服务端与客户端两个进程并传入空闲端口，不参与实际业务通信）：

| 业务通信场景 | 一键演示脚本 | 专题文档与构建说明 |
| --- | --- | --- |
| 本机跨进程双向设备控制消息（带设备号与请求号） | `run_ipc_business.py` | [IPC 独立示例](../docs/IPC_BUSINESS.zh-CN.md) |
| TCP 订单提交与异步回执 | `run_tcp_business.py` | [TCP 独立示例](../docs/TCP_BUSINESS.zh-CN.md) |
| UDP 传感器采样上报（含丢包、乱序、重复检测） | `run_udp_business.py` | [UDP 独立示例](../docs/UDP_BUSINESS.zh-CN.md) |
| 基于 KCP 的可靠 UDP 数据块传输 | `run_kcp_business.py` | [KCP 独立示例](../docs/KCP_BUSINESS.zh-CN.md) |
| 双向证书校验 TLS 连接上的多线程 JSON 收发 | `run_tls_business.py` | [TLS 指南与证书准备](../docs/TLS.zh-CN.md) |

在库源码根目录下运行启动脚本（请先按对应专题文档构建好客户端和服务端两个目标）：

```sh
# Windows (MSVC Release)
python examples/run_tcp_business.py --bin-dir build/bin/Release

# Linux / WSL
python3 examples/run_tcp_business.py --bin-dir build/bin
```

> [!IMPORTANT]
> **如何选择网络抽象层与报文格式？**
> - 如果你希望由库自动管理设备 ID、请求序号、连接会话与有界消息队列，请使用 **IPC 模块（`HXIP v1` 协议）**。
> - 如果你需要对接现有的第三方 TCP、UDP、KCP 或 TLS 协议，请直接参考对应的独立业务示例（把对应的 `.c` 源码和同目录配套协议头文件一起复制到你的工程中）：
>
> | 示例名称 | 报文格式（Wire Format） |
> | --- | --- |
> | **IPC 设备控制** | `HXIP v1` 协议帧，载荷为 UTF-8 JSON（详见 [IPC 协议说明](../docs/IPC.zh-CN.md)） |
> | **TCP 订单** | 2 字节大端序正文字节长度 + UTF-8 JSON |
> | **UDP 采样** | 每个 UDP 数据报承载一条完整 UTF-8 JSON（无长度前缀） |
> | **KCP 数据块** | KCP 消息模式下的一条完整 UTF-8 JSON 消息（需定时驱动 KCP 更新） |
> | **TLS 温度读数** | TLS 加密流内的 4 字节大端序正文字节长度 + UTF-8 JSON |

### 高级网络与协程 I/O 示例

| 任务场景 | 源码与 CMake 目标 | 命令行参数与预期结果 |
| --- | --- | --- |
| 接收 UDP 报文的目标地址与接收网卡索引 | [udp_packet.c](udp_packet.c) · `hx_os_udp_packet` | 无参数；打印本机回环 JSON 报文、目标地址与网卡编号 |
| 使用可取消 DNS 解析主机名 | [dns_lookup.c](dns_lookup.c) · `hx_os_dns_lookup` | `[--async\|--coro] HOST [DNS_IP:PORT]`；打印解析出的 IP 地址 |
| 可信局域网内的 KCP 会话请求/应答 | [kcp_session.c](kcp_session.c) · `hx_os_kcp_session` | 先运行 `server IP:PORT`，再运行 `client IP:PORT`；打印请求与回复 |
| HTTP/HTTPS 流式文件上传与下载 | [http_file.c](http_file.c) · `hx_os_http_file` | `download URL FILE [CA]` 或 `upload URL FILE [CA]`；打印 HTTP 状态码 |
| 在 C99 协程中挂起等待 UDP 报文 | [coroutine_udp.c](coroutine_udp.c) · `hx_os_coroutine_udp` | 无参数；打印协程收发给自身的 JSON 报文 |
| 在 C99 协程中挂起等待 HTTP 响应 | [coroutine_http.c](coroutine_http.c) · `hx_os_coroutine_http` | `URL`；打印 HTTP 状态码与响应正文 |

- `hx_os_dns_lookup --async 127.0.0.1` 与 `--coro 127.0.0.1` 分别演示在事件循环与协程中执行非阻塞 DNS 解析。
- `hx_os_http_file` 与 `hx_os_coroutine_http` 支持在末尾追加 `--cancellable-dns` 参数（要求编译 `libcurl` SDK 时开启了 `HX_DEPS_CURL_CARES=ON`，默认系统 DNS 版 SDK 会明确返回 `HX_OS_NOT_SUPPORTED`）。完整命令详见 [网络选项](../docs/NETWORK_OPTIONS.zh-CN.md#123-事件循环和协程里怎么用) 与 [HTTP 传输](../docs/HTTP_FILES.zh-CN.md#4-连-dns-无响应也需要取消时)。

<a id="plugin"></a>

## 9. 动态库加载示例：插件与宿主加载器

在库源码根目录下编译演示插件（`hx_os_demo_plugin`）与加载器（`hx_os_load_plugin`）两个目标：

```sh
cmake --build build --config Release --target hx_os_demo_plugin hx_os_load_plugin
```

进入 `build` 目录后运行加载器，并将编译生成的动态库路径作为参数传入：

```powershell
# Windows
.\bin\Release\hx_os_load_plugin.exe .\bin\Release\hx_os_demo_plugin.dll
```

```sh
# Linux / WSL
./bin/hx_os_load_plugin ./bin/libhx_os_demo_plugin.so
```

**预期输出**：`Compatible plugin: Example plugin 1.0`

- [demo_plugin.c](demo_plugin.c) 演示如何导出 ABI 版本号与描述函数。
- [load_plugin.c](load_plugin.c) 演示如何加载动态库、校验 ABI 版本、绑定函数指针并在调用结束后安全卸载动态库。

## 10. 将示例代码改造成你的业务代码

在将示例迁移到实际工程时，建议遵循以下顺序：

1. **先替换输入参数与业务数据**，再把打印语句替换成你的业务处理逻辑。
2. **保持资源创建、状态检查与销毁的配对顺序**：凡是包含后台线程或异步 I/O 的对象，退出前必须先停止新任务准入并等待现有任务完成。如果等待超时（`HX_OS_TIMEOUT`），切勿提前释放后台任务仍在借用的内存。
3. **关注平台能力边界**：各目标平台的详细限制详见 [平台支持说明](../docs/PLATFORMS.zh-CN.md)，并发、容量上限与卸载契约详见各专题文档末尾章节。
