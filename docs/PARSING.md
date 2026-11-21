# Parsing helpers for runtime text

[简体中文](PARSING.zh-CN.md) · [README](../README.md)

When parsing a lightweight custom configuration format, rule script, or DSL that mixes required fields with optional nested extension blocks (such as `{ ... }`), hand-writing character loops, string unescaping, and bracket-depth counters quickly becomes repetitive and error-prone. Built on top of the UTF-8 lexer, this module provides recursive-descent parsing helpers for matching tokens, decoding quoted strings, and skipping balanced bracket groups.

To see a complete working example, open [parse_script.c](../examples/parse_script.c). The program creates a lexer, calls `read_title()` to verify and consume `title`, `=`, a quoted string, and `;`, calls `hx_os_lexer_skip_group()` to skip the entire `extensions { ... }` block, verifies that the stream has reached `HX_OS_TOKEN_END`, and destroys the lexer. Follow the [example run instructions](../examples/README.md#run) to enable examples and build `hx_os_parse_script` (no command-line arguments or input files are required):

```text
Title: Camera
```

Replace the sample input string with your own grammar rules and add reader functions for your required fields, or drop the example source into the [minimal project](USAGE.md#c-project) as `main.c`. If your configuration text also needs `#define` constants, `#if` conditionals, or bounded `#include` files before parsing, pair this module with [runtime preprocessing](PREPROCESSING.md).

## 1. From tokens to a small grammar

The lexer splits runtime UTF-8 text into identifiers, numbers, quoted strings, punctuation, and (optionally) whitespace and comments. The parsing helpers wrap common grammar steps—accepting optional tokens, requiring expected tokens, unescaping string literals, and skipping balanced bracket groups—without altering C/C++ source syntax or requiring a build-time code generator.

Consider an input where `title` is required and `extensions` should be ignored:

```text
title = "Camera";
extensions { nested [call("}")]; /* ignored */ }
```

When `hx_os_lexer_skip_group()` skips the `{ ... }` block, the lexer properly tokenizes string literals and comments, so the `"}"` inside the string literal and any braces inside `/* ... */` comments never prematurely close the outer `extensions` block, while nested `[...]` and `(...)` pairs are verified to balance.

## 2. Run a complete C program

[parse_script.c](../examples/parse_script.c) reads the `title` assignment, skips the `extensions` group, and confirms that no trailing tokens remain:

```sh
cmake --build build --config Release --target hx_os_parse_script
# Linux / macOS:
./build/bin/hx_os_parse_script
# Windows / Visual Studio:
./build/bin/Release/hx_os_parse_script.exe
```

This prints `Title: Camera`. The example enables `HX_OS_LEXER_SKIP_NEWLINES` when creating the lexer so line breaks are treated as skippable whitespace; all parsing helpers honor the whitespace and comment flags configured on the lexer.

| Parsing task | C API | C++11 API (`hx::Lexer`) |
| --- | --- | --- |
| Compare a token without advancing | `hx_os_token_is` | Compare the `.text()` of an existing `Token` |
| Consume an optional matching token | `hx_os_lexer_accept` | `lexer.accept(";")` |
| Require and consume exact token text | `hx_os_lexer_expect` | `lexer.expect("title")` |
| Require and consume a token kind | `hx_os_lexer_expect_kind` | `lexer.expectKind(HX_OS_TOKEN_NUMBER)` |
| Consume and decode a quoted string | `hx_os_lexer_read_string` | `lexer.readString()` |
| Skip a balanced bracket group `()` / `[]` / `{}` | `hx_os_lexer_skip_group` | `lexer.skipGroup()` |
| Skip through a target token | `hx_os_lexer_skip_until` | `lexer.skipUntil(";")` |

> [!NOTE]
> **Token Matching Rules:**
> - **Exact raw token bytes**: `accept`, `expect`, and `skip_until` compare the exact raw bytes of a single token, and identifier comparisons are case-sensitive.
> - **Unescaping quoted strings**: Call `hx_os_lexer_read_string()` to obtain the unquoted, escape-decoded string value rather than inspecting the raw token slice.
> - **Single-character punctuation tokens**: The lexer emits one punctuation character per token (so `==` is tokenized as two consecutive `=` tokens, not a combined operator).

## 3. C++11 calls

In C++11, include `<hx/Lexer.hpp>`. The snippet below can be used inside a function returning `hx_os_status` (returning `HX_OS_OK` at the end of the happy path):

```cpp
auto created = hx::Lexer::create("title = 'Camera'; { [1, 2] }");
if (!created)
	return created.status();

auto &lexer = created.value();
auto status = lexer.expect("title");
if (!status)
	return status.status();

status = lexer.expect("=");
if (!status)
	return status.status();

auto title = lexer.readString();
if (!title)
	return title.status();

status = lexer.skipUntil(";");
if (!status)
	return status.status();

status = lexer.skipGroup();
if (!status)
	return status.status();

std::puts(title.value().c_str());
```

- **Owned vs. borrowed strings**: `lexer.readString()` returns an owning `hx::Result<hx::String>` that remains valid even after the lexer is closed. In C, raw token views borrow from the lexer's immutable input buffer until the lexer is destroyed.
- **Checking `lexer.accept()`**: `accept()` returns `hx::Result<bool>`. Check `result.ok()` first for lexing errors, then inspect `result.value()` to see whether the token matched—a normal non-match returns `ok() == true` with `value() == false` (testing `if (result)` alone only checks that no error occurred, not that the token matched). Calling methods on a moved-from C++ `Lexer` returns `HX_OS_CLOSED`.

## 4. Failure and progress

The table below summarizes how each operation advances the cursor on success and failure:

| Operation and outcome | Cursor position and output behavior |
| --- | --- |
| `accept` matches | Consumes the token, sets `matched = 1`, and returns `HX_OS_OK` |
| `accept` does not match (including at `EOF`) | **Does not advance the cursor**, sets `matched = 0`, and returns `HX_OS_OK` |
| `expect` mismatches | **Does not advance the cursor**, leaves token output unchanged, and returns `HX_OS_SYNTAX` |
| `expect` or `read_string` hits unexpected `EOF` | Returns `HX_OS_TRUNCATED_INPUT` |
| `expect_kind(HX_OS_TOKEN_END)` at `EOF` | Succeeds with `HX_OS_OK` and `END` without advancing further |
| `read_string` fails allocation or escape decoding | Leaves both the destination string and lexer cursor unchanged |
| `skip_group` hits a mismatched closing bracket or exceeds depth | Returns `HX_OS_SYNTAX` or `HX_OS_LIMIT` after any consumed prefix; the offending bracket remains the next token |
| `skip_group` hits `EOF` before closing | Consumes the prefix and returns `HX_OS_TRUNCATED_INPUT` |
| `skip_until` finds the target token | Consumes through and **including** the target token, returning `HX_OS_OK` |
| `skip_until` reaches the end without finding the token | Leaves the cursor at `EOF` and returns `HX_OS_EOF` |

> [!IMPORTANT]
> **Bracket-Aware `skip_group` vs. Flat `skip_until`:**
> - `hx_os_lexer_skip_group()` handles `()`, `[]`, and `{}` with strict nested pairing. The default nesting limit is **64 levels**; use `hx_os_lexer_skip_group_with_limit()` or C++ `skipGroup(limit)` to configure a limit between **1 and 256** using a fixed 256-byte stack buffer (zero heap allocations and no C call-stack recursion).
> - `hx_os_lexer_skip_until()` performs a flat linear scan and does **not** track bracket depth—so `skip_until(";")` will stop at a semicolon inside a nested `{ ... }` block. Always use `skip_group` when skipping bracketed structures.
> - Because `expect` leaves the cursor unmoved on mismatch, you can call `peek` immediately afterward to inspect the unexpected token. After a failed `skip_group`, call `reset` or recreate the lexer if you want to restart from the beginning.

Except for decoding into an owned string in `read_string`, these helpers perform zero heap allocations and use no background threads or locks. When `HX_OS_ENABLE_LEXER` is disabled at build time, lexer-based helpers return `HX_OS_NOT_SUPPORTED`, while stateless `hx_os_token_is()` comparisons remain available.
