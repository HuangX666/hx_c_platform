# Unicode normalization and case folding

[简体中文](UNICODE.zh-CN.md) · [README](../README.md)

When matching user search queries, comparing cross-platform filenames, or normalizing internationalized identifiers, visually identical text can have multiple UTF-8 byte representations (for example, precomposed `é` vs. `e` followed by a combining acute accent), and case conversion extends far beyond ASCII `A–Z` (for instance, German `ß` folds to the two-letter sequence `ss`). Naive byte-wise comparisons or ASCII lowercasing will miss legitimate matches. This module wraps a pinned `utf8proc` build to provide standard Unicode normalization (`NFC`, `NFD`, `NFKC`, `NFKD`) and locale-independent case folding.

In practice, you should **keep the user's original display text intact and transform a separate copy used for comparison or indexing**. To see this workflow, inspect [unicode_tools.c](../examples/unicode_tools.c): the program passes a UTF-8 view of `"Straße"` to `hx_os_unicode_casefold()`, writes the folded result into an `hx_string`, and prints it. Make sure `HX_OS_ENABLE_UNICODE` and the `utf8proc` SDK are enabled, then follow the [example run instructions](../examples/README.md#run) to build and run `hx_os_unicode_tools` (no command-line arguments or input files are needed):

```text
strasse
```

Notice how `ß` expands into two characters (`ss`), illustrating that normalization and case folding can change both byte length and code point count. If you consume an installed package, your [installed SDK consumer](USAGE.md#installed-project) build must also keep the corresponding SDK module enabled.

## 1. Choose a transformation

The C API is declared in `<hx_os.h>` and provides two primary entry points:

- `hx_os_unicode_normalize(input, form, out, error)`: Normalizes `input` into `HX_OS_UNICODE_NFC`, `HX_OS_UNICODE_NFD`, `HX_OS_UNICODE_NFKC`, or `HX_OS_UNICODE_NFKD`.
- `hx_os_unicode_casefold(input, out, error)`: Applies locale-independent Unicode case folding and normalizes the output to `NFC`.

The destination `out` must be an initialized `hx_string` (such as `HX_STRING_INIT`). Both functions allocate through `out`'s bound memory context and leave `out` unchanged if an error occurs. The `input` view may safely borrow from `out`'s existing buffer.

```c
#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_string result = HX_STRING_INIT;

	hx_os_status status = hx_os_unicode_casefold(
		hx_string_view_from_cstr("Straße"), &result, NULL);
	if (status == HX_OS_OK)
		puts(hx_string_c_str(&result));

	hx_string_destroy(&result);
	return status == HX_OS_OK ? 0 : 1;
}
```

In C++11, include `<hx/Unicode.hpp>` (or `<hx/Os.hpp>`) and call `hx::Unicode::normalize()` or `hx::Unicode::casefold()`, both of which return `hx::Result<hx::String>`. See [unicode_tools.c](../examples/unicode_tools.c) for the complete runnable example.

## 2. Meaning and limits

Choose the transformation that matches your domain rules:

- **NFC (Canonical Decomposition, followed by Canonical Composition)**: Combines canonically equivalent sequences into standard precomposed characters. Ideal for storage, wire protocols, and general equality checks.
- **NFD (Canonical Decomposition)**: Splits precomposed characters into base characters and combining marks. Useful when inspecting or stripping diacritics.
- **NFKC / NFKD (Compatibility Decomposition and Composition)**: Also folds compatibility variants—such as full-width alphanumerics, superscripts, and ligatures (`ﬁ` → `fi`)—into standard characters. Useful for search indexing and loose matching.
- **Case folding (`hx_os_unicode_casefold`)**: Eliminates case distinctions for case-insensitive matching independently of the process locale. It can change string length and is neither locale-aware collation (sorting) nor language-specific casing (such as Turkish dotted/dotless `I`/`ı`).

> [!WARNING]
> **Do Not Blindly Transform Passwords or Exact IDs:**
> The library never normalizes file paths, passwords, or business identifiers behind your back. Do not apply `NFKC` or `casefold` automatically to case-sensitive tokens, exact database keys, or password inputs unless your specification explicitly requires it.

**Size Bounds and SDK Integration:**
- **Strict UTF-8 & memory limits**: Inputs must be valid UTF-8 (length-counted embedded `NUL` bytes are allowed) up to **1 MiB**. Internal scratch space and final output are capped at **16 MiB**. Exceeding size bounds returns `HX_OS_LIMIT`, and malformed UTF-8 returns `HX_OS_INVALID`.
- **No global state mutation**: Operations never touch the process locale or override `utf8proc`'s global allocator; all temporary buffers use the destination string's memory context.
- **Optional module & native access**: You can disable the module via `-DHX_OS_ENABLE_UNICODE=OFF`, in which case these functions return `HX_OS_NOT_SUPPORTED`. When you need low-level code point property queries directly from `utf8proc`, link against the [native dependencies](NATIVE_DEPENDENCIES.md) target.
