# Native wide text and UTF-8 stdio paths

[简体中文](TEXT_INTEROP.zh-CN.md) · [README](../README.md)

Keeping strings in UTF-8 throughout your application is clean and portable, until you hit two common platform boundaries: passing a UTF-8 file path to a third-party library that demands a standard C `FILE *` (where plain `fopen()` on Windows interprets paths using the active ANSI code page), or exchanging `wchar_t *` buffers with native OS APIs. This module bridges both boundaries cleanly and provides locale-independent ASCII case-insensitive string helpers.

> [!TIP]
> When you simply want to read or write a UTF-8 text file yourself, use `hx_os_file_read_text()` / `hx::Files::readText()` directly—no `FILE *` or `wchar_t` conversion is needed.

Sections 1 and 2 below each contain a complete, self-contained C99 program that you can save as `main.c` and build with the [minimal C project](USAGE.md#c-project):
- **Program 1** creates or overwrites `message-中文.txt` in the working directory with `Hello, 中文!` followed by a newline, exiting with status `0` on success.
- **Program 2** converts the native `wchar_t` literal `L"中文😀"` into UTF-8 and prints `中文😀` to a UTF-8 terminal.

## 1. Open a standard FILE using a UTF-8 filename

When a third-party C library requires a `FILE *`, include `<hx_os_stdio.h>` and call `hx_os_fopen_utf8()` to open the stream from a UTF-8 path, then close it with `hx_os_fclose()`. Note that `hx_os_fopen_utf8()` converts only the **filename path** (including UTF-8 validation and Windows UTF-16 long-path handling), leaving file contents untouched.

```c
#include <hx_os_stdio.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	const char *message = "Hello, 中文!\n";
	hx_os_error error;
	FILE *file = hx_os_fopen_utf8("message-中文.txt", "wb", &error);
	if (!file) {
		fprintf(stderr, "%s\n", hx_os_status_name(error.status));
		return 1;
	}

	size_t written = fwrite(message, 1, strlen(message), file);
	hx_os_status closed = hx_os_fclose(&file, &error);

	return written == strlen(message) && closed == HX_OS_OK ? 0 : 1;
}
```

- **Supported mode strings**: Modes must start with `r`, `w`, or `a`, followed by optional `b` and `+` in either order (such as `rb`, `wb`, `ab`, `r+b`, or `rb+`). Duplicate flags or vendor-specific CRT mode extensions are rejected with `HX_OS_INVALID` before invoking the underlying C runtime. Include `b` on Windows to preserve exact newline bytes.
- **Closing & CRT ownership**: `hx_os_fclose(&file, &error)` closes the stream through the same C runtime that opened it (flushing the CRT's user-space buffer, though this is not an OS-level disk persistence guarantee) and always nulls out `file`, even if closing reports an error.

> [!WARNING]
> **Shared CRT Requirement Across Windows DLL Boundaries:**
> When caller code invokes `fread()`, `fwrite()`, or other `<stdio.h>` functions directly on the returned `FILE *`, both the caller and `hx_c_platform` must share the same C runtime (CRT) instance. On Windows, two DLLs linked against separate static CRT instances (`/MT`) cannot safely exchange `FILE *` streams—use the opaque `hx_os_file` API across such boundaries instead.

## 2. Convert native wide strings into hx_string

Use `hx_string_assign_wide()` to convert a native `wchar_t` buffer into a UTF-8 `hx_string`:

```c
#include <hx_string.h>
#include <wchar.h>
#include <stdio.h>

int main(void)
{
	hx_string text = HX_STRING_INIT;
	const wchar_t *native = L"中文😀";

	hx_os_status status = hx_string_assign_wide(
		&text, native, wcslen(native), NULL);
	if (status == HX_OS_OK)
		puts(hx_string_c_str(&text));

	hx_string_destroy(&text);

	return status == HX_OS_OK ? 0 : 1;
}
```

- **`units` counts `wchar_t` elements**: The `units` parameter specifies the number of `wchar_t` elements, not byte count or Unicode code point count. On Windows (`UTF-16` `wchar_t`), supplementary-plane characters like `😀` occupy **two** `wchar_t` units; on supported POSIX targets (`UTF-32` `wchar_t`), each code point is one unit.
- **Strict, locale-independent validation**: Conversion never depends on the process locale. Unpaired surrogates and out-of-range scalar values fail with `HX_OS_INVALID` while leaving the destination `hx_string` untouched.
- **Buffer-level conversion APIs**: `hx_os_text_to_wide()` and `hx_os_text_from_wide()` accept explicit element lengths and preserve embedded `NUL` characters. Both `capacity` and the reported `required` size are measured in **destination elements** (`wchar_t` or `char`) and **include the trailing NUL terminator**. Passing `output = NULL` with `capacity = 0` queries the required element count and returns `HX_OS_OUTPUT_FULL`. Input, output, and the `required` size variable must not overlap.

## 3. C++11 convenience without exceptions

In C++11, include `<hx/Text.hpp>` to convert between `hx::WideString` and `hx::String` via `hx::Result` without C++ exceptions:

```cpp
hx::Result<hx::WideString> make_native_title() noexcept
{
	auto text = hx::Text::fromWide(L"中文😀");
	if (!text)
		return hx::Failure(text.error());

	return hx::Text::toWide(text.value());
}
```

- Check `if (result)` first, then access `result.value().c_str()` while the `hx::WideString` object remains alive.
- `hx::WideString::size()` returns the number of native `wchar_t` units excluding the trailing `L'\0'`.
- Use `hx::Text::fromWideUnits(pointer, units, context)` to preserve embedded `L'\0'` units via an explicit length; `hx::Text::toWide()` also accepts an optional `hx_context` allocator.

## 4. ASCII case-insensitive comparison and search

When comparing or searching protocol tokens, HTTP header names, or ASCII keys without case sensitivity:

- **C API**: `hx_string_compare_ascii_nocase()` (returns `-1`, `0`, or `1`) and `hx_string_find_ascii_nocase()` (returns the matching byte offset or `HX_STRING_NOT_FOUND`).
- **C++11 API**: `hx::String::compareAsciiNoCase()` and `hx::String::findAsciiNoCase()`.

These helpers perform zero heap allocations, preserve embedded `NUL` bytes, and ignore the process locale. Only ASCII `A–Z` and `a–z` are folded; non-ASCII UTF-8 bytes must match byte-for-byte (so `ß` does not match `ss`, and no Unicode normalization is performed—use the [Unicode module](UNICODE.md) when full Unicode case folding is needed). Search follows the standard `find` contract: an invalid UTF-8 needle or a starting offset that lands in the middle of a multibyte UTF-8 code point returns `HX_STRING_NOT_FOUND`.
