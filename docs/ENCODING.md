# Base64 and Hex Encoding

[简体中文](ENCODING.zh-CN.md)

## 1. Encode bytes without OpenSSL

Enable `HX_OS_ENABLE_ENCODING` for Base64, Base64URL and hexadecimal conversion. These are representations of bytes, not encryption. No OpenSSL, network initialization or container module is required. Disabled builds retain the functions and return `HX_OS_NOT_SUPPORTED`.

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

Output: `aGVsbG8=`. Use `hx_os_hex_encode_string` instead for `68656c6c6f`. The [encoding example](../examples/encoding.c) round-trips bytes containing NUL and values above 127. Build target `hx_os_encoding` in your existing examples-enabled directory.

## 2. Choose the exact Base64 representation

| Flags | Alphabet | Padding |
|---|---|---|
| `0` | Standard `A-Z a-z 0-9 + /` | Required when the final group needs it. |
| `HX_OS_BASE64_URL` | URL-safe, replacing `+ /` with `- _` | Required when needed. |
| `HX_OS_BASE64_NO_PADDING` | Standard | Omitted on encode, rejected on decode. |
| Both flags | URL-safe | Omitted on encode, rejected on decode. |

Decoding rejects spaces, newlines, mixed alphabets, extra or missing padding, impossible lengths and nonzero unused tail bits. For example, padded `Zg==` and unpadded `Zg` represent `f` under their respective modes; `Zh==` is rejected because its unused bits are nonzero. Inputs are explicit byte ranges: do not include the C string terminator.

Hexadecimal output is lowercase by default; `HX_OS_HEX_UPPERCASE` selects uppercase. Decode accepts either case and rejects odd-length input, `0x` prefixes, whitespace and separators.

## 3. Buffer and ownership rules

All four buffer functions first validate the entire input and calculate output size. Every failure preserves the output buffer. Valid input sets `required` on `HX_OS_OK` or `HX_OS_OUTPUT_FULL`; malformed input leaves it unchanged.

- Encoding `required` includes the terminating NUL.
- Decoding `required` is the exact number of binary bytes, without a terminator.
- `output=NULL, capacity=0` queries the required size. Encoding an empty range still requires one NUL byte; decoding an empty range succeeds with zero bytes.
- Input, output and the `required` object must be separate storage. In-place conversion is not provided.
- Buffer functions do not allocate. Allocate the reported decode size with `hx_os_memory_alloc`, decode, then release with `hx_os_memory_free`, or supply an existing array.
- String convenience functions use the output string's context, accept input borrowed from that string, and preserve it on failure. They limit encoded payload to 16 MiB. Use the buffer functions for a larger caller-managed result.

## 4. C++11 results

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

`Encoding::hex` returns `Result<String>`; `fromHex` and `fromBase64` return `Result<Buffer>`. `Buffer` owns arbitrary bytes and releases them automatically. C++ owning results use a 16 MiB result limit, require no exceptions, and retain the underlying status/native-error mechanism. Check success before accessing `.value()`.
