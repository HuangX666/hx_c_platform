# Formatting and printing

[简体中文](FORMAT.zh-CN.md) · [README](../README.md)

## 1. Choose a calling style

These styles use the same C99 UTF-8 formatting engine. None uses C++ exceptions. `HX_OS_ENABLE_FORMAT` defaults ON; when disabled, runtime entries return `HX_OS_NOT_SUPPORTED` and `HX_OS_CAP_FORMAT` is absent.

| Style | Example | Build requirements |
| --- | --- | --- |
| C99, convenient typed calls | `HX_PRINTLN("{}", hx_fmt_i64(count))` from `hx_print.h` | The existing C compiler; no generator |
| C99, no generator | `hx_fmt_i64(count)` inside an argument array | Your normal C compiler |
| C99, automatic argument types | `HX_PRINTLN("{}", count)` | Optional host Python 3.10+ and Clang, enabled once in CMake |
| C++11 | `hx::println("{}", count)` | Your normal C++11 compiler |

`std::format` belongs to C++20; `std::print` belongs to C++23. The interfaces here provide a documented brace-format language, not complete standard-library or Python compatibility.

## 2. C: write expressions directly

The complete [format_syntax.c](../examples/format_syntax.c) prints `张三: 18, total=3.75`. Its important lines are:

```c
#include <hx_format_syntax.h>

/* user.name is "张三", user.age is 18; price is 1.25, count is 3. */
hx_os_status status = HX_FPRINTLN(
	"{user.name}: {user.age}, total={price * count:.2f}");
```

`HX_FPRINTLN` extracts C expressions from the literal. `user.name` reads a field, `price * count` performs ordinary C multiplication, and `.2f` prints two digits after the decimal point. Check `status == HX_OS_OK` before assuming output succeeded.

Explicit argument lists work too:

```c
HX_PRINTLN("{}: {:04d}", user.name, count);
HX_PRINTLN("{name}: {count}", HX_NAMED("name", user.name),
	HX_NAMED("count", count));

hx_string text = HX_STRING_INIT;

hx_os_status status = HX_FFORMAT(&text, "Total: {price * count:.2f}");

/* Use text only after checking status; release it even on failure. */
hx_string_destroy(&text);
```

`HX_FORMAT(&text, pattern, args...)` formats into a string with explicit arguments. `HX_PRINT` writes without a newline, `HX_PRINTLN` adds LF, and `HX_EPRINTLN` targets stderr. The `F` variants parse expressions from a literal; they do not inspect runtime strings for variable names.

Enable the build step on a target after setting its sources, dependencies, include directories and compiler options:

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

The helper is also available through `add_subdirectory` of this repository. Build normally with `cmake --build build --config Release`; there is no separate developer-operated generation command. If Clang is not on PATH, pass `-DHX_FORMAT_CLANG=/path/to/clang` when configuring, or use the function's `CLANG` argument. `PYTHON` selects a host interpreter and `CLANG_OPTIONS` supplies additional parser options, such as a toolchain-specific system include path.

```mermaid
flowchart LR
    S[Original C source] --> P[Host Clang checks types and expressions]
    P --> G[C99 source in build directory]
    G --> C[MSVC, GCC or Clang compiles the target]
    C --> R[C99 library on target device]
```

The original file stays unchanged. Each configuration gets separate generated files. The helper inherits target include directories, definitions and compile options, including dependency usage requirements. It checks source/header/tool changes before reuse; an unchanged build preserves the generated file's timestamp. A failed check stops the build without replacing the previous generated C file. Installation includes the helper and script; the target device needs neither Python nor Clang.

Use direct `.c` sources and target-level compile settings. Source-specific settings are rejected with an actionable error. Markers in macro definitions, generated macro bodies, global initializers or nested marker calls are unsupported; an ordinary assertion wrapper around a direct marker is supported. Call the helper once per target. A cross build needs host Clang capable of parsing the target triple, sysroot and compiler extensions; unsupported dialects fail during analysis rather than receiving guessed types. MSVC runtime selection also supplies `_MT`, `_DLL` and `_DEBUG` to analysis. Header search changes made only through environment variables require rebuilding the generation cache. Application branches based on frontend identity (such as `__clang__` versus `__GNUC__`) must expose the same types to analysis and the final compiler; use shared project definitions or the explicit typed C99 entry if they differ. A second frontend is not a proof of identical behavior for every compiler extension.

Expressions use C scope and C semantics. Parenthesize a ternary expression containing `:` to separate it from the format specifier. Fixed literals check field count, basic types and specs during generation. Unknown members are Clang errors. Argument expressions are evaluated once, in source order; output and pattern expressions are evaluated first. Registered structure values are copied into call-local storage, while their pointer members remain borrowed. Ordinary runtime format strings are checked at runtime. This is not Python expression evaluation, C++ template specialization, or a general compile-time proof of custom formatter behavior.

## 3. C99 without a build tool

Include `hx_print.h` for ordinary C99 calls. Keep the usual `add_executable` and `target_link_libraries`; no `hx_enable_format_syntax`, Python or Clang step is needed. This complete program uses explicitly typed values:

```c
#include <hx_print.h>

int main(void)
{
	hx_os_status status = HX_PRINTLN("{}, {:04d}",
		hx_fmt_str("HuangX"), hx_fmt_i64(2000));

	return status == HX_OS_OK ? 0 : 1;
}
```

`HX_PRINT` omits the LF, `HX_PRINTLN` appends it, `HX_EPRINTLN` writes to stderr and `HX_FORMAT(&text, pattern, ...)` writes to an initialized `hx_string`. A plain call such as `HX_PRINTLN("ready")` also works. All return `hx_os_status`. Parameters must be `hx_format_arg` values constructed with `hx_fmt_*`; ordinary raw values are not inferred. Named values use `HX_NAMED("name", hx_fmt_str("HuangX"))`, and custom objects use `hx_fmt_custom(&object, format_callback)`.

The C99 macros accept **0–64 value arguments**, excluding the format string and destination. More than 64 is a compile-time error. This is not a limit on replacement-field occurrences: `{0}` may be repeated many times using one argument; `{{` and `}}` produce literal braces and consume none. Each argument expression is evaluated once, with ordinary C evaluation-order rules; avoid depending on a left-to-right order of side effects. A temporary argument array, including an internal sentinel, uses automatic storage and does not allocate a separate heap buffer. The formatter itself retains its existing allocations and limits.

Include only one of `hx_print.h` (typed C99 calls) and `hx_format_syntax.h` (generated automatic types/expressions) in a translation unit. Both use the same macro names; mixing them is rejected explicitly. `hx_os.h` does not implicitly select either macro interface. The new macros are for C; C++11 callers use the facade described below. No exported function or public object layout changes.

For dynamic arrays, more than 64 values, custom limits or a full error object, use the existing functions below. Their defaults are **1,024 arguments** and **16 MiB of UTF-8 output**, including any LF added by printing. These are runtime defaults, not a hard 1,024-field ceiling: `hx_format_with_options` can explicitly set `max_arguments` and `max_output_bytes`. Raising them also increases potential work/memory. Missing positional arguments return `HX_OS_RANGE`; exceeding the selected limits returns `HX_OS_LIMIT`. Formatting failures preserve the destination and do not silently truncate.


[format.c](../examples/format.c) is a complete program needing only the C library:

```c
hx_format_arg args[] = {hx_fmt_str("张三"), hx_fmt_f64(3.75)};

hx_os_error error;

hx_os_status status = hx_print_args(HX_PRINT_STDOUT, 1,
	hx_string_view_from_cstr("{}: {:.2f}"), args, 2, &error);
```

The wrappers state the real types explicitly, so the runtime never guesses a `va_list` argument. Available wrappers cover signed/unsigned 64-bit integers, float/double/long double, boolean, Unicode scalar character, UTF-8 view/string, object pointer, named argument and custom formatter. `hx_fmt_char` takes a Unicode scalar; an arbitrary individual byte from a multibyte UTF-8 character is not a whole character.

Use `hx_format` for an `hx_string`, `hx_format_with_options` to set bounds, and `hx_format_buffer` for caller-owned buffers. The last entry reports required capacity including the trailing NUL. `OUTPUT_FULL` leaves the buffer unchanged; a NULL buffer with zero capacity can query the requirement. Existing printf-style `hx_string_format` remains available.

## 4. C++11

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

`formatTo(existingString, pattern, args...)` uses the destination's context. `format` creates an owning `hx::String`; `print`, `println` and `printTo` return `Result<void>`. `hx::named("name", value)` borrows its value until the call completes. Do not store a named argument that refers to a temporary. Object pointers and enums are supported; function pointers need an explicit application representation. `Formatter<T>` specializes application formatting with a `noexcept` static `format` function. Unsupported C++ types fail to compile; format-string/type compatibility is checked at runtime in this C++11 layer.

## 5. Format rules

| Pattern | Meaning |
| --- | --- |
| `{}`, `{0}`, `{name}` | Automatic, positional, named field; do not mix automatic and numbered indexes |
| `{{`, `}}` | Literal braces |
| `{:>8}`, `{:*^8}`, `{:<8}` | Right, centered with `*`, left alignment |
| `{:+08d}`, `{:#x}`, `{:b}`, `{:o}` | Sign/zero padding, hex prefix, binary, octal |
| `{:.2f}`, `{:.3e}`, `{:.6g}`, `{:a}`, `{:.1%}` | Fixed, scientific, significant digits, hexadecimal float, percentage |
| `{:,.2f}`, `{:_d}` | Group decimal digits with comma or underscore |
| `{:.3s}`, `{:?}`, `{!r}` | Truncate text, quote/escape text, string representation |
| `{0:>{1}}`, `{value:.{precision}f}` | Dynamic integer width or precision |

Widths and string precision count Unicode scalar values, not terminal cells or grapheme clusters. Chinese characters and combining marks therefore do not promise visual table alignment. Strict UTF-8 is required; invalid data fails rather than being replaced. Embedded NUL bytes can be represented by explicit views; NUL in a built-in specification is invalid. Character conversion rejects invalid scalars. Boolean default/`s` output is `true` or `false`; numeric specs produce 0 or 1.

Formatting does not change the process locale. Floats use a native C locale; a platform without that required locale facility reports `NOT_SUPPORTED` for floating conversion; default conversion searches for a significant-digit representation that round-trips to the input type. It can choose a different scientific/fixed spelling from `std::format`. Precision is bounded at 4,096. This portable libc path may perform repeated conversion and locale allocation; it is not a high-performance Dragonbox implementation. Integer and string paths do not use floating conversion. No assembly optimization or speedup claim is attached to this module.

## 6. Custom structures and failure contracts

A C callback has the following shape:

```c
static hx_os_status format_user(const void *object, hx_string_view spec,
	hx_string *out, size_t limit, hx_os_error *error);
HX_FORMATTER(user_info, format_user);
```

The registration belongs to the source being generated and names a previously declared type and callback. The callback borrows `object`, formats UTF-8 into `out` using its context, obeys `limit`, and returns an explicit status. Its own spec language is its responsibility. With no generator, use `hx_fmt_custom(&user, format_user)`. [The generated-source test](../tests/test_format_generated.c) contains a complete callback and both invocation styles. C++ follows the same signature through `hx::Formatter<T>::format` with a typed first argument and `hx::StringView` specification.

Formatting copies into temporary output and commits only on success. The destination is unchanged on invalid syntax/type/UTF-8, allocation failure or limit exhaustion, including when arguments borrow its existing bytes. Argument pointers and callback data are borrowed for the synchronous call only. Default limits are 16 MiB of output and 1,024 arguments; print's optional LF counts within that output limit. Callbacks must honor their supplied limit before allocation; final checking cannot prevent an application callback from allocating excessive private memory. No global formatter registry or global allocator is installed.

`hx_os_error` keeps the existing status/native fields. For parser/field failures without a native error domain, `cause_code` identifies the failing field's byte offset. Convenience source markers return status; use the typed runtime entry for a caller-owned error object. Custom callbacks retain their native error domain/details.

Printing formats completely before writing, so a formatting failure emits nothing. Output errors can occur after some bytes have been written; a print call is not an atomic transaction across processes. Windows consoles use UTF-16 `WriteConsoleW`; redirected handles receive UTF-8 without BOM. POSIX writes retry interrupted calls and contain a newly generated `SIGPIPE` within the calling thread. No process-global signal handler, console code-page switch or locale change is installed. stdout/stderr printing is synchronous and can block; do not use it as cancellable coroutine I/O.
