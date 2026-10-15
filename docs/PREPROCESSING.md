# Runtime constants, conditions and includes

[简体中文](PREPROCESSING.zh-CN.md) · [README](../README.md)

**Start with a complete program.** Preprocess runtime configuration text when it needs constants and conditional sections before JSON or custom parsing.

Open [preprocess_config.c](../examples/preprocess_config.c). Prepare independent define/if text → preprocess_text_ex expands it → check status/diagnostic → use output → destroy.

Follow the [example run instructions](../examples/README.md#run), enable examples and build only `hx_os_preprocess_config`. This program needs no arguments or input files.

**Expected result:** The JSON output contains title Camera, port 8080 and enabled true. The demo only prints this text; application code still parses and validates fields. Includes require an explicitly bounded loader.

Short snippets below explain individual calls; use the complete example when running them. You can also use its source as main in the [minimal project](USAGE.md#c-project).

## 1. Start with one operation

The preprocessor transforms UTF-8 configuration text at runtime. It does not change C/C++ source syntax, invoke a compiler or add a code-generation step. Keep templates as data files, such as `.hxconf`, rather than compilation units.

```c
hx_string text = HX_STRING_INIT;
hx_os_error error;

hx_os_status status =
	hx_os_preprocess_file("device.hxconf", &text, &error);
if (status == HX_OS_OK)
	puts(hx_string_c_str(&text));

hx_string_destroy(&text);
```

Include `<hx_os.h>` and `<stdio.h>`. Use `hx_os_preprocess_text(input, &text, &error)` for a C string. The advanced text entry accepts a byte span, source name, options and optional diagnostic. Output uses its own context, remains unchanged on failure and needs no global initialization.

[preprocess_config.c](../examples/preprocess_config.c) is a complete executable. Build `hx_os_preprocess_config`, then run `build/bin/hx_os_preprocess_config` on Linux/macOS or `build/bin/Release/hx_os_preprocess_config.exe` with the Visual Studio generator. It prints valid JSON containing title `Camera`, port `8080` and enabled `true`; additional whitespace is normal. The paired files [device.hxconf](../examples/data/preprocess/device.hxconf) and [common.hxconf](../examples/data/preprocess/common.hxconf) demonstrate file inclusion. Copy them together, then pass the first path to the file entry. Feed processed text to the [configuration parser](CONFIGURATION.md) to obtain typed values.

## 2. Language rules

```text
#define PORT 8080
#define TITLE "Camera"
#if defined(PORT) && PORT >= 1024
{ "name": TITLE, "port": PORT }
#else
{ "name": TITLE, "port": 8080 }
#endif
```

| Directive | Behavior |
| --- | --- |
| `#define NAME replacement` | Copies an object macro; the last successful definition wins |
| `#define NAME` | Defines an empty replacement, still visible to ifdef |
| `#undef NAME` | Removes a definition; a missing name is harmless |
| `#if expression` | Starts a conditional branch |
| `#ifdef NAME`, `#ifndef NAME` | Tests definition existence |
| `#elif expression` | Selects a branch only when none was previously selected |
| `#else`, `#endif` | Selects the remainder once, or closes the current condition |
| `#include "relative.hxconf"` | Resolves and processes another source |

The `#` must be the first non-space/comment token on a line. Names are case-sensitive UTF-8 identifiers, at most 127 bytes, using the existing lexer rules. Strings/comments are copied without macro expansion. Each included source balances its own conditions; it cannot close the parent condition. Undefined ordinary identifiers remain unchanged.

Conditions accept signed int64 decimal/hex and true/false literals, `defined(NAME)` or `defined NAME`, unary `+ - !`, comparisons, `&&`, `||` and parentheses. Precedence is unary, relational comparison, equality, `&&`, then `||`. Conditions expand as text first, preserving this precedence without silently parenthesizing macros. Undefined condition identifiers are zero. Names in `defined` are not expanded; defined/true/false cannot be redefined. Compound operator characters must be adjacent. All operands are validated, including unused logical operands; expression nesting is limited to 64.

There are no function macros, stringification/token pasting, line continuation, binary arithmetic, bitwise operations, integer suffixes, character arithmetic, compiler/system includes or automatic host-variable access. Unsupported active directives/operators fail explicitly. Inactive branches still require lexically valid UTF-8, but do not invoke includes or evaluate nested conditions. A BOM is accepted at a complete source beginning, not in an internal fragment.

## 3. Explicit file access

The **text entry has no filesystem resolver by default**; an active include reports `HX_OS_NOT_SUPPORTED` unless options supply one. The **file entry provides a relative-file resolver**, using the directory of the including file and handling Windows UTF-8 conversion internally.

The built-in resolver rejects absolute include names, colon syntax and `..` components, accepts either slash separator and follows filesystem links. It is bounded access for trusted configuration files, not a security sandbox. Use a custom resolver for an application resource map, package or approved file set. `HX_OS_PREPROCESS_NO_INCLUDES` disables includes even when a resolver exists. FILE_UTILS is needed only for the file entry.

A resolver receives the parent identifier, requested name and remaining byte allowance. It fills initialized `hx_string` outputs and must honor that allowance. It must not retain the output pointers, change their contexts or destroy their owners. Returned sizes, UTF-8 and a nonempty resolved identifier up to 4,096 bytes are checked. Stable identifiers permit cycle detection; depth/byte bounds also limit aliases that do not compare equal. Callback side effects are not rolled back. Resolver and file I/O are synchronous, without a hard deadline or an automatically started worker.

## 4. C++11 and diagnostics

Include `<hx/Preprocessor.hpp>` and `<cstdio>`. In a function returning `hx_os_status`:

```cpp
hx::PreprocessDiagnostic why;
auto text = hx::Preprocessor::file("device.hxconf", NULL, &why);
if (!text) {
	std::fprintf(stderr, "%s:%zu:%zu: %s (%s)\n",
		why.sourceName(), why.line(), why.column(),
		why.description(), hx_os_status_name(text.status()));

	return text.status();
}

std::puts(text.value().c_str());
```

Return `HX_OS_OK` on the successful path. `Preprocessor::text()` accepts `StringView`. Both methods return an owning String without exceptions. Options, diagnostic and context are optional; a normal call needs only the input.

For C, initialize `hx_os_preprocess_diagnostic` with `HX_OS_PREPROCESS_DIAGNOSTIC_INIT` or its initializer and later destroy it. Each call clears old diagnostics. Failure records the source and 1-based invocation line/column when known. Description borrows static library text; source is copied best-effort and can be empty on OOM. Diagnostic failure never replaces the primary status/native error. Diagnostic storage must not alias input, source name or output. Keep the implementing module loaded through all string/diagnostic lifetimes.

Output can gain spaces and blank lines to preserve token boundaries and separate included content; this is not a formatting-preserving editor or full source map. Lexer returns newline tokens by default; choose `HX_OS_LEXER_SKIP_NEWLINES` when the downstream grammar treats them as whitespace. Configuration parsers apply their own format rules.

## 5. Limits and ownership

| Option | Default | Maximum |
| --- | --- | --- |
| `max_input_bytes` per source | 16 MiB | 64 MiB |
| `max_total_input_bytes` across sources | 64 MiB | 64 MiB |
| `max_output_bytes` | 16 MiB | 64 MiB |
| `max_defines` | 256 | 4,096 |
| `max_macro_bytes` retained names/replacements | 1 MiB | 64 MiB |
| `max_expanded_bytes` cumulatively processed replacements | 64 MiB | 64 MiB |
| `max_expansions`, `max_directives` | 65,536 each | 1,048,576 each |
| `max_include_depth`, counting the root | 16 | 64 |
| `max_expansion_depth` | 32 | 64 |
| `max_condition_depth` per source | 64 | 64 |

Use `HX_OS_PREPROCESS_OPTIONS_INIT` or the capacity-checked initializer. Zero limits are invalid. Initial definition count must fit max_defines; names/values are borrowed through the call and copied into its private macro map. Each invocation has independent definitions, also during concurrent calls. Every processed replacement consumes the expanded-byte budget. Existing lexer token-size/count limits still apply.

Source and expansion stacks grow on demand within their limits; macros reuse PARAMETERS. These are payload/operation limits, not exact process RSS: allocation headers, geometric growth, lexer copies, current destination contents and temporary buffers add bounded library overhead. Custom resolver internals remain its responsibility. Lower limits to the actual requirements of an embedded application.

Success replaces output; failure releases temporary strings, lexers, frames and macros while preserving it. Serialize access to participating output/diagnostic objects and keep callbacks, contexts and input alive until return. The module changes no process locale, environment, working directory or global configuration. `HX_OS_ENABLE_PREPROCESSOR` defaults to ON and also requires LEXER and PARAMETERS; `hx_os_preprocess_available()` checks this combination. Missing modules give NOT_SUPPORTED. No new third-party dependency is introduced.
