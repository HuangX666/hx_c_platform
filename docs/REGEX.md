# UTF-8 regular expressions

[简体中文](REGEX.zh-CN.md) · [README](../README.md)

When validating semi-structured text (such as device IDs, log lines, or key-value records), extracting named capture groups, or performing template replacements, hand-written character scanners become brittle while unbounded backtracking regex engines risk catastrophic backtracking (ReDoS). This module provides a first-party C99 UTF-8 regular expression engine with explicit compile-time and runtime step, memory, and deadline budgets—requiring no external regex library in production (note: for structured formats like JSON or YAML, use their dedicated parsers instead).

To see the full workflow, open [regex.c](../examples/regex.c). The program compiles a pattern with named capture groups, extracts the `device` and `request` byte spans from `"device=Door_01 request=42"`, formats a replacement string (`"Door_01: #42"`), and destroys the compiled object. Follow the [example run instructions](../examples/README.md#run) to enable examples and build `hx_os_regex` (it needs no command-line arguments or input files):

```text
Device: Door_01
Request: 42
Door_01: #42
```

When matching the same pattern repeatedly, compile it once and reuse the compiled object across calls—and always distinguish a normal no-match from an exhausted execution budget. You can also drop the example source into the [minimal project](USAGE.md#c-project) as `main.c`.

## 1. Match a whole value

If you only need a one-shot check to see whether an entire string matches a pattern, call `hx_os_regex_matches()`. Save the complete program below as `main.c` and build it using the [minimal project](USAGE.md#c-project) setup. It checks whether `"AB1234"` matches two uppercase ASCII letters followed by four digits (`[A-Z]{2}[0-9]{4}`), printing `Valid` for `"AB1234"` and `Invalid` if you change the input to `"A1234"`:

```c
#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	int matched = 0;
	hx_os_status status = hx_os_regex_matches(
		"[A-Z]{2}[0-9]{4}", hx_string_view_from_cstr("AB1234"),
		&matched, NULL);

	if (status != HX_OS_OK) {
		fprintf(stderr, "%s\n", hx_os_status_name(status));
		return 1;
	}

	puts(matched ? "Valid" : "Invalid");
	return 0;
}
```

- `hx_os_regex_matches()` compiles, executes, and frees the pattern internally.
- **No-match vs. error**: A valid pattern that finishes executing always returns `HX_OS_OK` and sets `matched` to `1` (matched) or `0` (did not match). Syntax errors (`HX_OS_SYNTAX`) and exhausted runtime budgets (`HX_OS_LIMIT`) return distinct non-`HX_OS_OK` status codes.
- When evaluating the same pattern more than once, precompile it with `hx_os_regex_compile()` as shown in Section 2.
- In C++11, call `hx::Regex::matches(pattern, text)` and check the returned `hx::Result<bool>`. The implementation never uses `try`/`catch`/`throw` or `std::regex`.

## 2. Compile, find captures, replace

The complete [C example](../examples/regex.c) extracts the `device` and `request` fields from `"device=Door_01 request=42"` and rewrites the record into `"Door_01: #42"`. [The C++ example](../examples/regex_cpp.cpp) locates repeated words using a named capture group and backreference.

From a build directory configured per the [dependency guide](DEPENDENCIES.md) with `HX_OS_BUILD_EXAMPLES=ON`, build and run both targets (on Windows with MSVC, add `--config Release` and run the binaries from `bin/Release/`):

```sh
cmake --build build --target hx_os_regex hx_os_regex_cpp
build/bin/hx_os_regex
build/bin/hx_os_regex_cpp
```

Expected output from the C example:

```text
Device: Door_01
Request: 42
Door_01: #42
```

**Ownership and Capture Group Rules:**
- **Thread-safe reuse**: `hx_os_regex_compile()` produces an immutable compiled object that can be shared safely across threads for concurrent matching. Call `hx_os_regex_destroy()` once all callers have finished using it.
- **Borrowed byte spans**: `hx_os_regex_find()` returns byte offsets and lengths referring directly into the subject buffer, so keep the subject string alive and unmodified while inspecting captures.
- **Capture indexing**: Group `0` represents the entire match. Captures that did not participate in the match report `offset = SIZE_MAX`.
- **Named capture lookup**: `hx_os_regex_capture_index()` maps a non-empty capture group name to its numeric index. Passing an empty string `""` or `NULL` returns `HX_OS_INVALID`, and passing an unknown name returns `HX_OS_NOT_FOUND` (both leave the output index untouched). Access unnamed groups directly by numeric index.

In C++11, include `<hx/Regex.hpp>`:

```cpp
auto regex = hx::Regex::compile("(?<word>\\w+)\\s+\\k<word>");
if (!regex.hasValue())
	return 1;

auto match = regex.value().find("hello hello");
if (!match.hasValue())
	return 1;

auto word = match.value().group(1);
if (!word.hasValue())
	return 1;

std::printf("%.*s\n", (int)word.value().size(), word.value().data());
```

Here, `match.value().group(1)` returns an `hx::StringView` borrowing the subject text, `replace()` returns an owning `hx::Result<hx::String>`, and `split()` delivers borrowed slices synchronously on the calling thread.

## 3. Supported syntax

The matching engine is implemented in first-party C99 using a bounded backtracking virtual machine (VM) with explicit heap-managed recursion and call state, protecting the C call stack from deep recursion (PCRE2 is used only in local maintainer differential tests, never as a production dependency).

| Feature category | Examples |
| --- | --- |
| Literal/quoted text, character ranges, negated & POSIX classes | `abc`, `\Q[a]+\E`, `[a-z]`, `[^0-9]`, `[[:alpha:]]` |
| Quantifiers, ordered alternation, greedy & lazy modes | `*`, `+`, `?`, `{2,4}`, `{2,}`, `a\|ab`, `*?`, `+?` |
| Capturing, non-capturing, and named groups | `(abc)`, `(?:abc)`, `(?<name>abc)`, `(?P<name>abc)` |
| Numeric and named backreferences | `\1`, `\g{1}`, `\k<name>`, `(?P=name)` |
| Positive/negative lookahead and bounded lookbehind | `(?=abc)`, `(?!abc)`, `(?<=a{1,3})`, `(?<!abc)` |
| Atomic groups and possessive quantifiers | `(?>a*)`, `a*+`, `a{2,4}+` |
| Whole-pattern recursion and group subroutines | `(?R)`, `(?0)`, `(?1)`, `(?-1)`, `(?+1)`, `(?&name)` |
| Capture/recursion conditions, assertion conditions, and `DEFINE` | `(?(1)a\|b)`, `(?(name)a\|b)`, `(?(R)a\|b)`, `(?(?=a)a\|b)`, `(?(DEFINE)...)` |
| Backtracking control verbs and marks | `(*FAIL)`, `(*F)`, `(*ACCEPT)`, `(*COMMIT)`, `(*PRUNE)`, `(*THEN)`, `(*SKIP)`, `(*MARK:name)`, `(*SKIP:name)` |
| Anchors, word boundaries, and match-start reset | `^`, `$`, `\A`, `\z`, `\Z`, `\G`, `\b`, `\B`, `\K` |
| Inline and scoped flags | `(?i)`, `(?im-s:...)`, `(?x)`, `(?U)`, `(?n)` |
| Unicode general categories, grapheme clusters, and newlines | `\p{L}`, `\p{Nd}`, `\P{Lu}`, `\X`; newline sequence `\R` |

> [!NOTE]
> **UTF-8, Case Folding, and Dialect Rules:**
> - **Byte offsets & strict UTF-8**: Subject strings and patterns must be valid UTF-8 (length-counted embedded `NUL` bytes are allowed), and all reported offsets and lengths are in **bytes**. The default newline is LF (`\n`). Shorthands `\d`, `\w`, and `\s` match ASCII ranges by default and switch to Unicode properties when `UCP` is enabled. Case-insensitive matching (`CASELESS`), `UCP`, `\p{...}`, and `\X` use the configured `utf8proc` provider. Matching never normalizes text implicitly; call [Unicode normalization](UNICODE.md) first when canonical equivalence is required.
> - **Unicode simple case folding**: Caseless matching (`(?i)`) uses locale-independent Unicode simple case folding (`C` and `S` mappings) without expanding a single code point into multiple code points. For example, `(?i)[Z-a]` matches both `'A'` and `'z'`, whereas `(?i)i` does not match `'İ'`. Character ranges `[...]` expand all folded members at compile time rather than merely folding the two endpoints. The pinned `utf8proc 2.12` table is supplemented with the three simple-only mappings from [Unicode 17 CaseFolding.txt](https://www.unicode.org/Public/17.0.0/ucd/CaseFolding.txt).
> - **Subroutine & control verb semantics**: Recursion conditions support `R`, `R0`/`Rn`, and `R&name` (resolving to an existing capture group name first whenever PCRE2 does so). `(*ACCEPT)` closes active captures and immediately accepts the enclosing subroutine, lookaround, or full match (an ordinary atomic group `(?>...)` is not an acceptance boundary). Control mark names accept up to 255 UTF-8 bytes with at most 64 distinct labels per pattern, and global prefix directives must appear at the very start of the pattern. `\K` inside lookarounds is rejected, and case-insensitive POSIX classes preserve PCRE2's distinction between ASCII and UCP modes.
> - **Unsupported PCRE2 features fail explicitly**: This engine does not claim full PCRE2 compatibility. Unicode script/script-extension and binary-property tables, branch-reset groups `(?|...)`, duplicate capture group names, native callouts, JIT compilation, alternative newline conventions, and PCRE2's extended replacement language are outside the supported dialect. Unsupported constructs and unbounded or overly long lookbehinds are rejected with an error—never silently treated as literal text.

## 4. Limits and errors

Both compilation and execution enforce explicit resource budgets to prevent runaway CPU or memory usage:

- **Default compile limits**: Pattern length up to `32 KiB`, compiled bytecode plus character-class/range tables up to `4 MiB`, group nesting depth up to `64`, counted repetitions up to `1000`, fixed lookbehind width up to `255` code points, and at most `64` capture groups. Compile options can raise counted repetitions up to `65,535` and lookbehind width up to `1024`.
- **Compile work ceiling**: Compilation enforces a fixed ceiling of `4,194,304` work units across AST construction, caseless range expansion, width analysis, and instruction emission (group widths are cached so repeated subroutine calls never trigger exponential lookbehind analysis). Exceeding this ceiling returns `HX_OS_LIMIT` without producing a compiled object. Overflowing group reference numbers (including relative index arithmetic) return `HX_OS_RANGE`, and references to nonexistent groups return `HX_OS_SYNTAX`.
- **Default execution limits**: Up to `10,000,000` (10 million) VM steps, `8 MiB` of retained backtracking stack storage, `16 MiB` subject and result size, recursion/scope depth up to `64`, and at most `65,536` delivered matches or split segments. Simple literal, character-class, and dot repetitions share compact repeat states rather than pushing one backtracking frame per character.

To customize execution budgets or attach a timeout deadline for a specific call, use the `*_with_limits` functions:

```c
hx_os_regex_limits limits = HX_OS_REGEX_LIMITS_INIT;
limits.max_steps = 50000000;
limits.deadline = hx_os_deadline_after_ms(200);

hx_os_status status = hx_os_regex_find_with_limits(
	regex, text, 0, &limits, &match, &error);
```

A single `find`, `replace`, or `split` call shares its budget across the entire operation, including retries at successive starting offsets. Deadlines and cancellation tokens are checked cooperatively (not hard real-time), and retrying with a larger budget starts a fresh execution from the beginning.

| Status code | Meaning and caller action |
| --- | --- |
| `HX_OS_OK` | Completed normally: `find` located a match, or `matches` / `full_match` produced a definitive true/false result |
| `HX_OS_NOT_FOUND` | `find` finished searching the subject and found no match; output is unchanged |
| `HX_OS_SYNTAX` / `HX_OS_NOT_SUPPORTED` | Invalid regex syntax, or the pattern uses an unsupported dialect or disabled Unicode feature |
| `HX_OS_RANGE` | Numeric overflow in a quantifier or group reference (including relative group numbering) |
| `HX_OS_LIMIT` | Compile work, VM step count, recursion depth, text size, result count, or memory budget exhausted (**do not treat this as a normal no-match**) |
| `HX_OS_TIMEOUT` / `HX_OS_CANCELLED` | Stopped before reaching a conclusion due to deadline expiration or cancellation |
| `HX_OS_NO_MEMORY` | Heap allocation failed |

When an error occurs, `hx_os_error_message(&error)` provides an English diagnostic along with the offending byte offset. Failed `find`, `full_match`, and `replace` calls leave the caller's output object unchanged, and the library never terminates the process or kills application threads.

## 5. Replace and split contracts

- **Replacement templates (`replace`)**: Supports numeric references (`$0`, `$1`), braced group names or indices (`${name}`, `${1}`), and `$$` for a literal dollar sign. Unmatched capture groups expand to an empty string. The replacement is assembled using `out`'s allocator and committed only on complete success, so the subject and replacement template may safely borrow from `out` during the call.
- **Global empty matches**: During global search or replacement, an empty match (`length == 0`) advances by one Unicode code point before continuing, preventing infinite loops.
- **Splitting (`split`)**: Preserves empty slices between adjacent delimiters and excludes delimiter capture groups from the delivered slices. If splitting stops early due to budget exhaustion or a non-`HX_OS_OK` callback return, it still reports the number of slices delivered prior to stopping. Split callbacks must not destroy the regex object, mutate the subject text, throw C++ exceptions, or `longjmp`.
- **Module configuration**: `HX_OS_ENABLE_REGEX` can be disabled independently in CMake. Basic UTF-8 literal and ASCII-class matching requires no external regex library, whereas Unicode properties, grapheme clusters, and case folding require `HX_OS_ENABLE_UNICODE`. See [platform support](PLATFORMS.md) for target boundaries.
