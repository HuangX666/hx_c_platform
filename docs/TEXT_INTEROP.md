# Native wide text and UTF-8 stdio paths

[简体中文](TEXT_INTEROP.zh-CN.md) · [README](../README.md)

**This page has two complete programs you can save as main.c.** Use the [minimal C project](USAGE.md#c-project), choosing one program at a time.

The first creates or replaces `message-中文.txt` in the working directory. Success is exit code 0 and file content `Hello, 中文!` followed by newline; it prints no success message. The second converts native wchar_t text to UTF-8 and displays `中文😀` on a UTF-8-capable terminal.

Use the first when a third-party interface requires FILE*, and the second when a native API provides wide text. Ordinary UTF-8 file reads can use the file convenience API directly.

Use these helpers when an existing native API expects `wchar_t` or a third-party API expects a standard `FILE *`. Ordinary whole-text reading still uses `hx_os_file_read_text()` / `hx::Files::readText()`.

## 1. Open a standard FILE using a UTF-8 filename

This complete C99 program writes a UTF-8 message without newline conversion. The path and mode are borrowed only during opening. Opening converts the filename, not the file content.

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

Accepted modes start with `r`, `w` or `a`, followed by optional `b` and `+` in either order: for example `rb`, `wb`, `ab`, `r+b` or `rb+`. Duplicate flags and CRT-specific mode extensions return INVALID before calling the CRT. `b` preserves bytes on Windows. These synchronous calls may block; `fclose` also flushes the CRT's user-space buffer. This is not a file-persistence guarantee.

`hx_os_fclose()` closes through the opening library's CRT and always clears the pointer, including on a close error. Direct caller-side `fread`, `fwrite` and other stdio operations require a compatible shared CRT. In particular, Windows DLLs built with separate static CRT instances cannot exchange these streams safely. Use the opaque `hx_os_file` API across that boundary. Windows uses the same UTF-8 validation and long-path conversion as the existing file API.

## 2. Convert native wide strings into hx_string

This complete C99 program owns and releases its output string:

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

`units` counts `wchar_t` elements, not bytes or Unicode codepoints. Windows supplementary characters use two UTF-16 units. Supported POSIX targets use UTF-32 native wide units. Conversion is strict and independent of the process locale. It rejects isolated surrogates and out-of-range scalar values; failure preserves the original `hx_string`.

The buffer-level `hx_os_text_to_wide()` and `hx_os_text_from_wide()` functions accept explicit lengths and preserve embedded NUL. Capacity and required size use destination units and include a final NUL. A NULL output with capacity zero measures and returns OUTPUT_FULL. Invalid input or insufficient capacity leaves the output buffer unchanged. Neither input/output buffers nor the required-size storage may overlap.

## 3. C++11 convenience without exceptions

Include `<hx/Text.hpp>`. This function converts a native string to UTF-8 and back; the returned wide string owns its storage:

```cpp
hx::Result<hx::WideString> make_native_title() noexcept
{
	auto text = hx::Text::fromWide(L"中文😀");
	if (!text)
		return hx::Failure(text.error());

	return hx::Text::toWide(text.value());
}
```

Check the result, then use `result.value().c_str()` while the result remains alive. `WideString::size()` excludes the terminator and counts native wide units. `fromWideUnits(pointer, units, context)` preserves embedded NUL using an explicit length. `toWide()` also accepts a context for the owned destination. All first-party functions use `Result` and compile with exceptions disabled.

## 4. ASCII case-insensitive comparison and search

`hx_string_compare_ascii_nocase()` returns -1, 0 or 1; `hx_string_find_ascii_nocase()` returns a byte offset or `HX_STRING_NOT_FOUND`. The C++ `String` methods are `compareAsciiNoCase()` and `findAsciiNoCase()`. They allocate nothing, preserve embedded NUL, and do not depend on the process locale. Only A-Z fold to a-z: non-ASCII UTF-8 characters compare exactly, so these helpers do not equate `ß` with `ss` or provide Unicode normalization. Search follows the existing find contract: invalid needles or a start inside a UTF-8 codepoint return NOT_FOUND.
