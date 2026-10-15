# Unicode normalization and case folding

[简体中文](UNICODE.zh-CN.md) · [README](../README.md)

**Start with a complete program.** For Unicode-insensitive input matching, transform a comparison copy while preserving the original display text.

Open [unicode_tools.c](../examples/unicode_tools.c). Pass a UTF-8 view of Straße → unicode_casefold writes hx_string → use the result → destroy. Enable UNICODE with its utf8proc SDK.

Follow the [example run instructions](../examples/README.md#run), enable examples and build only `hx_os_unicode_tools`. This program needs no arguments or input files.

**Expected result:** It prints `strasse`: ß becomes two letters, so length can change. Do not automatically apply this to passwords or IDs requiring exact comparison.

Short snippets below explain individual calls; use the complete example when running them. An [installed SDK consumer](USAGE.md#installed-project) must also enable the corresponding SDK module.

## 1. Choose a transformation

`hx_os_unicode_normalize(input, form, out, error)` supports NFC, NFD, NFKC and NFKD. `hx_os_unicode_casefold(input, out, error)` applies Unicode case folding and NFC. The initialized hx_string keeps its value on failure and uses its own allocator. Input may borrow it.

```c
hx_string result = HX_STRING_INIT;

hx_os_status status = hx_os_unicode_casefold(
	hx_string_view_from_cstr("Straße"), &result, NULL);
if (status == HX_OS_OK)
	puts(hx_string_c_str(&result));

hx_string_destroy(&result);
```

Include hx_os.h and stdio.h; output is strasse. C++11 uses `hx::Unicode::normalize` / `casefold` and Result<String>. Complete example: [unicode_tools.c](../examples/unicode_tools.c).

## 2. Meaning and limits

NFC joins canonical equivalents; NFD decomposes them. Compatibility forms NFKC/NFKD additionally change compatibility characters. Case folding is locale-independent and can change text length. It is not locale collation or Turkish-language casing.

Inputs are strict UTF-8, including length-counted NUL, limited to 1 MiB. Conversion scratch/output is bounded to 16 MiB. No global locale, normalization of paths/passwords/IDs, or background work is implied. Select transformations explicitly for each business field.

UNICODE uses the pinned utf8proc SDK and can be disabled; functions then return NOT_SUPPORTED. The implementation controls its scratch through the output context and does not replace utf8proc's global allocator. Direct native utf8proc calls remain available through [native dependencies](NATIVE_DEPENDENCIES.md).
