# Runtime constants, conditions and includes

[简体中文](PREPROCESSING.zh-CN.md) · [README](../README.md)

When runtime configuration files need shared constants (`#define`), environment- or feature-gated sections (`#if` / `#ifdef`), or modular composition across relative files (`#include`), you can run a **bounded runtime preprocessing pass** before handing the resulting text to a JSON parser or custom grammar parser.

To see it in action, open [preprocess_config.c](../examples/preprocess_config.c). The program prepares a configuration snippet containing `#define` macros and `#if` branches, expands it in memory with `hx_os_preprocess_text_ex()`, checks the returned status and diagnostic struct, and prints the resulting JSON text. Follow the [example run instructions](../examples/README.md#run) to enable examples and build `hx_os_preprocess_config` (no command-line arguments or external input files are needed):

```json
{
  "title": "Camera",
  "port": 8080,
  "enabled": true
}
```

In a real application, preprocessing produces the expanded text, which you then pass to the [configuration parser](CONFIGURATION.md) or your own lexer to extract typed fields. You can also drop the example source into the [minimal project](USAGE.md#c-project) as `main.c`.

## 1. Start with one operation

The runtime preprocessor transforms UTF-8 configuration data files (such as `.hxconf`) at runtime—it never alters C/C++ compilation units, invokes a C compiler, or requires a build-time code generator.

```c
#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_string text = HX_STRING_INIT;
	hx_os_error error;

	hx_os_status status =
		hx_os_preprocess_file("device.hxconf", &text, &error);
	if (status == HX_OS_OK)
		puts(hx_string_c_str(&text));

	hx_string_destroy(&text);
	return status == HX_OS_OK ? 0 : 1;
}
```

- **File entry vs. text entry**: Call `hx_os_preprocess_file()` to preprocess a file on disk, or `hx_os_preprocess_text(input, &text, &error)` for an in-memory C string. Use the extended entry points `hx_os_preprocess_text_ex()` and `hx_os_preprocess_file_ex()` when you want to pass an explicit byte length, custom source label, resource budget options, or an `hx_os_preprocess_diagnostic` struct.
- **Transactional output**: Output is written into an initialized `hx_string` using its bound memory context, and any failure leaves the destination string untouched.
- **Trying `#include` files**: The companion files [device.hxconf](../examples/data/preprocess/device.hxconf) and [common.hxconf](../examples/data/preprocess/common.hxconf) demonstrate relative file inclusion. Copy both files into the same directory, pass `device.hxconf` to `hx_os_preprocess_file()`, and pass the expanded output to the [configuration parser](CONFIGURATION.md) to read typed values.

## 2. Language rules

Templates use familiar line-oriented `#` directives:

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
| `#define NAME replacement` | Defines an object-like macro; if redefined, the latest successful definition wins |
| `#define NAME` | Defines a macro with an empty replacement body (`#ifdef NAME` and `defined(NAME)` evaluate to true) |
| `#undef NAME` | Removes a macro definition (succeeds harmlessly if `NAME` was not defined) |
| `#if expression` | Evaluates a conditional expression and opens a conditional block |
| `#ifdef NAME`, `#ifndef NAME` | Tests whether `NAME` is currently defined |
| `#elif expression` | Evaluates and enters a branch only if no prior branch in the chain was taken |
| `#else`, `#endif` | Enters the fallback branch (at most once per level) or closes the current conditional block |
| `#include "relative.hxconf"` | Resolves and recursively preprocesses another source through the active include resolver |

**Lexical and Conditional Evaluation Rules:**
- **Directive placement & identifiers**: The `#` token must be the first non-whitespace, non-comment token on its physical line. Macro names are case-sensitive UTF-8 identifiers up to `127` bytes long. **Macros are never expanded inside string literals or comments**, and undefined identifiers in ordinary body text are emitted unchanged. Each source file (including included files) must close its own `#if ... #endif` blocks—an included file cannot close a conditional opened by its parent.
- **Conditional expressions**: `#if` and `#elif` support signed 64-bit decimal and hexadecimal integers, `true` / `false` literals, `defined(NAME)` or `defined NAME`, unary `+`, `-`, and `!`, relational and equality operators (`<`, `<=`, `>`, `>=`, `==`, `!=`), logical `&&` and `||`, and parentheses `(...)`. Operator precedence from highest to lowest is: unary → relational → equality → `&&` → `||`.
- **Macro expansion in conditions**: Identifiers in `#if` expressions (other than the target of `defined`) undergo text macro expansion before expression evaluation without implicitly wrapping macro bodies in extra parentheses. Identifiers that remain undefined after expansion evaluate to `0`; the keywords `defined`, `true`, and `false` cannot be redefined or undefined. Expression parentheses can nest up to `64` levels deep, and all operands are validated syntactically even when logical short-circuiting determines the result early.

> [!NOTE]
> **Intentionally Omitted C Preprocessor Features:**
> Function-like macros, stringification (`#`), token pasting (`##`), backslash line continuations, binary arithmetic and bitwise operators (`+ - * / & | ^ << >>`), integer suffixes (`ULL`), character constant arithmetic, angle-bracket system includes (`<...>`), and implicit host environment variable lookups are **not** supported. Unsupported active directives or operators fail explicitly. Inactive conditional branches do not execute `#include` directives or evaluate nested `#if` expressions, but must still contain lexically valid UTF-8 tokens.

## 3. Explicit file access

Filesystem access during preprocessing is strictly opt-in:
- **Text entry points disable file access by default**: `hx_os_preprocess_text()` has no default filesystem resolver, so encountering an active `#include` returns `HX_OS_NOT_SUPPORTED` unless you supply a custom resolver in `options`.
- **File entry points provide a relative-path resolver**: `hx_os_preprocess_file()` resolves `#include` paths relative to the **directory containing the including file**, handling UTF-8-to-UTF-16 path conversion on Windows internally.
- **Built-in resolver constraints**: The built-in file resolver rejects absolute paths, colon (`:`) drive/scheme syntax, and `..` parent-directory components, while accepting both `/` and `\` separators and following filesystem symlinks. Note that this is a bounded path check for trusted configuration files, not an OS security sandbox. Pass `HX_OS_PREPROCESS_NO_INCLUDES` in `options.flags` to disable `#include` even when a resolver is present.
- **Custom resolvers**: To load included fragments from an in-memory resource table, archive, or allowlisted file map, register a custom resolver callback in `options`. The callback receives the parent source identifier, the requested include name, and the remaining byte budget, populates an initialized `hx_string`, and outputs a non-empty resolved identifier up to `4,096` bytes long (used for include cycle detection).

## 4. C++11 and diagnostics

In C++11, include `<hx/Preprocessor.hpp>` and call `hx::Preprocessor::file()` or `hx::Preprocessor::text()`, passing an optional `hx::PreprocessDiagnostic` to capture 1-based line and column error locations:

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

- In C, initialize an `hx_os_preprocess_diagnostic` with `HX_OS_PREPROCESS_DIAGNOSTIC_INIT` and release it with `hx_os_preprocess_diagnostic_destroy()`. On failure, it records the source name, 1-based line and column numbers, and a static English `description`.
- **Whitespace and token separation**: Expanding macros and splicing `#include` files may insert spaces or blank lines so adjacent tokens never accidentally fuse together. When feeding preprocessed text into a custom lexer that treats newlines as whitespace, enable `HX_OS_LEXER_SKIP_NEWLINES`; standard JSON, YAML, and TOML parsers handle this whitespace automatically.

## 5. Limits and ownership

To guard against recursive macro loops, exponential macro expansion ("Billion Laughs"), or deep include chains, every preprocessing call enforces explicit resource budgets (initialize with `HX_OS_PREPROCESS_OPTIONS_INIT`; setting any limit to `0` is rejected with `HX_OS_INVALID`):

| Option field | Default bound | Maximum configurable ceiling |
| --- | --- | --- |
| `max_input_bytes` per source | `16 MiB` | `64 MiB` |
| `max_total_input_bytes` across all sources | `64 MiB` | `64 MiB` |
| `max_output_bytes` final output size | `16 MiB` | `64 MiB` |
| `max_defines` active macro definitions | `256` | `4,096` |
| `max_macro_bytes` retained macro names/bodies | `1 MiB` | `64 MiB` |
| `max_expanded_bytes` cumulative replacement bytes | `64 MiB` | `64 MiB` |
| `max_expansions`, `max_directives` | `65,536` each | `1,048,576` each |
| `max_include_depth` (counting root source as 1) | `16` | `64` |
| `max_expansion_depth` nested macro depth | `32` | `64` |
| `max_condition_depth` per source | `64` | `64` |

- **Per-call isolation**: Predefined macro names and values passed in `options` are borrowed only for the duration of the call and copied into the invocation's private macro table. Concurrent calls never share macro tables or mutate process-global state.
- **Feature toggles**: `HX_OS_ENABLE_PREPROCESSOR` defaults to `ON` and requires `HX_OS_ENABLE_LEXER` and `HX_OS_ENABLE_PARAMETERS` (plus `HX_OS_ENABLE_FILE_UTILS` for `hx_os_preprocess_file`). Query `hx_os_preprocess_available()` at runtime; when disabled, preprocessing functions return `HX_OS_NOT_SUPPORTED`.
