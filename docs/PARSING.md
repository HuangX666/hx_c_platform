# Parsing helpers for runtime text

[简体中文](PARSING.zh-CN.md) · [README](../README.md)

**Start with a complete program.** Use Lexer helpers when a custom configuration has required fields and optional extension blocks to skip.

Open [parse_script.c](../examples/parse_script.c). Create lexer → read_title checks title, equals, string and semicolon → skip_group skips extensions → require END → destroy.

Follow the [example run instructions](../examples/README.md#run), enable examples and build only `hx_os_parse_script`. This program needs no arguments or input files.

**Expected result:** The built-in input produces `Title: Camera`. Replace the input and write a reader for each required field. This does not alter C/C++ source syntax.

Short snippets below explain individual calls; use the complete example when running them. You can also use its source as main in the [minimal project](USAGE.md#c-project).

For constants, conditional branches and bounded includes before parsing, see [runtime preprocessing](PREPROCESSING.md).

## 1. From tokens to a small grammar

The lexer splits UTF-8 input into identifiers, numbers, quoted strings, punctuation and optional whitespace/comments. The parsing helpers check or skip those tokens so each application need not repeat the same comparisons and bracket bookkeeping. They process runtime text, such as a configuration string or file contents. They do not add language syntax to C/C++ source files or require a pre-build generator.

For example, this input has a required title and an extension block that the application may ignore:

```text
title = "Camera";
extensions { nested [call("}")]; /* ignored */ }
```

The `"}"` string does not close the extension block. The parser skips complete nested groups while respecting strings, comments and bracket types.

## 2. Run a complete C program

[parse_script.c](../examples/parse_script.c) reads the title, skips the extension block and checks that no trailing tokens remain:

```sh
cmake --build build --config Release --target hx_os_parse_script
# Linux / macOS:
./build/bin/hx_os_parse_script
# Windows / Visual Studio:
./build/bin/Release/hx_os_parse_script.exe
```

Expected output is `Title: Camera`. The example enables `HX_OS_LEXER_SKIP_NEWLINES` so line breaks act as whitespace. All parsing helpers otherwise use the whitespace/comment options selected when creating the lexer.

| Task | C entry | C++11 entry |
| --- | --- | --- |
| Compare a token without moving | `hx_os_token_is` | Compare the owned token's text |
| Consume optional exact text | `hx_os_lexer_accept` | `lexer.accept(";")` |
| Require and consume exact text | `hx_os_lexer_expect` | `lexer.expect("title")` |
| Require a token kind | `hx_os_lexer_expect_kind` | `lexer.expectKind(HX_OS_TOKEN_NUMBER)` |
| Decode and consume a quoted string | `hx_os_lexer_read_string` | `lexer.readString()` |
| Skip a complete bracket group | `hx_os_lexer_skip_group` | `lexer.skipGroup()` |
| Skip through an exact token | `hx_os_lexer_skip_until` | `lexer.skipUntil(";")` |

Exact text means the raw bytes of a single token. Identifier comparison is case-sensitive. To retrieve the decoded contents of a quoted string, use `read_string` rather than comparing its raw quotes/escapes. The lexer emits one punctuation character per token; `==` is two `=` tokens, not one combined operator. These helpers do not silently change the existing token grammar.

## 3. C++11 calls

Include `<hx/Lexer.hpp>`. This fragment belongs in a function returning `hx_os_status`; include `<cstdio>` and return `HX_OS_OK` after the successful path:

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

`readString()` returns an owning `String`; closing the lexer does not invalidate it. `native()` borrows the C handle for advanced calls. C token outputs borrow the lexer's immutable input until the lexer is destroyed. C++ `next()` retains its existing independently owned `Token` behavior.

`accept()` returns `Result<bool>`. Check `ok()` first, then `value()` to learn whether a token matched. Successful no-match is `ok()==true, value()==false`; checking only the result's truth value checks for an error, not a match. An inert/moved-from C++ lexer reports `HX_OS_CLOSED` for these operations.

## 4. Failure and progress

| Operation/result | Cursor and output |
| --- | --- |
| `accept` matches | Consumes the token; `matched=1` |
| `accept` does not match, including EOF | Returns OK with `matched=0`; no movement |
| `expect` mismatches | `HX_OS_SYNTAX`; no movement or token-output change |
| `expect` / `read_string` unexpectedly reaches EOF | `HX_OS_TRUNCATED_INPUT` |
| `expect_kind(END)` at EOF | Succeeds with END, without further progress |
| `read_string` allocation/decoding failure | Preserves the output string and cursor |
| `skip_group` mismatched closing bracket or excessive depth | Reports SYNTAX/LIMIT after any consumed prefix; the offending bracket remains next |
| `skip_group` lacks a closing bracket | Reports TRUNCATED_INPUT after the consumed prefix |
| `skip_until` finds its token | Consumes through and including that token |
| `skip_until` cannot find its token | Returns EOF after scanning the input |

Group skipping accepts `()`, `[]` and `{}` with matching nested types. Its default depth bound is 64; `hx_os_lexer_skip_group_with_limit` or C++ `skipGroup(limit)` accepts 1–256. It uses a fixed 256-byte bracket stack and does not recurse or allocate. Existing input, token-size and token-count limits still apply. Flat `skip_until` is not a replacement for bracket-aware skipping and can stop at a token inside a nested group.

After an expectation mismatch, `peek` reports the next token's position without consuming it. After a failed group skip, reset or recreate the lexer to restart from the beginning; changing only the depth limit does not reconstruct the skipped outer groups. Do not use a retry loop that assumes every successful END assertion makes progress.

Except for decoding into an owned string, these helpers do not allocate. They introduce no worker or lock. Serialize operations on one lexer and do not reenter/mutate/destroy the lexer or destination string from its allocation callbacks. Disabling `HX_OS_ENABLE_LEXER` makes lexer-dependent operations explicitly unsupported; raw token comparison remains a stateless byte comparison.
