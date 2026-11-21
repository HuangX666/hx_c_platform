# Varints, ZigZag and UUIDs

[简体中文](INTEGER_TOOLS.zh-CN.md) · [README](../README.md)

When a binary protocol or storage format needs compact variable-length integers (unsigned Varints and signed ZigZag encoding), or when database and log records need timestamp-prefixed unique identifiers (UUID v7) alongside strict UUID parsing and formatting, use `<hx_os_varint.h>` and `<hx_os_random.h>` (both included by `<hx_os.h>`).

To see a complete working example, inspect [integer_tools.c](../examples/integer_tools.c): the program encodes `300` into 2 bytes with `hx_os_varint_encode`, decodes it back with `hx_os_varint_decode`, maps `-1` to unsigned `1` via `hx_os_zigzag_encode`, and generates a textual UUID v7 with `hx_os_uuid_v7_generate_string`.

Follow the [example run instructions](../examples/README.md#run) to configure with `-DHX_OS_BUILD_EXAMPLES=ON` and build `hx_os_integer_tools` (no arguments or input files are required). The first two lines of output are deterministic, while the final UUID v7 line changes on every run:

```text
300 uses 2 bytes; decoded: 300
ZigZag -1: 1
018f... (UUID v7)
```

You can also use the example source as `main.c` in the [minimal project](USAGE.md#c-project).

## 1. Compact integers

Include `<hx_os_varint.h>` (or `<hx_os.h>`) to encode and decode integers in memory buffers without global initialization or external dependencies:
- **Unsigned Varints**: Store `uint64_t` values in `1..10` bytes using the shortest canonical base-128 representation (`300` encodes as the 2-byte sequence `0xac 0x02`).
- **Signed ZigZag mapping**: Because negative two's-complement numbers have leading `1` bits that would always expand to a full 10-byte unsigned Varint, `hx_os_zigzag_encode` and `hx_os_zigzag_decode` map signed integers of small magnitude to small unsigned integers (`0 → 0`, `-1 → 1`, `1 → 2`, `-2 → 3`) before Varint encoding.

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
> **Buffer encoding and decoding contracts:**
> - **Encoding (`hx_os_varint_encode`)**: If the destination buffer is too small, the call returns `HX_OS_OUTPUT_FULL`, writes the required byte count to `*written`, and leaves the destination buffer untouched.
> - **Decoding (`hx_os_varint_decode`)**: On `HX_OS_OK`, `*consumed` reports the exact number of bytes read for this integer, leaving any subsequent bytes in the buffer unread for the next field. Incomplete input returns `HX_OS_TRUNCATED_INPUT`, values exceeding `UINT64_MAX` return `HX_OS_RANGE`, and non-minimal encodings (redundant trailing zero groups) return `HX_OS_SYNTAX`. On any failure, `*value` is unchanged and `*consumed` is set to `0`. Output pointers must not overlap the byte buffer or each other.

When `HX_OS_ENABLE_STREAM=ON`, `hx_os_stream` (and C++ `hx::Stream`) also provides `hx_os_stream_read_varuint` / `write_varuint` and ZigZag-encoded signed `hx_os_stream_read_varint` / `write_varint`:

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

If a stream Varint read encounters mid-field `EOF` or invalid bytes, the caller's output variable is preserved and `HX_OS_TRUNCATED_INPUT` (or `HX_OS_RANGE` / `HX_OS_SYNTAX`) is returned, but the stream position may have advanced past the bytes already read—reposition or recover the stream before retrying.

## 2. Parse or create a UUID

- **Canonical parsing and formatting (`hx_os_uuid_parse` / `hx_os_uuid_format`)**:
  - `hx_os_uuid_parse` accepts an exact 36-byte `8-4-4-4-12` hexadecimal string (uppercase or lowercase, any UUID version/variant, and the all-zero nil UUID) and rejects braces, `urn:uuid:` prefixes, or surrounding whitespace. Failed parses leave the caller's 16-byte output array unchanged.
  - `hx_os_uuid_format` writes canonical lowercase 36-character text plus a trailing `NUL` into a 37-byte destination buffer.
- **Generating timestamp-prefixed UUID v7**:
  - `hx_os_uuid_v7_generate_string(&id, &error)` writes a newly generated UUID v7 string directly into an initialized `hx_string`.
  - For 16-byte binary IDs, call `hx_os_uuid_v7_generate` (which samples the current Unix wall clock) or `hx_os_uuid_v7_generate_at` (which accepts an explicit 48-bit Unix millisecond timestamp for deterministic tests or custom clocks).
  - `hx_os_uuid_v7_time` extracts the 48-bit Unix millisecond timestamp from a binary UUID v7 and rejects other UUID versions or variants.

```c
hx_string id = HX_STRING_INIT;

hx_os_status status = hx_os_uuid_v7_generate_string(&id, NULL);
if (status == HX_OS_OK)
	puts(hx_string_c_str(&id));

hx_string_destroy(&id);
```

> [!NOTE]
> UUID v7 generation requires `HX_OS_ENABLE_RANDOM=ON` and fills its random bits from the OS cryptographic random source. Because the generator is stateless, multiple IDs generated within the same millisecond are not monotonically ordered, and wall clocks can step backward; do not treat UUID v7 alone as a strict global sequence number across machines. Existing UUID v4 APIs remain unchanged. In C++11, `<hx/Random.hpp>` provides `hx::Random::uuidV7()`, `parseUuid()`, and `uuidV7Time()` returning `hx::Result`.

## 3. Run the example

In an examples-enabled build directory (`-DHX_OS_BUILD_EXAMPLES=ON`), build and run `hx_os_integer_tools` ([integer_tools.c](../examples/integer_tools.c)):

```sh
cmake --build build --config Release --target hx_os_integer_tools
# Linux / macOS:
./build/bin/hx_os_integer_tools
# Windows / Visual Studio:
./build/bin/Release/hx_os_integer_tools.exe
```

The first two lines print `300 uses 2 bytes; decoded: 300` and `ZigZag -1: 1`, followed by a freshly generated UUID v7 string.
