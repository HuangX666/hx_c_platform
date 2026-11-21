# Varint、ZigZag 与 UUID

[English](INTEGER_TOOLS.md) · [首页](../README.zh-CN.md)

当二进制协议或持久化格式需要将小整数压缩为更少的字节（Varint 与有符号 ZigZag 编码），或者业务记录需要生成带有毫秒时间戳前缀的有序唯一标识符（UUID v7）与规范化解析时，可以使用 `<hx_os_varint.h>` 与 `<hx_os_random.h>`（均已包含在总头文件 `<hx_os.h>` 中）。

完整可运行的示例位于 [integer_tools.c](../examples/integer_tools.c)：程序先调用 `hx_os_varint_encode` 将整数 `300` 编码为 2 个字节并用 `hx_os_varint_decode` 解回，再用 `hx_os_zigzag_encode` 将有符号负数 `-1` 映射为无符号小整数 `1`，最后调用 `hx_os_uuid_v7_generate_string` 生成并打印一个带当前时间戳的 UUID v7 字符串。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 `-DHX_OS_BUILD_EXAMPLES=ON` 并只构建 `hx_os_integer_tools` 目标即可直接运行（无需命令行参数或输入文件）。运行后前两行固定输出如下（第三行的 UUID 每次运行都会变化）：

```text
300 uses 2 bytes; decoded: 300
ZigZag -1: 1
018f... (UUID v7)
```

你也可以参照[最小工程配置](USAGE.zh-CN.md#c-project)将该示例源码作为自己工程的 `main.c`。

## 1. 紧凑整数编码

包含 `<hx_os_varint.h>`（或 `<hx_os.h>`）即可使用内存缓冲区上的 Varint 与 ZigZag 工具，无需全局初始化或可选第三方依赖：
- **Varint（无符号变长编码）**：采用最短的 Base-128 表示，将 `uint64_t` 编码为 `1..10` 个字节（例如 `300` 编码为 2 字节 `0xac 0x02`）。
- **ZigZag（有符号映射）**：由于普通补码负数的高位全为 `1`，直接按无符号 Varint 编码会固定占满 10 字节；`hx_os_zigzag_encode` / `hx_os_zigzag_decode` 将小绝对值的有符号整数交错映射为无符号小整数（`0 → 0`、`-1 → 1`、`1 → 2`、`-2 → 3`），从而让正负小整数都能紧凑编码。

```c
unsigned char data[10];
size_t written = 0, consumed = 0;
uint64_t value = 0;

hx_os_status status =
	hx_os_varint_encode(300, data, sizeof(data), &written, NULL);
if (status == HX_OS_OK)
	status = hx_os_varint_decode(data, written, &value, &consumed, NULL);
```

> [!IMPORTANT]
> **编解码状态码与流式边界：**
> - **缓冲区编码（`hx_os_varint_encode`）**：若目标缓冲区容量不足，函数返回 `HX_OS_OUTPUT_FULL`，通过 `*written` 告知实际需要的字节数，且保持目标缓冲区内容不变。
> - **缓冲区解码（`hx_os_varint_decode`）**：解码成功后通过 `*consumed` 返回本次整数实际占用的字节数（紧随其后的后续字段字节不会被消费）。若输入在整数结束前截断则返回 `HX_OS_TRUNCATED_INPUT`；若数值超出 `uint64_t` 范围返回 `HX_OS_RANGE`；若存在冗余的前导高零字节（非最短规范表示）则返回 `HX_OS_SYNTAX`。解码失败时 `*value` 保持原值且 `*consumed` 置为 `0`。输出指针与输入数据区不得重叠。

此外，当开启 `HX_OS_ENABLE_STREAM=ON` 时，读写流（`hx_os_stream` / C++ `hx::Stream`）还直接提供流式变长整数接口 `hx_os_stream_read_varuint` / `write_varuint` 以及内置 ZigZag 转换的有符号版本 `hx_os_stream_read_varint` / `write_varint`：

```cpp
auto stream = hx::Stream::memory();
if (!stream)
	return 1;

if (!stream.value().writeVarInt(-42) || !stream.value().seek(0))
	return 1;

auto number = stream.value().readVarInt();
if (!number)
	return 1;
```

注意在流上调用 `read_varuint` / `read_varint` 若中途遇到 `EOF` 或非法字节，函数会保留调用方输出变量不变并返回 `HX_OS_TRUNCATED_INPUT` 等错误，但此时底层流可能已经消费了该字段的前面几个字节——重试前需先定位或恢复流偏移，不能直接在原位置盲目重读。

## 2. 解析或创建 UUID

- **规范化解析与格式化（`hx_os_uuid_parse` / `hx_os_uuid_format`）**：
  - `hx_os_uuid_parse` 严格接受长度为 36 字节的 `8-4-4-4-12` 十六进制文本（大小写字母均可，支持任意 UUID 版本、变体及全零 Nil UUID），拒绝带花括号 `{...}`、`urn:uuid:` 前缀或首尾空白字符的输入；解析失败时保持调用方的 16 字节输出数组不变。
  - `hx_os_uuid_format` 将 16 字节二进制 UUID 格式化为规范的小写 36 字符文本加结尾 `NUL`（目标缓冲区至少需 37 字节）。
- **生成带时间戳的 UUID v7**：
  - `hx_os_uuid_v7_generate_string(&id, &error)` 直接将新生成的 UUID v7 文本写入已初始化的 `hx_string`。
  - 如需 16 字节二进制表示，可调用 `hx_os_uuid_v7_generate`（使用当前系统墙上时钟）或 `hx_os_uuid_v7_generate_at`（显式传入 48 位 Unix 毫秒时间戳，便于测试或受控时钟环境）。
  - `hx_os_uuid_v7_time` 可从二进制 UUID v7 中提取出 48 位 Unix 毫秒时间戳；若传入的 UUID 不是版本 7 或不符合 RFC 变体标记，会明确返回错误。

```c
hx_string id = HX_STRING_INIT;

hx_os_status status = hx_os_uuid_v7_generate_string(&id, NULL);
if (status == HX_OS_OK)
	puts(hx_string_c_str(&id));

hx_string_destroy(&id);
```

> [!NOTE]
> 生成 UUID v7 需要开启 `HX_OS_ENABLE_RANDOM=ON`，底层复用操作系统的密码学安全随机源填充随机位。生成器本身是无状态的：同一毫秒内多次生成的 UUID v7 随机位相互独立（不维护全局单调递增计数器），且系统墙上时钟可能因 NTP 校时发生回拨，因此不要将其作为跨机器严格全序的事务序列号。原有的随机 UUID v4 接口行为保持不变。在 C++11 中可使用 `<hx/Random.hpp>` 提供的 `hx::Random::uuidV7()`、`parseUuid()` 和 `uuidV7Time()`。

## 3. 运行示例

在已开启 `HX_OS_BUILD_EXAMPLES=ON` 的构建目录中构建并运行 `hx_os_integer_tools`（源码见 [integer_tools.c](../examples/integer_tools.c)）：

```sh
cmake --build build --config Release --target hx_os_integer_tools
# Linux / macOS：
./build/bin/hx_os_integer_tools
# Windows / Visual Studio：
./build/bin/Release/hx_os_integer_tools.exe
```

前两行固定输出 `300 uses 2 bytes; decoded: 300` 和 `ZigZag -1: 1`，第三行输出每次新生成的 UUID v7 字符串。
