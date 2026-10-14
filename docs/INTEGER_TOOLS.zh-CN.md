# Varint、ZigZag 与 UUID

[English](INTEGER_TOOLS.md) · [首页](../README.zh-CN.md)

## 1. 紧凑整数编码

包含 `hx_os.h` 或 `hx_os_varint.h` 即可使用。缓冲区接口无需初始化或可选依赖。Varint 使用最短的 base-128 表示，把无符号 64 位整数编码为 1—10 字节。例如 `300` 编码为 `ac 02`。ZigZag 将有符号值映射为无符号值：`0 → 0`、`-1 → 1`、`1 → 2`、`-2 → 3`。这是整数编码工具，不是完整的 Protocol Buffers 实现。

```c
unsigned char data[10];
size_t written = 0, consumed = 0;
uint64_t value = 0;

hx_os_status status =
	hx_os_varint_encode(300, data, sizeof(data), &written, NULL);
if (status == HX_OS_OK)
	status = hx_os_varint_decode(data, written, &value, &consumed, NULL);
```

检查 `status` 成功后再使用 `value`。编码容量不足时返回 `OUTPUT_FULL`，通过 written 返回所需大小，缓冲区不变。解码失败保留原 value，consumed 为零。输入不完整返回 `TRUNCATED_INPUT`，溢出返回 `RANGE`，不是最短表示返回 `SYNTAX`。一个完整整数之后的字节不会被消费。输出字段不能与字节区域或彼此重叠。

已有读写流新增 `hx_os_stream_read_varuint/write_varuint`，以及使用 ZigZag 的有符号 `read_varint/write_varint`，需要开启 STREAM。读取失败保留原数值，但可能已经消费部分字段；字段内 EOF 返回截断错误。写入报告实际完成字节数。部分 I/O 后不能直接重试整个字段，需要先处理流的当前位置。

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

## 2. 解析或创建 UUID

`hx_os_uuid_parse` 接受恰好 36 字节的 `8-4-4-4-12` 文本，十六进制大小写均可。接受所有版本／variant 和全零 UUID；拒绝花括号、前缀和空白。失败保留原有 16 字节输出。`hx_os_uuid_format` 写入小写规范文本，需要 37 字节，包含 NUL。

`hx_os_uuid_v7_generate_string(&id, &error)` 直接写入已初始化的 `hx_string`。二进制 ID 使用 `hx_os_uuid_v7_generate`；`generate_at` 接受显式 Unix 毫秒时间，方便受控时钟。`hx_os_uuid_v7_time` 提取时间，遇到其他版本或 variant 会明确失败。

```c
hx_string id = HX_STRING_INIT;

hx_os_status status = hx_os_uuid_v7_generate_string(&id, NULL);
if (status == HX_OS_OK)
	puts(hx_string_c_str(&id));

hx_string_destroy(&id);
```

UUID v7 需要 RANDOM，复用已有的操作系统安全随机源。时间字段限制为 48 位。生成器没有全局状态：同一毫秒内不保证生成顺序，墙上时钟可能回拨，唯一性属于概率保证，不能用它确定跨设备的严格业务顺序。已有 UUID v4 行为保持原契约。C++11 提供 `hx::Random::uuidV7`、`parseUuid`、`uuidV7Time`，使用 Result 返回结果。

## 3. 运行示例

复用已开启 `HX_OS_BUILD_EXAMPLES=ON` 的构建目录，构建 `--target hx_os_integer_tools`，运行 `bin/hx_os_integer_tools`；Visual Studio Debug 对应 `bin/Debug/hx_os_integer_tools.exe`。前两行输出 `300 uses 2 bytes; decoded: 300` 和 `ZigZag -1: 1`，最后的 UUID 每次不同。源码：[integer_tools.c](../examples/integer_tools.c)。
