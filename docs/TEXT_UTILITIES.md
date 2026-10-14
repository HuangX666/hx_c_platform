# Borrowed text utilities

[简体中文](TEXT_UTILITIES.zh-CN.md) · [String tools](COMMON_UTILITIES.md) · [README](../README.md)

## 1. Split without copying the text

`<hx_text_utils.h>` provides a stack cursor that returns `hx_string_view` pieces of an existing input. It allocates no memory and does not modify the input. Use the existing `hx_string_split()` / `hx::Strings::split()` when the result should own its input copy instead.

```c
hx_os_text_split split;
hx_string_view part;
hx_os_status status = hx_os_text_split_init(&split,
	hx_string_view_from_cstr("alpha::beta::::gamma::"),
	hx_string_view_from_cstr("::"), 1, NULL);

if (status == HX_OS_OK) {
	while ((status = hx_os_text_split_next(&split, &part, NULL)) ==
		HX_OS_OK) {
		if (part.size)
			(void)fwrite(part.data, 1, part.size, stdout);
		putchar('\n');
	}
}

if (status != HX_OS_EOF)
	fprintf(stderr, "Split failed: %s\n", hx_os_status_name(status));
```

The five pieces are `alpha`, `beta`, an empty piece, `gamma`, and a trailing empty piece. The delimiter is the entire exact byte sequence `::`, not a set of individual separator characters. Matches proceed left to right without overlap: splitting `aaa` by `aa` yields an empty piece and `a`.

| Case | `keep_empty=1` | `keep_empty=0` |
| --- | --- | --- |
| Empty input | One empty piece | No pieces |
| Consecutive delimiters | Retains empty pieces | Skips empty pieces |
| Trailing delimiter | Retains a final empty piece | Skips it |
| Empty delimiter | `HX_OS_INVALID` | `HX_OS_INVALID` |

Initialization validates both complete bounded views as UTF-8 once; invalid encoding fails without changing the cursor. Subsequent `next()` calls do not rescan the whole input for validation. Multibyte UTF-8 delimiters and embedded NUL are supported. Provide explicit lengths when the data contains NUL. The delimiter matcher uses constant auxiliary memory and scans for exact matches; it is not a regular expression or token parser.

`next()` returns `HX_OS_OK` for a piece, including an empty one, and `HX_OS_EOF` after exhaustion. EOF/failure leaves the output unchanged. No fixed maximum number of pieces is imposed; the caller controls how many times it advances within the bounded input. Initialization and iteration are synchronous and can take time proportional to the data scanned.

Both input and delimiter must stay alive and unchanged during iteration. Returned pieces borrow the original input and need not be NUL-terminated. Copying the cursor copies only its position; the new cursor still borrows the same text. Keep cursor/output objects outside the borrowed data, and keep the output view outside the cursor. Serialize access to one cursor; independent cursor copies can read shared immutable text concurrently.

## 2. Strict integer conversion

`hx_os_text_parse_i64()` and `hx_os_text_parse_u64()` parse a complete bounded token without allocation or locale changes. Pass an explicit base of `2`, `8`, `10` or `16`. Leading zeros do not choose a different base.

```c
uint64_t port = 0;
hx_os_status status = hx_os_text_parse_u64(
	hx_string_view_from_cstr("443"), 10, &port, NULL);

if (status != HX_OS_OK)
	fprintf(stderr, "Invalid port: %s\n", hx_os_status_name(status));
```

| Input | Interpretation |
| --- | --- |
| `+42` in base 10 | Accepted by signed and unsigned parsing |
| `-42` in base 10 | Accepted only by signed parsing |
| `-0` for unsigned | Rejected |
| `ff` / `FF` in base 16 | Both produce 255 |
| `101` in base 2; `377` in base 8 | Produce 5 and 255 |
| `0xff`, surrounding whitespace, `1_000`, `1.0`, `1e2`, `42ms` | Rejected; no prefixes, separators, fractions or suffixes |
| Empty input, a sign by itself, an included trailing NUL | Rejected |

Successful conversion writes the entire value. Every failure preserves the old output value. Invalid arguments or unsupported bases return `HX_OS_INVALID`; malformed tokens return `HX_OS_TYPE_MISMATCH`; numeric overflow returns `HX_OS_RANGE`. Syntax is checked throughout the token, even after a magnitude overflow is detected. The full `INT64_MIN`…`INT64_MAX` and `0`…`UINT64_MAX` ranges are handled using integer arithmetic, without rounding through floating point.

These two functions follow `HX_OS_ENABLE_TEXT_NUMBER`. Disabled builds return `HX_OS_NOT_SUPPORTED`; check the existing `HX_OS_CAP_TEXT_NUMBER` capability when needed. Existing floating-point conversion retains its own configuration-oriented grammar; this strict integer parser does not change it.

## 3. Natural ordering for names

`hx_os_text_compare_natural(left, right)` returns `-1`, `0` or `1` and can be used by sorting callbacks. It compares simultaneous ASCII digit runs by numeric magnitude without converting them into a machine integer. Arbitrarily long digit runs therefore do not overflow.

```text
file0 < file00 < file2 < file2a < file02 < file002 < file10
```

When digit runs have the same numeric value, the shorter complete digit run sorts first. That tie-break is applied before the following text; it makes leading-zero handling deterministic. Equal comparison means equal complete byte sequences, not merely equal numeric values.

Other bytes compare as unsigned bytes. Valid UTF-8 text is supported without locale collation, normalization or Unicode case folding; Chinese names such as `设备2` and `设备10` share their byte prefix and then compare by the ASCII digit run. Non-ASCII numerals, signs and decimal separators have no numeric meaning. The comparator does not validate encoding. Inputs must be valid byte ranges, with NULL allowed only for zero length. It allocates no memory and does not change process state.

## 4. C++11 and the runnable example

`<hx/TextUtilities.hpp>` provides:

- `hx::SplitView::create(input, delimiter, keepEmpty)` and `next()`, returning borrowed `hx::StringView` values through `Result`; EOF is available through `status()`.
- `hx::TextUtilities::parseI64(input, base=10)` and `parseU64()`, returning `Result<int64_t>` / `Result<uint64_t>`.
- `hx::TextUtilities::compareNatural(left, right)`, returning the ordinary signed comparison result with the same valid-view precondition.

The facade adds no allocations or exceptions. A default-constructed `SplitView` is exhausted. Copies have independent cursor positions, but do not extend source storage lifetime.

Build `hx_os_text_utilities` with examples enabled and numeric text conversion enabled, then run the executable from the configured `bin` directory. The complete [C example](../examples/text_utilities.c) prints split piece lengths, `Port: 443, offset: -42`, and `file2 sorts before file10: yes`. Split and natural comparison are always part of the string core; only numeric conversion depends on the feature switch.
