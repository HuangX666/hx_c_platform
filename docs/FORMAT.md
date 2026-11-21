# Formatting and printing

[简体中文](FORMAT.zh-CN.md) · [README](../README.md)

When building log lines, status messages, or CLI output in C99 and C++11, traditional `printf` calls make it easy to mismatch format specifiers and argument types, and cannot reuse arguments by position or name. This module brings brace-style UTF-8 formatting (`{}`, `{:.2f}`, `{name}`) and cross-platform terminal printing to C99 and C++11 with explicit type checking, zero C++ exceptions, and native UTF-8 console support on both Windows and POSIX.

To see it in action, check out [format.c](../examples/format.c). The program packs typed arguments with `hx_fmt_str()` and `hx_fmt_f64()`, formats them through `hx_print_args()` using `"{}: {:.2f}"`, and checks the returned status. Follow the [example run instructions](../examples/README.md#run) to enable examples and build the `hx_os_format` target (it takes no command-line arguments or input files):

```text
张三: 3.75
```

You can replace the sample literals with your own variables or drop the example source into the [minimal project](USAGE.md#c-project) as `main.c`. The sections below walk through the available calling styles and format specifiers.

## 1. Choose a calling style

All four styles below share the same underlying C99 UTF-8 formatting engine and never throw C++ exceptions. The CMake option `HX_OS_ENABLE_FORMAT` defaults to `ON`; when disabled, runtime formatting functions return `HX_OS_NOT_SUPPORTED` and `HX_OS_CAP_FORMAT` is omitted from the capability bitmask.

| Style | Example | Build requirements |
| --- | --- | --- |
| C99 convenient typed macros | Include `hx_print.h` and call `HX_PRINTLN("{}", hx_fmt_i64(count))` | Standard C99 compiler; no code generator |
| C99 explicit argument array | Pack `hx_fmt_i64(count)` into an `hx_format_arg` array | Standard C99 compiler; no code generator |
| C99 automatic argument types & inline expressions | Include `hx_format_syntax.h` and call `HX_PRINTLN("{}", count)` | Enable once per target in CMake; requires host Python 3.10+ and Clang |
| C++11 facade | Include `<hx/Format.hpp>` and call `hx::println("{}", count)` | Standard C++11 compiler |

> [!NOTE]
> `std::format` requires C++20 and `std::print` requires C++23. This module provides a well-defined brace-formatting subset tailored for C99 and C++11 projects (see Section 5), rather than a drop-in clone of the full C++20 or Python format specification.

## 2. C: write expressions directly

If you want to embed C variables and arithmetic expressions directly inside format string literals—or let the build deduce argument types automatically—include `<hx_format_syntax.h>`. The complete [format_syntax.c](../examples/format_syntax.c) example prints `张三: 18, total=3.75` using:

```c
#include <hx_format_syntax.h>

// user.name is "张三", user.age is 18; price is 1.25, count is 3.
hx_os_status status = HX_FPRINTLN(
	"{user.name}: {user.age}, total={price * count:.2f}");
```

During the build, `HX_FPRINTLN` extracts the C expressions from the literal: `user.name` reads the struct member, `price * count` performs standard C multiplication, and `:.2f` formats the floating-point product to two decimal places. Always check `status == HX_OS_OK` to confirm that formatting and output succeeded.

You can also pass arguments explicitly after the format string:

```c
HX_PRINTLN("{}: {:04d}", user.name, count);
HX_PRINTLN("{name}: {count}", HX_NAMED("name", user.name),
	HX_NAMED("count", count));

hx_string text = HX_STRING_INIT;

hx_os_status status = HX_FFORMAT(&text, "Total: {price * count:.2f}");

// Use text only after checking status == HX_OS_OK; always destroy it when done.
hx_string_destroy(&text);
```

- `HX_FORMAT(&text, pattern, args...)` and `HX_FFORMAT(&text, pattern)` write the formatted result into an initialized `hx_string`.
- `HX_PRINT` writes without a trailing newline, `HX_PRINTLN` appends `\n` (LF), and `HX_EPRINTLN` writes to `stderr`.
- The `F`-prefixed macros (`HX_FPRINT`, `HX_FPRINTLN`, `HX_FEPRINTLN`, `HX_FFORMAT`) extract C expressions from string literals at build time; they never inspect runtime strings to look up local variable names.

To enable this syntax on a CMake target, call `hx_enable_format_syntax()` after configuring the target's sources, linked libraries, include directories, and compiler flags:

```cmake
cmake_minimum_required(VERSION 3.16)
project(format_demo LANGUAGES C)

find_package(hx_c_platform CONFIG REQUIRED)

add_executable(format_demo main.c)
target_link_libraries(format_demo PRIVATE hx_c_platform::hx_c_platform)
set_target_properties(format_demo PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO)

if(MSVC)
	target_compile_options(format_demo PRIVATE /utf-8)
endif()

hx_enable_format_syntax(format_demo SOURCES main.c)
```

`hx_enable_format_syntax()` works whether you consume the library via `find_package(hx_c_platform)` or `add_subdirectory()`. Build normally with `cmake --build build --config Release`—no manual code generation step is needed. If `clang` is not on your host `PATH`, pass `-DHX_FORMAT_CLANG=/path/to/clang` at configure time or specify the `CLANG` argument in `hx_enable_format_syntax()`. Use `PYTHON` to select a specific host Python interpreter and `CLANG_OPTIONS` to pass extra parser flags such as custom toolchain system includes.

```mermaid
flowchart LR
    S[Original C source] --> P[Host Clang checks types and expressions]
    P --> G[C99 source in build directory]
    G --> C[MSVC, GCC or Clang compiles the target]
    C --> R[C99 library on target device]
```

Your original source files in the working tree are never modified. Each build configuration writes its generated C99 sources into the build directory, inheriting the target's include paths, preprocessor definitions, and compile options (including transitive dependency requirements). Generation only re-runs when the source file, included headers, or generator script change; unchanged builds preserve timestamps. If type or format validation fails, the build stops immediately without overwriting the last good generated file. Installed packages include the CMake helper and generator script, while the target device running the compiled binary needs neither Python nor Clang.

> [!IMPORTANT]
> **Generator Constraints and Expression Semantics:**
> - **Target-level settings**: Pass direct `.c` sources to `hx_enable_format_syntax()` once per target, and keep compile flags at the target level. Per-source compile properties are rejected with an explicit error.
> - **Supported call sites**: Format markers inside macro definitions, macro-expanded function bodies, global initializers, or nested format calls are not supported (wrapping a top-level format call in an ordinary `assert`-style macro is fine).
> - **Cross-compilation & frontend consistency**: For cross-builds, host Clang must understand the target triple, sysroot, and compiler extensions used by your headers (on MSVC, `_MT`, `_DLL`, and `_DEBUG` are forwarded automatically). If your code branches on compiler identity (`__clang__` vs. `__GNUC__`), make sure both the analysis pass and the final compiler see identical type definitions—or use the generator-free C99 macros in Section 3. If you change include paths solely via environment variables, clean and rebuild the generation cache.
> - **Evaluation order & lifetimes**: Inline expressions obey C scope and semantics at the call site. Parenthesize ternary expressions (`?:`) inside braces so `:` is not parsed as a format specifier delimiter. Output and pattern expressions are evaluated first, followed by argument expressions once each in source order. Registered custom structs are copied into call-local stack storage, while any pointer members inside them remain borrowed.

## 3. C99 without a build tool

If you prefer not to require Python or Clang on build machines, include `<hx_print.h>`. Your CMake setup stays a standard `add_executable` + `target_link_libraries` build, and you tag each argument with a lightweight `hx_fmt_*` wrapper:

```c
#include <hx_print.h>

int main(void)
{
	hx_os_status status = HX_PRINTLN("{}, {:04d}",
		hx_fmt_str("HuangX"), hx_fmt_i64(2000));

	return status == HX_OS_OK ? 0 : 1;
}
```

`<hx_print.h>` provides `HX_PRINT`, `HX_PRINTLN`, `HX_EPRINTLN`, and `HX_FORMAT(&text, pattern, ...)`, all returning `hx_os_status`. Argument-free calls like `HX_PRINTLN("ready")` work out of the box. Every value argument must be an `hx_format_arg` constructed via `hx_fmt_*`: pass named arguments as `HX_NAMED("name", hx_fmt_str("HuangX"))` and custom structs as `hx_fmt_custom(&object, format_callback)`.

- **Argument count & stack storage**: The C99 macros accept **0–64 value arguments** (excluding the destination and format pattern); passing more than 64 produces a compile-time error. A single argument can be referenced any number of times via `{0}`, and escaped braces (`{{` / `}}`) consume no arguments. The macro builds a temporary argument array on the stack with no heap allocation of its own. Each argument expression is evaluated once under standard C evaluation rules—do not rely on left-to-right ordering of side effects across arguments.
- **Mutually exclusive headers**: Include either `<hx_print.h>` (explicitly typed C99 calls) or `<hx_format_syntax.h>` (build-time generated types/expressions) in a single translation unit, never both. `<hx_os.h>` does not implicitly include either macro header.

When you need dynamically constructed argument arrays, more than 64 arguments, custom memory/output bounds, or a detailed `hx_os_error` diagnostic, call the underlying runtime functions directly (as shown in [format.c](../examples/format.c)):

```c
hx_format_arg args[] = {hx_fmt_str("张三"), hx_fmt_f64(3.75)};

hx_os_error error;

hx_os_status status = hx_print_args(HX_PRINT_STDOUT, 1,
	hx_string_view_from_cstr("{}: {:.2f}"), args, 2, &error);
```

Because `hx_fmt_*` wrappers record the exact type in `hx_format_arg`, the runtime never guesses `va_list` promotions. Wrappers are provided for signed and unsigned 64-bit integers, `float` / `double` / `long double`, booleans, Unicode scalar code points (`hx_fmt_char`—pass a decoded code point, not an individual byte of a multibyte UTF-8 sequence), UTF-8 string views, object pointers, named arguments, and custom formatter callbacks.

For string destinations:
- `hx_format()` writes into an `hx_string` with default runtime limits of **1,024 arguments** and **16 MiB of UTF-8 output** (including any trailing LF added by print functions).
- `hx_format_with_options()` lets you customize `max_arguments` and `max_output_bytes`.
- `hx_format_buffer()` writes into a caller-supplied buffer and reports the required capacity (including the trailing NUL). If the buffer is too small, it returns `HX_OS_OUTPUT_FULL` without modifying the buffer (pass `buffer = NULL` and `capacity = 0` to query the required size).
- Missing positional arguments return `HX_OS_RANGE`, and exceeding argument or output limits returns `HX_OS_LIMIT`. Traditional `printf`-style `hx_string_format()` remains available as well.

## 4. C++11

In C++11, include `<hx/Format.hpp>` to format native types directly without wrapper macros. All calls return `hx::Result<T>`:

```cpp
#include <hx/Format.hpp>

auto printed = hx::println("{}: {:.2f}", "张三", 3.75);
if (!printed)
	return 1;

auto text = hx::format("Answer: {:04d}", 42);
if (!text)
	return 1;
// text.value() owns "Answer: 0042".
```

- `hx::format(pattern, args...)` returns an owning `hx::Result<hx::String>`, while `hx::formatTo(existingString, pattern, args...)` writes into `existingString` using its bound memory context.
- `hx::print`, `hx::println`, and `hx::printTo` return `hx::Result<void>`.
- `hx::named("name", value)` borrows `value` for the duration of the call; never store a named argument that binds to a temporary object.
- Object pointers and enums work out of the box (format function pointers by converting them to an explicit application representation first). To support custom C++ types, specialize `hx::Formatter<T>` with a `noexcept` static `format` method. Unsupported types fail at compile time, while format specifier compatibility is verified at runtime.

## 5. Format rules

| Pattern | Meaning |
| --- | --- |
| `{}`, `{0}`, `{name}` | Automatic, positional, or named replacement field (do not mix automatic `{}` and numbered `{0}` fields in the same pattern) |
| `{{`, `}}` | Literal `{` or `}` |
| `{:>8}`, `{:*^8}`, `{:<8}` | Right-aligned, centered with `*` fill, or left-aligned |
| `{:+08d}`, `{:#x}`, `{:b}`, `{:o}` | Sign and zero-padding, `0x` hex prefix, binary, or octal |
| `{:.2f}`, `{:.3e}`, `{:.6g}`, `{:a}`, `{:.1%}` | Fixed-point, scientific, significant digits, hex float, or percentage |
| `{:,.2f}`, `{:_d}` | Group decimal integer digits with `,` or `_` |
| `{:.3s}`, `{:?}`, `{!r}` | Truncate text length, quoted/escaped debug text, or string representation |
| `{0:>{1}}`, `{value:.{precision}f}` | Dynamic integer width or precision supplied by another argument |

> [!NOTE]
> **Unicode Scalar Counting and Floating-Point Behavior:**
> - **Unicode code point counts**: Field widths and string truncation precisions (such as `{:.3s}`) count Unicode scalar values (code points), not terminal column widths or grapheme clusters. As a result, CJK characters and combining marks are not guaranteed to align visually in monospaced terminal tables. Input strings must be valid UTF-8; malformed UTF-8 fails with an error rather than inserting replacement characters. Explicit `hx_string_view` values may contain embedded NUL bytes, but format specifiers themselves cannot contain NUL. Booleans format as `true` / `false` by default (or with `:s`), and as `1` / `0` with numeric specifiers.
> - **Locale-independent floats**: Floating-point formatting always uses C-locale rules without mutating the process-global locale. If a target platform lacks the required locale-independent conversion primitive, floating-point formatting returns `HX_OS_NOT_SUPPORTED`. Default float formatting searches for a significant-digit representation that round-trips back to the input type (which may choose fixed vs. scientific notation at slightly different thresholds than `std::format`). Precision is capped at `4096`.

## 6. Custom structures and failure contracts

In C, you can register or pass a custom formatting callback for your own structs:

```c
static hx_os_status format_user(const void *object, hx_string_view spec,
	hx_string *out, size_t limit, hx_os_error *error);
HX_FORMATTER(user_info, format_user);
```

- When using `hx_enable_format_syntax`, place `HX_FORMATTER(type_name, callback)` in the compiled source file after declaring both the struct type and callback. Without the generator, pass `hx_fmt_custom(&user, format_user)` at each call site.
- The callback borrows `object` for the synchronous call, parses its custom `spec`, appends valid UTF-8 to `out` using `out`'s allocator context, and must respect `limit` before allocating memory. In C++11, specialize `hx::Formatter<T>::format` with a typed `const T &` first parameter and an `hx::StringView` specifier.

**Transactional Output and Error Diagnostics:**
- **Unchanged destination on failure**: Formatting always assembles output in temporary storage and commits to the destination only on complete success. On syntax errors (`HX_OS_SYNTAX`), type mismatches (`HX_OS_TYPE_MISMATCH`), invalid UTF-8, allocation failure (`HX_OS_NO_MEMORY`), or budget exhaustion (`HX_OS_LIMIT`), the destination string remains untouched—even when arguments borrow bytes from the destination string itself.
- **Byte-offset error reporting**: When format string parsing or field validation fails without an underlying OS error domain, `hx_os_error.cause_code` records the byte offset of the offending field in the format string. Native error domains reported by custom callbacks are preserved.

**Terminal and Stream Printing Behavior:**
- Print functions finish formatting the complete string in memory before writing to the output stream, so a formatting error never writes partial output.
- On Windows, writing to a console handle converts the text to UTF-16 and calls `WriteConsoleW` directly (without changing the console code page), while redirected files and pipes receive raw UTF-8 without a BOM.
- On POSIX, writes automatically retry on `EINTR` and suppress `SIGPIPE` locally within the calling thread if a pipe breaks, without installing a process-global signal handler. Because `stdout` / `stderr` writes are synchronous and may block, do not treat them as non-blocking coroutine I/O.
