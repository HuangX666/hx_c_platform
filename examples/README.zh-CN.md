# 可运行示例：从一个具体任务开始

[English](README.md) · [入门教程](../docs/USAGE.zh-CN.md) · [库首页](../README.zh-CN.md)

演示程序按任务组织，展示输入、调用、结果和资源释放；配套头文件和插件源码由相应例子说明。先选与你要做的事情接近的一项，读懂并运行后，再替换数据和业务处理。这里是公开用法示例，不要求维护者的私有回归文件。

安装 SDK 时，这些公开源码和说明位于 `share/doc/hx_c_platform/examples`。从安装包阅读时，按[安装包工程](../docs/INSTALL.zh-CN.md)复制需要的源码及同目录头文件并链接库；下面的目标构建命令用于完整源码仓库。

<a id="run"></a>

## 1. 只构建要运行的那个程序

在库源码根目录操作。下面的 `build` 代表**已有且工具链匹配的构建目录**，请换成你的目录，例如 `build-shared-msvc` 或 `build-shared-linux`。首次接入先完成[依赖准备](../docs/DEPENDENCIES.zh-CN.md)；默认 SDK 模块需要匹配的依赖。只想从纯 C 小例子开始时，可用[首页的精简源码配置](../README.zh-CN.md#section-2)，将选定的示例文件作为 main。

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --target hx_os_binary_stream
```

如果现有配置使用 Debug，把构建和运行路径里的 Release 一起换成 Debug。不要混用 Windows 与 WSL 的构建目录。启用 examples 后，CMake 自动查找 C++ 编译器；检测到 C++20 的 `<coroutine>` 支持后才添加相应示例，不需要另开一个开关。缺少 C++ 编译器时只生成 C 示例。

进入这个构建目录运行，让演示生成的文件留在构建目录内。Windows Visual Studio：

```powershell
cd build
.\bin\Release\hx_os_binary_stream.exe
```

Linux／WSL：

```sh
cd build
./bin/hx_os_binary_stream
```

此例不需要输入文件，输出 `device=42 temperature=23.5`。它先写设备编号和温度，再用相同字节序读回。下面的表格给出目标名；把命令里的目标／程序名一起换成所选项。Windows Ninja 的程序通常直接位于 `bin`，不带配置子目录。

完整程序包含创建和清理；专题里的短片段可能只展示其中一步。不要将多个 main 拼到一起，也不要省掉失败后的资源回收。返回状态不成功时先读错误提示，模块关闭会明确报告不支持。

<a id="inputs"></a>

## 2. 准备文件输入，看清楚写入位置

以下命令均在上一步进入的构建目录执行。为文件示例准备一个只含 `0123456789` 的演示文件：

```powershell
Set-Content -LiteralPath demo.txt -Value '0123456789' `
    -NoNewline -Encoding ascii
```

Linux／WSL 用：

```sh
printf '0123456789' > demo.txt
```

将 `demo.txt` 传给 `text_file`、`read_lines`、`positioned_read`、`mapped_file` 或 `native_file`。例如 Windows 运行 `.\bin\Release\hx_os_positioned_read.exe demo.txt`；Linux 运行 `./bin/hx_os_positioned_read demo.txt`，结果是 `34 35 36 37`，即偏移 4 起的字符 4567。

| 程序 | 输入／命令行 | 会改变什么 |
| --- | --- | --- |
| `hx_os_text_file` | `demo.txt` 或 `demo.txt copied.txt` | 带第二个路径时保存副本；可能替换该目标 |
| `hx_os_read_lines` | `demo.txt` | 只读；可把文件改成多行观察逐行结果 |
| `hx_os_file_attributes` | `demo.txt` | 更新这个演示文件的修改时间 |
| `hx_os_find_files` | `../examples "*.c"` | 只读扫描，模式用引号避免 shell 提前展开 |
| `hx_os_watch_directory` | 一个已存在的目录 | 监听五秒；在另一个终端创建或修改文件观察事件 |
| `hx_os_config_save` | 不传参数，或 `saved.json` | 不传时打印 JSON；传路径时保存到该位置 |
| `hx_os_config_edit` | 不传参数，或 `--restore` | 使用当前目录 preferences.ini；已有原文件时产生备份 |
| `hx_os_buffered_file` | 不传参数 | 写入 samples.jsonl |
| `hx_os_persistent_queue` | `publish records.db`，然后 `consume records.db` | 创建／更新 SQLite 文件，成功确认后移出消息 |

恢复配置前应先存在一次成功保存产生的备份；第一次保存全新文件并不会凭空产生旧版本。运行写入示例时使用演示文件，不要把正式配置作为练习输入。

<a id="data"></a>

## 3. 字符串、数组与容器

| 什么时候用 | 完整源码与构建目标 | 主要结果 |
| --- | --- | --- |
| [把字节转成 Base64 再还原](../docs/ENCODING.zh-CN.md) | [encoding.c](encoding.c) · `hx_os_encoding` | `Base64: AAH+/w==`; 4 bytes |
| [按 Unicode 规则折叠大小写](../docs/UNICODE.zh-CN.md) | [unicode_tools.c](unicode_tools.c) · `hx_os_unicode_tools` | `strasse` |
| [拆分字段、读取整数、自然比较](../docs/TEXT_UTILITIES.zh-CN.md) | [text_utilities.c](text_utilities.c) · `hx_os_text_utilities` | `Port: 443, offset: -42` |
| [C99 花括号打印](../docs/FORMAT.zh-CN.md) | [format.c](format.c) · `hx_os_format` | `张三: 3.75` |
| [按先入先出顺序取整数](../docs/CONTAINERS.zh-CN.md) | [containers.c](containers.c) · `hx_os_containers` | `Queue result: 42` |
| [红黑树按键遍历，保留重复键](../docs/CONTAINERS.zh-CN.md) | [ordered_map.c](ordered_map.c) · `hx_os_ordered_map` | `a: 10`, `b: 20`, `b: 21` |
| [结构体排序、查询、筛选和累计](../docs/ALGORITHMS.zh-CN.md) | [algorithms.c](algorithms.c) · `hx_os_algorithms` | ID 顺序 17、23、31；2 项待处理 |
| [C++ 数组和链表算法](../docs/ALGORITHMS.zh-CN.md) | [algorithms_cpp.cpp](algorithms_cpp.cpp) · `hx_os_algorithms_cpp` | 有序 ID |
| [动态字节队列与固定双片段环形缓冲](../docs/RING.zh-CN.md) | [byte_queue_ring.c](byte_queue_ring.c) · `hx_os_byte_queue_ring` | `ByteQueue: 5 bytes: hello`; `Ring: efxxxxxx` |
| [记录编号任务是否完成](../docs/BITSET.zh-CN.md) | [bitset.c](bitset.c) · `hx_os_bitset` | `Completed: 11 of 100` |
| [缓存最近结果并处理过期](../docs/CACHE.zh-CN.md) | [lru_cache.c](lru_cache.c) · `hx_os_lru_cache` | sensor-b 淘汰；temporary 过期 |
| [抽样、基数排序、多模式查找](../docs/FAST_TOOLS.zh-CN.md) | [fast_algorithms.c](fast_algorithms.c) · `hx_os_fast_algorithms` | `First error: byte 16` |
| [Varint、ZigZag 与 UUID v7](../docs/INTEGER_TOOLS.zh-CN.md) | [integer_tools.c](integer_tools.c) · `hx_os_integer_tools` | `300 uses 2 bytes; decoded: 300` |
| [捕获字段并替换文字](../docs/REGEX.zh-CN.md) | [regex.c](regex.c) · `hx_os_regex` | `Door_01: #42` |
| [同一变量保存不同类型](../docs/VARIANT.zh-CN.md) | [variant.c](variant.c) · `hx_os_variant` | 整数、字符串、结构体 |

<a id="config"></a>

## 4. 配置、参数与解析

| 什么时候用 | 完整源码与构建目标 | 主要结果 |
| --- | --- | --- |
| [从内置 TOML 读出端口](../docs/CONFIGURATION.zh-CN.md) | [configuration.c](configuration.c) · `hx_os_configuration` | `Server port: 8080` |
| [输出 JSON，或保存到给定文件](../docs/CONFIGURATION.zh-CN.md) | [config_save.c](config_save.c) · `hx_os_config_save` | JSON; 指定路径时 Saved |
| [生成 INI、保存备份、显式恢复](../docs/CONFIG_EDIT.zh-CN.md) | [config_edit.c](config_edit.c) · `hx_os_config_edit` | `Saved preferences.ini` |
| [结构体和嵌套文档往返转换](../docs/SERIALIZATION.zh-CN.md) | [serialization.c](serialization.c) · `hx_os_serialization` | 默认 JSON，可选 yaml/xml/toml |
| [用宏声明结构体及字段映射](../docs/SERIALIZATION.zh-CN.md) | [codec_macros.c](codec_macros.c) · `hx_os_codec_macros` | JSON; `port = 8080` |
| [构造混合类型嵌套树](../docs/SERIALIZATION.zh-CN.md) | [nested_document.c](nested_document.c) · `hx_os_nested_document` | 序列化后的 JSON |
| [修改设置并取得快照](../docs/COMMON_UTILITIES.zh-CN.md) | [settings.c](settings.c) · `hx_os_settings` | 读取结果和 JSON |
| [按名字传字符串、整数和字节](../docs/PARAMETERS.zh-CN.md) | [parameters.c](parameters.c) · `hx_os_parameters` | `Device 42 (camera): 4 binary bytes` |
| [读取自定义语法并跳过扩展块](../docs/PARSING.zh-CN.md) | [parse_script.c](parse_script.c) · `hx_os_parse_script` | `Title: Camera` |
| [展开运行时文本的常量和条件](../docs/PREPROCESSING.zh-CN.md) | [preprocess_config.c](preprocess_config.c) · `hx_os_preprocess_config` | JSON: Camera, 8080, true |

<a id="threads"></a>

## 5. 线程、协程和通知

| 什么时候用 | 完整源码与构建目标 | 主要结果 |
| --- | --- | --- |
| [提交后台计算并取得结果](../docs/CPP_POOL.zh-CN.md) | [thread_pool.cpp](thread_pool.cpp) · `hx_os_cpp_thread_pool` | `Task result: 42` |
| [启动协程并通过 channel 取得值](../docs/COROUTINES.zh-CN.md) | [go.cpp](go.cpp) · `hx_os_go` | `Answer: 42` |
| [C99 无栈状态机](../docs/COROUTINES.zh-CN.md) | [coroutine.c](coroutine.c) · `hx_os_coroutine` | Count 逐步增加 |
| [可选 C++20 co_await 写法](../docs/COROUTINES.zh-CN.md) | [coroutine20.cpp](coroutine20.cpp) · `hx_os_cpp_coroutine20` | `Answer: 42` |
| [生产线程批量发送，主线程批量接收](../docs/QUEUES.zh-CN.md) | [message_queue.c](message_queue.c) · `hx_os_message_queue` | 6 条设备样本 |
| [取消任务、等完成、保护共享数据](../docs/COORDINATION.zh-CN.md) | [coordination.c](coordination.c) · `hx_os_coordination` | `Processed ... records, then stopped cooperatively.` |
| [多个线程共享一次初始化](../docs/ONCE.zh-CN.md) | [once.c](once.c) · `hx_os_once` | 初始化提示 1 次，工作线程提示 4 次 |
| [用 release/acquire 发布结果](../docs/ATOMICS.zh-CN.md) | [atomic_publication.c](atomic_publication.c) · `hx_os_atomic_publication` | `answer: 42` |
| [秒表、退避和令牌桶](../docs/CONTROL.zh-CN.md) | [control.c](control.c) · `hx_os_control` | 3 次重试延迟及本次耗时 |
| [订阅一个处理器的所有信号](../docs/SIGNALS.zh-CN.md) | [signal_observer.c](signal_observer.c) · `hx_os_signal_observer` | `ready: 42 items`; `saved: 42 items` |
| [回调修改参数，观察者读取结果](../docs/SIGNALS.zh-CN.md) | [signal_mutable.c](signal_mutable.c) · `hx_os_signal_mutable` | Observer/Caller: volume = 60 |
| [区间统计和近似分位数](../docs/PROFILER.zh-CN.md) | [profile_statistics.c](profile_statistics.c) · `hx_os_profile_statistics` | 12 次样本；均值、标准差和分位数 |
| [命名过程、文本拆分与时间换算](../docs/COMMON_UTILITIES.zh-CN.md) | [common_tasks.c](common_tasks.c) · `hx_os_common_tasks` | `procedure result: 42` |

<a id="io"></a>

## 6. 文件、数据流和平台对象

| 什么时候用 | 完整源码与构建目标 | 主要结果 |
| --- | --- | --- |
| [确认基本链接和 UTF 编码调用](../docs/CONTRACTS.zh-CN.md) | [hello.c](hello.c) · `hx_os_hello` | 平台名、字节数、码点数和 UTF-16 单元数 |
| [完成一次异步定时请求](../docs/PLATFORM_UTILITIES.zh-CN.md) | [async_timer.c](async_timer.c) · `hx_os_async_timer` | `Timer completed through ...`，后端依平台而定 |
| [读取文本，可选保存副本](../docs/PLATFORM_UTILITIES.zh-CN.md) | [text_file.c](text_file.c) · `hx_os_text_file` | 显示输入文字 |
| [按行读取文本](../docs/LINE_READER.zh-CN.md) | [read_lines.c](read_lines.c) · `hx_os_read_lines` | 逐行输出，不带原换行符 |
| [从字节偏移 4 读取四个字节](../docs/PLATFORM_UTILITIES.zh-CN.md) | [positioned_read.c](positioned_read.c) · `hx_os_positioned_read` | demo.txt → `34 35 36 37` |
| [把已有文件映射为只读字节](../docs/PLATFORM_UTILITIES.zh-CN.md) | [mapped_file.c](mapped_file.c) · `hx_os_mapped_file` | 映射大小和 CRC32 |
| [显式更新文件修改时间](../docs/PLATFORM_UTILITIES.zh-CN.md) | [file_attributes.c](file_attributes.c) · `hx_os_file_attributes` | `Modification time updated; access time preserved.` |
| [边递归扫描边返回文件路径](../docs/FILE_SEARCH.zh-CN.md) | [find_files.c](find_files.c) · `hx_os_find_files` | 匹配路径和 Files 数量 |
| [监听指定目录五秒](../docs/PLATFORM_UTILITIES.zh-CN.md) | [watch_directory.c](watch_directory.c) · `hx_os_watch_directory` | Watching; 变化事件 |
| [借用原生句柄查询文件大小](../docs/NATIVE_INTEROP.zh-CN.md) | [native_file.c](native_file.c) · `hx_os_native_file` | `Native query: ... bytes` |
| [按大端写入并读回数字](../docs/STREAMS.zh-CN.md) | [binary_stream.c](binary_stream.c) · `hx_os_binary_stream` | `device=42 temperature=23.5` |
| [给 JSON 加长度帧再读取](../docs/FRAMES.zh-CN.md) | [framed_messages.c](framed_messages.c) · `hx_os_framed_messages` | `{"device":"door-1","request":42}` |
| [后台写出三条 JSONL 记录](../docs/STREAMS.zh-CN.md) | [buffered_file.c](buffered_file.c) · `hx_os_buffered_file` | samples.jsonl; Saved 3 device readings |
| [查询系统版本和进程 CPU 采样](../docs/PLATFORM_UTILITIES.zh-CN.md) | [system_info.c](system_info.c) · `hx_os_system_info` | 系统名称、Process CPU |
| [测量一次短等待的真实耗时](../docs/CONTROL.zh-CN.md) | [high_resolution_time.c](high_resolution_time.c) · `hx_os_high_resolution_time` | 请求等待 20 ms；实际耗时随环境变化 |
| [为一段工作申请防睡眠](../docs/LINUX_DESKTOP.zh-CN.md) | [sleep_inhibit.c](sleep_inhibit.c) · `hx_os_sleep_inhibit` | 申请／释放或明确不支持 |

<a id="sdk"></a>

## 7. 加密、压缩与外部服务

| 什么时候用 | 完整源码与构建目标 | 主要结果 |
| --- | --- | --- |
| [用密码加密并还原文字](../docs/CRYPTO.zh-CN.md) | [encrypt_text.c](encrypt_text.c) · `hx_os_encrypt_text` | `Decrypted: Hello, UTF-8!` |
| [分块计算 SHA-256](../docs/INCREMENTAL_HASH.zh-CN.md) | [incremental_hash.c](incremental_hash.c) · `hx_os_incremental_hash` | abc 的 64 字符十六进制摘要 |
| [压缩字节后还原 JSON](../docs/UTILITY_EXTENSIONS.zh-CN.md) | [compress_memory.c](compress_memory.c) · `hx_os_compress_memory` | 字节数及原 JSON |
| [打包、列出、解包文件或目录](../docs/ARCHIVE.zh-CN.md) | [archive_files.c](archive_files.c) · `hx_os_archive_files` | 归档条目及处理数量 |
| [安全编码 URL 查询参数](../docs/UTILITY_EXTENSIONS.zh-CN.md) | [url_query.c](url_query.c) · `hx_os_url_query` | 百分号编码地址与解码名称 |
| [向自己的 HTTP/HTTPS 接口提交 JSON](../docs/UTILITY_EXTENSIONS.zh-CN.md) | [http_json.c](http_json.c) · `hx_os_http_json` | HTTP 状态及响应 |
| [保存记录，处理成功后确认](../docs/PERSISTENT_QUEUE.zh-CN.md) | [persistent_queue.c](persistent_queue.c) · `hx_os_persistent_queue` | 3 Handle recognition-... lines |
| [向串口设备发一行 JSON 并收回复](../docs/UTILITY_EXTENSIONS.zh-CN.md) | [serial_message.c](serial_message.c) · `hx_os_serial_message` | 设备回复，或明确超时 |
| [父子进程通过共享内存交接 JSON](../docs/UTILITY_EXTENSIONS.zh-CN.md) | [shared_memory.c](shared_memory.c) · `hx_os_shared_memory` | `Child received: ...` |
| [直接调用同一套底层 SDK](../docs/NATIVE_DEPENDENCIES.zh-CN.md) | [native_dependencies.c](native_dependencies.c) · `hx_os_native_dependencies` | SQLite/OpenSSL/curl/Unicode 等结果 |

<a id="network"></a>

## 8. 两端收发 JSON：按协议选完整业务

选择 IP 网卡并发送一个小型组播发现报文时，构建 `hx_os_network_interfaces` 和 `hx_os_multicast_discovery`。[网络选项教程](../docs/NETWORK_OPTIONS.zh-CN.md)给出两个终端的命令、JSON 数据和预期输出，也说明 Keepalive、双栈与广播的配置方法。

这些例子已经包含服务端、客户端和收发线程。先从同机演示开始，再按各专题说明修改地址和消息字段。运行器只启动公开示例、选择端口并检查业务结果，不需要回归测试文件。

| 需求 | 运行器 | 读哪份说明 |
| --- | --- | --- |
| 本机进程交换带设备／请求编号的消息 | run_ipc_business.py | [IPC 业务](../docs/IPC_BUSINESS.zh-CN.md) |
| TCP 订单与异步回执 | run_tcp_business.py | [TCP 业务](../docs/TCP_BUSINESS.zh-CN.md) |
| UDP 测量值，观察缺号／重复／晚到 | run_udp_business.py | [UDP 业务](../docs/UDP_BUSINESS.zh-CN.md) |
| 基于 KCP 的可靠 UDP 传输 | run_kcp_business.py | [KCP 业务](../docs/KCP_BUSINESS.zh-CN.md) |
| 同一 TLS 连接分线程收发 JSON | run_tls_business.py | [TLS 业务及证书准备](../docs/TLS.zh-CN.md) |

运行脚本时回到库源码根目录。例如 `python examples/run_tcp_business.py --bin-dir build/bin/Release`；Linux 用 `python3` 和 `build/bin`。按专题先构建对应两端目标。TLS 还需证书、私钥与受信 CA；HTTP 的 URL 需要换成真实服务，示例地址不会替你提供服务。

**先选对接口层。** 想让库处理设备／请求编号、会话和消息队列时，读 IPC 教程；想接现有 TCP、UDP 或 TLS 协议时，读对应的独立业务示例。不是给任意 socket 加上 JSON 就自动获得 IPC 协议。

| 示例 | 对端需要发送什么 |
| --- | --- |
| IPC 设备控制 | HXIP v1 消息帧，正文为 JSON，见 IPC 协议说明 |
| TCP 订单 | 2 字节大端正文字节数 + UTF-8 JSON |
| UDP 采样 | 一个数据报包含一个 UTF-8 JSON，无长度前缀 |
| KCP 数据块 | KCP 消息模式的一条完整消息包含 JSON，需持续驱动 KCP |
| TLS 温度读数 | TLS 连接内的 4 字节大端正文字节数 + UTF-8 JSON |

运行器中的 Python 只是代替手动开两个终端，不进入业务进程。若提示 `Build the example first`，先构建该教程列出的两个目标；Windows 检查 Debug／Release 子目录是否与构建一致。`Listening:` 只表示监听就绪，看到双方最终的成功信息才表示整套业务完成。需要把 C 示例放进自己的工程时，复制各专题列出的配套头文件，不能只复制 main。

串口示例使用 115200、8 数据位、1 停止位、无校验，发送 `{"request":42}` 加换行，并等待对端的一行回复。必须先连接能理解该协议的设备或虚拟串口对，不能仅凭程序启动就认为设备通信成功。命名共享内存、桌面防睡眠、Windows GPU 查询也分别需要对应平台和权限，见各专题。

<a id="plugin"></a>

## 9. 动态库：公开插件与加载器配套运行

在库源码根目录，只构建这两个目标：

```sh
cmake --build build --config Release \
  --target hx_os_demo_plugin hx_os_load_plugin
```

上面的续行用于 Linux shell；PowerShell 可写成一行或用反引号续行。进入 build 后，Windows 运行：

```powershell
.\bin\Release\hx_os_load_plugin.exe `
    .\bin\Release\hx_os_demo_plugin.dll
```

Linux 运行：

```sh
./bin/hx_os_load_plugin ./bin/libhx_os_demo_plugin.so
```

输出 `Compatible plugin: Example plugin 1.0`。公开的 [demo_plugin.c](demo_plugin.c) 导出 ABI 版本与描述；[load_plugin.c](load_plugin.c) 打开模块、核对版本、绑定函数、调用后卸载。库文件不能当作可执行程序启动；替换为自己的插件时需保留约定的符号和调用约定，不依赖私有测试模块。

## 10. 把例子改成业务代码

先替换输入和值，再替换得到结果后的处理函数。保留创建、错误检查和释放顺序；有后台工作的对象先停止新任务，再等待退出。超时后仍在使用的数据不能先销毁。输出中的时间、UUID、密文、CPU/GPU 信息会随环境变化，应比较含义而不是照抄固定数值。

这里的输出是用法预期，目标平台能否运行仍取决于[平台边界](../docs/PLATFORMS.zh-CN.md)。详细接口契约放在各专题后半部分，遇到容量、并发、取消或卸载问题时再按需要查阅。

## 新增网络 I/O 示例

在同一个已配置目录构建需要的目标；Visual Studio 可执行文件位于 `bin/Release`，单配置构建位于 `bin`。

| 用途 | 源码／目标 | 参数与预期结果 |
| --- | --- | --- |
| 接收 UDP 目标地址和网卡信息 | [udp_packet.c](udp_packet.c) / `hx_os_udp_packet` | 无参数；打印本机 JSON 报文、地址和网卡编号 |
| 使用可取消 DNS 查询主机 | [dns_lookup.c](dns_lookup.c) / `hx_os_dns_lookup` | `[--async|--coro] HOST [DNS_IP:PORT]`；打印数字地址 |
| 可信局域网 KCP 请求／应答 | [kcp_session.c](kcp_session.c) / `hx_os_kcp_session` | 先运行 `server IP:PORT`，再运行 `client IP:PORT`；打印 JSON 请求和回复 |
| HTTP 流式传输文件 | [http_file.c](http_file.c) / `hx_os_http_file` | `download URL FILE [CA]` 或 `upload URL FILE [CA]`；打印 HTTP 状态 |
| C99 协程等待 UDP | [coroutine_udp.c](coroutine_udp.c) / `hx_os_coroutine_udp` | 无参数；打印发给自己的 JSON 报文 |
| C99 协程等待 HTTP | [coroutine_http.c](coroutine_http.c) / `hx_os_coroutine_http` | `URL`；打印 HTTP 状态和响应正文 |

完整命令和资源持有规则见[网络 I/O](../docs/NETWORK_IO.zh-CN.md)、[HTTP 文件传输](../docs/HTTP_FILES.zh-CN.md)及[协程等待](../docs/COROUTINES.zh-CN.md)。DNS 需要可达的已配置服务器，HTTP 需要你的服务 URL。示例不会修改防火墙或部署外部服务。

`hx_os_dns_lookup --async 127.0.0.1` 和 `--coro 127.0.0.1` 演示事件循环／协程 DNS，两者打印同样的地址结果。HTTP 文件与协程示例可追加 `--cancellable-dns`；这需要显式选择 c-ares 版 SDK，默认系统 DNS SDK 会明确报不支持。完整命令、成功输出与失败处理见[网络选项](../docs/NETWORK_OPTIONS.zh-CN.md#123-事件循环和协程里怎么用)和 [HTTP 传输](../docs/HTTP_FILES.zh-CN.md#4-连-dns-无响应也需要取消时)。
