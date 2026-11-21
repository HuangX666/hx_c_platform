# Borrowed text utilities

[简体中文](TEXT_UTILITIES.zh-CN.md) · [String tools](COMMON_UTILITIES.md) · [README](../README.md)

When splitting delimited fields, parsing port numbers or numeric flags, or sorting numbered names such as `file2` and `file10`, you rarely need to allocate a new heap string for every token. This module provides **zero-allocation borrowed string splitting**, **strict 64-bit integer parsing**, and **natural sort comparison**.

Open [text_utilities.c](../examples/text_utilities.c) to see all three tools working together: the program iterates over borrowed slices with `hx_os_text_split_init()` and `hx_os_text_split_next()`, parses integer tokens with `hx_os_text_parse_u64()` and `hx_os_text_parse_i64()`, and compares numbered filenames with `hx_os_text_compare_natural()`. Follow the [example run instructions](../examples/README.md#run) to enable examples and build `hx_os_text_utilities` (no command-line arguments or input files are required):

```text
Piece: 'alpha' (5)
Piece: 'beta' (4)
Piece: '' (0)
Piece: 'gamma' (5)
Piece: '' (0)
Port: 443, offset: -42
file2 sorts before file10: yes
```

You can also drop the example source into the [minimal project](USAGE.md#c-project) as `main.c`.

## 1. Split without copying the text

`<hx_text_utils.h>` provides a stack-allocated cursor (`hx_os_text_split`) that yields `hx_string_view` slices pointing directly into the existing input buffer. It performs zero heap allocations and never mutates the input text (when you want an array of owning string copies instead, use `hx_string_split()` or `hx::Strings::split()` from [String tools](COMMON_UTILITIES.md)).

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

This loop yields five slices: `alpha`, `beta`, an empty slice, `gamma`, and a trailing empty slice. The delimiter is the exact byte sequence `"::"`, not a set of individual separator characters. Matching proceeds left-to-right without overlap—for example, splitting `"aaa"` by `"aa"` yields an empty slice followed by `"a"`.

Use the `keep_empty` parameter to control how empty slices are handled:

| Input case | `keep_empty = 1` (retain empty) | `keep_empty = 0` (skip empty) |
| --- | --- | --- |
| Empty input `""` | Yields one empty slice, then `HX_OS_EOF` | Yields no slices (`HX_OS_EOF` immediately) |
| Consecutive delimiters (`a::::b`) | Retains intermediate empty slices | Skips intermediate empty slices |
| Trailing delimiter (`gamma::`) | Retains the final empty slice | Skips the final empty slice |
| Empty delimiter `""` | Fails initialization with `HX_OS_INVALID` | Fails initialization with `HX_OS_INVALID` |

> [!IMPORTANT]
> **UTF-8 Validation and Borrowed Lifetime Rules:**
> - **One-time UTF-8 validation**: `hx_os_text_split_init()` validates both the input and delimiter views as strict UTF-8 once (supporting multibyte UTF-8 delimiters and length-counted embedded `NUL` bytes); if validation fails, the cursor is left untouched. Subsequent `hx_os_text_split_next()` calls scan for exact delimiter matches in constant auxiliary space without re-validating the entire input.
> - **Iteration status**: `hx_os_text_split_next()` returns `HX_OS_OK` for each slice (including empty slices) and `HX_OS_EOF` when exhausted (`HX_OS_EOF` and errors leave `part` unchanged).
> - **Keep input storage alive**: Both the input text and delimiter must remain alive and unmodified while iterating and while using any returned `part` slice. Returned `hx_string_view` slices borrow the original buffer and **are not guaranteed to be NUL-terminated**—copy a slice into an `hx_string` if you need to retain it after the source buffer is freed.

## 2. Strict integer conversion

Standard C functions like `strtol` and `strtoull` silently skip leading whitespace, treat leading `0` as octal when `base == 0`, and require checking `errno` and end pointers to detect trailing garbage. `hx_os_text_parse_i64()` and `hx_os_text_parse_u64()` parse a complete bounded `hx_string_view` token with zero allocations and no locale dependency. Pass an explicit radix of `2`, `8`, `10`, or `16`—leading zeros never switch the radix automatically.

```c
uint64_t port = 0;
hx_os_status status = hx_os_text_parse_u64(
	hx_string_view_from_cstr("443"), 10, &port, NULL);

if (status != HX_OS_OK)
	fprintf(stderr, "Invalid port: %s\n", hx_os_status_name(status));
```

| Input token | Behavior |
| --- | --- |
| `"+42"` in base 10 | Accepted by both signed (`i64`) and unsigned (`u64`) parsers |
| `"-42"` in base 10 | Accepted only by `hx_os_text_parse_i64()`; rejected by `u64` |
| `"-0"` in unsigned `u64` | Rejected with `HX_OS_TYPE_MISMATCH` |
| `"ff"` or `"FF"` in base 16 | Both produce `255` |
| `"101"` in base 2; `"377"` in base 8 | Produce `5` and `255` respectively |
| `"0xff"`, leading/trailing whitespace, `"1_000"`, `"1.0"`, `"1e2"`, `"42ms"` | Rejected (no prefixes, whitespace, digit separators, fractions, or suffixes) |
| Empty input `""`, standalone sign `"+"` / `"-"`, or an included trailing `NUL` | Rejected |

- **Status codes & full 64-bit precision**: The output variable is written only on complete success and left unchanged on any failure. Invalid arguments or unsupported bases return `HX_OS_INVALID`, malformed characters return `HX_OS_TYPE_MISMATCH`, and values outside the target type's range return `HX_OS_RANGE` (the parser still validates the remainder of the token so syntax errors are never masked by an earlier overflow). Pure integer arithmetic covers the full `INT64_MIN`…`INT64_MAX` and `0`…`UINT64_MAX` ranges without floating-point rounding.
- **Feature toggle**: Controlled by `HX_OS_ENABLE_TEXT_NUMBER` (enabled by default). When disabled, both functions return `HX_OS_NOT_SUPPORTED` and `HX_OS_CAP_TEXT_NUMBER` is omitted from capability queries.

## 3. Natural ordering for names

Lexicographical byte sorting places `"file10"` before `"file2"`. `hx_os_text_compare_natural(left, right)` returns `-1`, `0`, or `1` for use in sorting comparators: whenever both strings reach an ASCII digit run (`0–9`) at the same position, it compares the **numeric magnitude** of the entire digit run directly without converting into a fixed-width integer, so arbitrarily long digit sequences never overflow.

```text
file0 < file00 < file2 < file2a < file02 < file002 < file10
```

- **Leading-zero tie-breaker**: When two digit runs have the same numeric value (such as `"2"` and `"02"`), the **shorter complete digit run sorts first**, and this tie-breaker takes precedence over any characters following the digit run. Consequently, `hx_os_text_compare_natural()` returns `0` if and only if the two byte sequences are **byte-for-byte identical**.
- **Non-digit byte comparison**: Non-digit bytes are compared as unsigned bytes. UTF-8 strings with shared prefixes—such as `"设备2"` and `"设备10"`—compare their shared UTF-8 prefix bytes and then order naturally by their ASCII digit runs. Note that this comparator does not perform locale collation, Unicode normalization, or case folding, and treats non-ASCII digits, signs, and decimal points as ordinary bytes. Callers must pass valid byte ranges (`NULL` is permitted only when `size == 0`).

## 4. C++11 and the runnable example

In C++11, include `<hx/TextUtilities.hpp>` for zero-allocation, exception-free wrappers:

- `hx::SplitView::create(input, delimiter, keepEmpty)` and `next()`: Return borrowed `hx::StringView` slices wrapped in `hx::Result`, with `status()` reporting `HX_OS_EOF` once exhausted (a default-constructed `SplitView` starts in the exhausted state).
- `hx::TextUtilities::parseI64(input, base = 10)` and `parseU64(input, base = 10)`: Return `hx::Result<int64_t>` and `hx::Result<uint64_t>`.
- `hx::TextUtilities::compareNatural(left, right)`: Returns `-1`, `0`, or `1`.

Splitting and natural comparison are always available in the core string library, while integer parsing follows `HX_OS_ENABLE_TEXT_NUMBER`. See the complete [C example](../examples/text_utilities.c) for a runnable demonstration.
