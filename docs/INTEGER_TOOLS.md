# Varints, ZigZag and UUIDs

[简体中文](INTEGER_TOOLS.zh-CN.md) · [README](../README.md)

**Start with a complete program.** Use Varint/ZigZag for compact protocol integers and UUID v7 when a record ID should carry a timestamp.

Open [integer_tools.c](../examples/integer_tools.c). varint_encode 300 → decode → zigzag_encode -1 → uuid_v7_generate_string → destroy the string.

Follow the [example run instructions](../examples/README.md#run), enable examples and build only `hx_os_integer_tools`. This program needs no arguments or input files.

**Expected result:** The first lines are `300 uses 2 bytes; decoded: 300` and `ZigZag -1: 1`; the UUID varies. Respect the consumed-byte count when decoding multiple fields.

Short snippets below explain individual calls; use the complete example when running them. You can also use its source as main in the [minimal project](USAGE.md#c-project).

## 1. Compact integers

Include `hx_os.h` or `hx_os_varint.h`. Buffer operations need no initialization or optional dependency. Varints store unsigned 64-bit values in 1–10 bytes, using the shortest base-128 representation. `300` encodes as `ac 02`. ZigZag maps signed values to unsigned values: `0 → 0`, `-1 → 1`, `1 → 2`, and `-2 → 3`. This is an integer encoding utility, not a complete Protocol Buffers implementation.

```c
unsigned char data[10];
size_t written = 0, consumed = 0;
uint64_t value = 0;

hx_os_status status =
	hx_os_varint_encode(300, data, sizeof(data), &written, NULL);
if (status == HX_OS_OK)
	status = hx_os_varint_decode(data, written, &value, &consumed, NULL);
```

Check `status` before using `value`. Encoding reports the required size on `OUTPUT_FULL` without modifying the buffer. Decoding leaves the value unchanged and sets consumed to zero on failure. Truncated input is `TRUNCATED_INPUT`, overflow is `RANGE`, and a nonminimal representation is `SYNTAX`. Bytes after a complete integer are left unread. Output fields must not overlap byte spans or each other.

The existing stream also provides `hx_os_stream_read_varuint/write_varuint` and signed `read_varint/write_varint` using ZigZag. These require STREAM. Reads preserve the value on failure but can consume a partial field; EOF inside a field is truncated input. Writes report the actual byte prefix. Do not retry an entire field after partial I/O without repairing stream state.

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

## 2. Parse or create a UUID

`hx_os_uuid_parse` accepts exactly 36 bytes in the `8-4-4-4-12` form, with either hexadecimal case. It accepts all versions/variants and nil UUIDs; braces, prefixes and whitespace are rejected. Failure preserves the 16-byte output. `hx_os_uuid_format` writes canonical lowercase text to 37 bytes, including NUL.

`hx_os_uuid_v7_generate_string(&id, &error)` generates directly into an initialized `hx_string`. For binary IDs use `hx_os_uuid_v7_generate`; `generate_at` accepts an explicit Unix timestamp in milliseconds for controlled clocks. `hx_os_uuid_v7_time` extracts that timestamp and rejects other versions or variants.

```c
hx_string id = HX_STRING_INIT;

hx_os_status status = hx_os_uuid_v7_generate_string(&id, NULL);
if (status == HX_OS_OK)
	puts(hx_string_c_str(&id));

hx_string_destroy(&id);
```

UUID v7 requires RANDOM and uses the existing OS cryptographic random source. Its timestamp is limited to 48 bits. The generator is stateless: values within a millisecond are not generation-ordered, wall clocks can move backwards, and uniqueness is probabilistic. It does not establish global business-event ordering. Existing UUID v4 behavior is unchanged. C++11 offers `hx::Random::uuidV7`, `parseUuid` and `uuidV7Time` with Result values.

## 3. Run the example

In an existing build directory configured with `HX_OS_BUILD_EXAMPLES=ON`, build `--target hx_os_integer_tools`, then run `bin/hx_os_integer_tools` (`bin/Debug/hx_os_integer_tools.exe` for a Debug Visual Studio build). The first two lines show `300 uses 2 bytes; decoded: 300` and `ZigZag -1: 1`; the final UUID varies each run. Source: [integer_tools.c](../examples/integer_tools.c).
