# Base64 and Hex Encoding

[简体中文](ENCODING.zh-CN.md)

When embedding raw binary data—including `NUL` (`\0`) bytes or non-ASCII bytes—inside JSON strings, configuration files, URL parameters, or text-based protocols, treating the payload as a NUL-terminated C string will truncate data or fail UTF-8 validation. This module provides dependency-free Base64, Base64URL, and hexadecimal encoding and decoding in pure C99 and C++11.

To see a complete round-trip in action, open [encoding.c](../examples/encoding.c). The program encodes the four-byte sequence `00 01 fe ff` into an `hx_string` with `hx_os_base64_encode_string()`, decodes it back into a caller-supplied byte array with `hx_os_base64_decode()`, and inspects the decoded byte count. Follow the [example run instructions](../examples/README.md#run) to enable examples and build `hx_os_encoding` (no command-line arguments or input files are required):

```text
Base64: AAH+/w==
Decoded 4 bytes; last byte: 255
```

You can also drop the example source into the [minimal project](USAGE.md#c-project) as `main.c`.

> [!NOTE]
> Base64 and hexadecimal are **reversible byte representations**, not encryption—anyone can decode them. When your data requires confidentiality or tamper protection, pair this module with the cryptography interfaces.

## 1. Encode bytes without OpenSSL

Enable the CMake option `HX_OS_ENABLE_ENCODING` (enabled by default) to use Base64, Base64URL, and hexadecimal conversions. The implementation is self-contained C99: it requires no OpenSSL, no network initialization, and no container module. When built with `HX_OS_ENABLE_ENCODING=OFF`, the entry points remain present and return `HX_OS_NOT_SUPPORTED`.

```c
#include <hx_encoding.h>
#include <stdio.h>

int main(void)
{
	hx_string encoded = HX_STRING_INIT;
	hx_os_status status =
		hx_os_base64_encode_string("hello", 5, 0, &encoded, NULL);

	if (status == HX_OS_OK)
		puts(hx_string_c_str(&encoded));

	hx_string_destroy(&encoded);

	return status == HX_OS_OK ? 0 : 1;
}
```

This program prints `aGVsbG8=`. Calling `hx_os_hex_encode_string()` instead produces `68656c6c6f`. See the [encoding example](../examples/encoding.c) for a complete round-trip with embedded `NUL` bytes and byte values above `127`.

## 2. Choose the exact Base64 representation

Use the `flags` parameter to select the exact Base64 alphabet and `=` padding behavior required by your protocol:

| `flags` value | Alphabet | `=` Padding rule |
|---|---|---|
| `0` | Standard `A-Z a-z 0-9 + /` | Emitted when the final group needs padding; required on decode. |
| `HX_OS_BASE64_URL` | URL-safe (`-` and `_` instead of `+` and `/`) | Emitted when needed; required on decode. |
| `HX_OS_BASE64_NO_PADDING` | Standard `A-Z a-z 0-9 + /` | Omitted on encode; rejected on decode. |
| `HX_OS_BASE64_URL \| HX_OS_BASE64_NO_PADDING` | URL-safe | Omitted on encode; rejected on decode. |

> [!IMPORTANT]
> **Strict Validation Rules:**
> - **Base64 decoding**: Rejects whitespace, newlines, mixed standard/URL alphabets, missing or superfluous `=` padding, impossible remainder lengths, and non-canonical encodings where unused trailing bits are non-zero. For example, `'f'` encodes as `Zg==` (padded) or `Zg` (unpadded), whereas `Zh==` is rejected with `HX_OS_INVALID` because its unused tail bits are non-zero.
> - **Hexadecimal conversion**: Hex encoding outputs lowercase `0-9a-f` by default, or uppercase `0-9A-F` when `HX_OS_HEX_UPPERCASE` is passed. Hex decoding accepts both upper- and lowercase characters, but rejects odd character counts, `0x` prefixes, whitespace, and separators.
> - **Explicit byte lengths**: All APIs take an explicit byte length. Do not include a C string's trailing `\0` terminator in the input length unless you intentionally want that `NUL` byte encoded.

## 3. Buffer and ownership rules

The four buffer-level functions (`hx_os_base64_encode`, `hx_os_base64_decode`, `hx_os_hex_encode`, and `hx_os_hex_decode`) validate the entire input and compute the exact output size before modifying the destination. Any failure leaves the output buffer untouched:

- **Reporting `required` size**: When the input is valid and the function returns `HX_OS_OK` or `HX_OS_OUTPUT_FULL`, `*required` is updated with the required output capacity. If the input itself is malformed (for example, returning `HX_OS_INVALID`), `*required` is left unchanged.
- **Terminators on encode vs. decode**:
  - **Encoding**: `required` **includes** the trailing `\0` terminator written at the end of the encoded ASCII text.
  - **Decoding**: `required` is the exact count of decoded binary bytes, **without** any trailing terminator.
- **Querying output size**: Pass `output = NULL` and `capacity = 0` to query the required capacity before allocating. Note that encoding an empty input (`length = 0`) still requires `1` byte for the trailing `\0`, whereas decoding an empty input succeeds immediately with `0` required bytes.
- **Non-overlapping storage**: Input, output, and the `required` size variable must occupy separate, non-overlapping memory regions; in-place conversion is not supported.
- **Allocation & payload limits**:
  - Buffer functions **never allocate heap memory**. You can decode into a fixed array or allocate the queried size with `hx_os_memory_alloc()` and free it later with `hx_os_memory_free()`.
  - String convenience functions (`*_encode_string`) allocate through the destination `hx_string`'s bound context, allow the input slice to borrow from that same `hx_string`, and preserve the original string contents on failure. They enforce a **16 MiB** encoded payload ceiling; use the buffer functions when working with larger caller-managed buffers.

## 4. C++11 results

In C++11, include `<hx/Encoding.hpp>` to encode into `hx::Result<hx::String>` or decode into `hx::Result<hx::Buffer>`:

```cpp
#include <hx/Encoding.hpp>

int main()
{
	auto encoded = hx::Encoding::base64("hello", 5);
	if (!encoded)
		return 1;

	auto bytes = hx::Encoding::fromBase64("aGVsbG8=");
	if (!bytes)
		return 1;

	return bytes.value().size() == 5 ? 0 : 1;
}
```

- `hx::Encoding::base64()` and `hx::Encoding::hex()` return `hx::Result<hx::String>`.
- `hx::Encoding::fromBase64()` and `hx::Encoding::fromHex()` return `hx::Result<hx::Buffer>`, which owns the decoded byte buffer and frees it automatically on destruction.
- C++11 owning helpers enforce the same **16 MiB** result limit, use no C++ exceptions, and preserve full status and native error details. Always verify `if (result)` before calling `.value()`.
