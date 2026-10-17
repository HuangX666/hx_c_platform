# UTF-8 regular expressions

[简体中文](REGEX.zh-CN.md) · [README](../README.md)

**Start with a complete program.** Use regex to extract or replace fields in loosely structured text; use a JSON parser for structured JSON.

Open [regex.c](../examples/regex.c). Compile named captures → find device/request → read captured byte spans → replace display text → destroy.

Follow the [example run instructions](../examples/README.md#run), enable examples and build only `hx_os_regex`. This program needs no arguments or input files.

**Expected result:** It prints `Device: Door_01`, `Request: 42` and `Door_01: #42`. Reuse compiled patterns for repeated work and distinguish no-match from exhausted budgets.

Short snippets below explain individual calls; use the complete example when running them. You can also use its source as main in the [minimal project](USAGE.md#c-project).

## 1. Match a whole value

Save the complete program below as `main.c` and build the [minimal project](USAGE.md#c-project). It checks the device ID `AB1234`: `[A-Z]{2}` requires two uppercase ASCII letters and `[0-9]{4}` requires four digits. It prints `Valid`; changing the input to `A1234` prints `Invalid`.

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

The function compiles, executes and releases an expression. A normal no-match returns OK with matched=0; syntax errors and incomplete execution return distinct statuses. For repeated work, compile once.

C++11 uses `hx::Regex::matches(pattern, text)` and checks its Result. No try/catch/throw or std::regex implementation is involved.

## 2. Compile, find captures, replace

The complete [C example](../examples/regex.c) extracts device and request fields from `device=Door_01 request=42`, then replaces the record with `Door_01: #42`. [The C++ example](../examples/regex_cpp.cpp) finds a repeated word with a named backreference.

```sh
cmake --build build --target hx_os_regex hx_os_regex_cpp
build/bin/hx_os_regex
build/bin/hx_os_regex_cpp
```

Run from the library checkout, replacing `build` with your existing build directory. Configure the SDKs using the [dependency guide](DEPENDENCIES.md) and enable `HX_OS_BUILD_EXAMPLES=ON` first. Windows adds `--config Release` and runs `bin/Release/` executables. The expected C output is:

```text
Device: Door_01
Request: 42
Door_01: #42
```

`hx_os_regex_compile` owns an immutable compiled object. `find` returns byte spans borrowing the original subject; keep its storage alive and unchanged. Capture 0 is the whole match. Absent groups have offset SIZE_MAX. Destroy the compiled object after all callers finish.

`hx_os_regex_capture_index` resolves a nonempty capture name to an index. An empty or NULL name returns `INVALID`; an unknown name returns `NOT_FOUND`. Both leave the output index unchanged. Use numeric indices directly for unnamed groups; an empty string does not select them.

C++ convenience:

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

Include `<hx/Regex.hpp>` and `<cstdio>`; put this fragment inside main. The returned view borrows the supplied text. Replace returns an owned hx::String; split delivers borrowed parts synchronously.

## 3. Supported syntax

The matching engine is first-party C99, with an explicitly bounded backtracking VM and iterative runtime call state. PCRE2 is a local comparison oracle, not a production dependency.

| Feature | Examples |
| --- | --- |
| Literal/quoted text, character ranges, negated/POSIX classes | abc, \\Q[a]+\\E, [a-z], [^0-9], [[:alpha:]] |
| Repetition, ordered alternatives, greedy/lazy forms | *, +, ?, {2,4}, {2,}, a\|ab, *?, +? |
| Capturing, noncapturing, named groups | (abc), (?:abc), (?<name>abc), (?P<name>abc) |
| Numeric/named backreferences | \\1, \\g{1}, \\k<name>, (?P=name) |
| Positive/negative lookahead and bounded lookbehind | (?=abc), (?!abc), (?<=a{1,3}), (?<!abc) |
| Atomic groups and possessive repetitions | (?>a*), a*+, a{2,4}+ |
| Recursive whole patterns and group subroutines | (?R), (?0), (?1), (?-1), (?+1), (?&name) |
| Capture/recursion conditions, assertion conditions, definitions | (?(1)a\|b), (?(name)a\|b), (?(R)a\|b), (?(?=a)a\|b), (?(DEFINE)...) |
| Flow control and marks | (*FAIL), (*F), (*ACCEPT), (*COMMIT), (*PRUNE), (*THEN), (*SKIP), (*MARK:name), (*SKIP:name) |
| Anchors, boundaries, match-start reset | ^, $, \\A, \\z, \\Z, \\G, \\b, \\B, \\K |
| Inline/scoped flags | (?i), (?im-s:...), (?x), (?U), (?n) |
| Unicode categories and graphemes | \\p{L}, \\p{Nd}, \\P{Lu}, \\X; newline sequence \\R |

UTF-8 is strict and length-counted, including embedded NUL. Offsets/lengths are bytes. Default newline is LF; \\d/\\w/\\s are ASCII unless UCP is requested. CASELESS, UCP, Unicode properties and grapheme handling use the selected utf8proc provider. Normalization is explicit; matching does not normalize text.

Caseless matching uses default Unicode simple folding (C/S records), not locale-sensitive lowercasing or multi-character expansion. For example, `(?i)[Z-a]` matches `A` and `z`, while `(?i)i` does not match `İ`. Folded range members are collected during compilation; matching never assumes that folding the two endpoints describes the entire range. The pinned utf8proc 2.12 provider is supplemented for the three simple-only mappings in [Unicode 17 CaseFolding.txt](https://www.unicode.org/Public/17.0.0/ucd/CaseFolding.txt).

This is not a claim of full PCRE2 compatibility. Unicode script/script-extension and binary-property tables, branch-reset groups, duplicate capture names, native callouts, JIT, alternate newline conventions and the complete PCRE2 replacement-expression language are outside the current dialect. Unsupported constructs are rejected, never silently interpreted as literals. Unbounded or excessive-length lookbehind is rejected. Refer to the header and the supported table before importing arbitrary external patterns.

## 4. Limits and errors

Compile defaults: pattern 32 KiB, instructions plus character-class/range data 4 MiB, nesting 64, counted repetition 1000, lookbehind 255 code points, at most 64 captures. Compile options can raise the permitted counted repetition up to 65,535 and lookbehind to 1024. Temporary parser storage, capacity overhead and allocator metadata are additional.

An overflowing numeric group reference, including relative-index arithmetic, fails compilation with `RANGE`. A reference to a nonexistent group returns `SYNTAX`. Neither produces a compiled object.

Compilation has a fixed 4,194,304-work-unit ceiling across AST construction, caseless range expansion, width analysis and instruction generation. Exceeding it returns LIMIT and no compiled object. Group widths are cached, so repeated subroutine references do not cause exponential lookbehind analysis. Runtime max_steps does not change this compile ceiling.

Execution defaults: 10 million VM steps, retained backtracking storage 8 MiB, subject/result 16 MiB, recursion/scope depth 64, at most 65,536 delivered results. Stack growth can briefly retain old and new allocations; current captures and scope snapshots are additional bounded scratch. Simple literal/class/dot repetitions share a compact repeat state rather than allocating one frame per character.

```c
hx_os_regex_limits limits = HX_OS_REGEX_LIMITS_INIT;
limits.max_steps = 50000000;
limits.deadline = hx_os_deadline_after_ms(200);

hx_os_status status = hx_os_regex_find_with_limits(
	regex, text, 0, &limits, &match, &error);
```

Use options only when needed. Budgets cover one whole find/replace/split operation, including candidate starts. Deadline/cancellation checks are cooperative, with no hard real-time promise. Repeating a failed operation starts a fresh attempt.

| Status | Meaning |
| --- | --- |
| OK | Completed; find produced a match, full_match produced true/false |
| NOT_FOUND | Completed find with no match; output unchanged |
| SYNTAX / NOT_SUPPORTED | Invalid syntax or an unavailable dialect/provider feature |
| RANGE | Numeric overflow, including relative group references |
| LIMIT | Computation, depth, input, result or memory budget exhausted; not a no-match |
| TIMEOUT / CANCELLED | Operation stopped before a conclusion |
| NO_MEMORY | Allocation failed |

Errors carry an English diagnostic and byte offset through hx_os_error_message. Failed find/full_match/replace preserves output. No process is terminated and no business thread is killed.

## 5. Replace and split contracts

Replace supports numeric dollar references, dollar-braced group names and a doubled dollar for a literal dollar. It constructs an owned result with out's allocator and publishes only on success. Unmatched captures substitute an empty string. Input/replacement may borrow out until publication.

Global empty matches advance one Unicode code point; this defined convenience behavior is not PCRE2's full global-substitution retry protocol. Split includes empty parts, excludes delimiter captures, and reports successful callback count even on failure. Callbacks must not destroy the regex, mutate the subject, throw or longjmp.

REGEX can be disabled independently. Basic UTF-8 exact matching needs no external regex library; Unicode-enhanced modes require UNICODE. See [platform support](PLATFORMS.md) for target boundaries.


Recursion conditions include `R`, `R0/Rn` and `R&name`; existing capture names take precedence where PCRE2 does so. `(*ACCEPT)` closes active captures and accepts its enclosing subroutine/assertion or the whole match; an ordinary atomic group is not an acceptance boundary. Control labels accept up to 255 UTF-8 bytes, with at most 64 distinct labels. Global prefix directives must appear before the pattern body. `\K` inside lookarounds is rejected, matching PCRE2's default restriction. Case-insensitive POSIX classes retain PCRE2's distinction between ASCII mode and UCP mode.
