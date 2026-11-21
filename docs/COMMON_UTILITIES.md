# Common utilities and direct calls

[简体中文](COMMON_UTILITIES.zh-CN.md) · [Home](../README.md)

Everyday tasks—such as in-place ASCII trimming and string splitting, exact 128-bit `a * b / c` unit conversions, invoking named in-process procedures, loading flat translation catalogs, formatting timestamped filenames, and visiting directory entries—are exposed as direct single-call entry points, keeping custom allocator budgets and extended options in `_ex` overloads. The C99 core and optional C++11 facade share the same underlying implementation with zero extra third-party dependencies (C++ methods return `hx::Result<T>`; see [the migration guide](CPP_RESULTS.md)).

To see these utilities working together, open [common_tasks.c](../examples/common_tasks.c): the program trims and splits a string (`hx_string_trim_ascii_in_place` + `hx_string_split`), converts a `1001 / 30000` frame duration into nanoseconds with `hx_os_muldiv_u64`, registers and calls a named procedure (`twice`) and reads its parameter output, runs two periodic ticks on the current thread with `hx_os_repeat`, and releases all resources.

Follow the [example run instructions](../examples/README.md#run) to configure with `-DHX_OS_BUILD_EXAMPLES=ON` and build `hx_os_common_tasks` (no command-line arguments or input files are required). With the default modules enabled, the program prints:

```text
pieces: 3
frame interval: 33366667 ns
procedure result: 42
```

(along with two periodic tick lines). You can also use the example source as `main.c` in the [minimal project](USAGE.md#c-project).

## Choose an operation

| Task | C99 entry point | C++11 entry point |
| --- | --- | --- |
| In-place ASCII edits; replace, split, join | `hx_string_trim_ascii_in_place` / `lower_ascii_in_place` / `replace_all` / `split` / `join` | `hx::String` in-place methods; `hx::Strings` copy helpers |
| Decode an encoded text file to UTF-8 | `hx_os_file_read_text_encoded` | `hx::Files::readText(path, encoding)` |
| Compose an application config path | `hx_os_path_config_file` | `hx::Paths::configFile` |
| Load and overlay translation catalogs | `hx_os_text_lookup_load` / `add_file` / `add_text` | `hx::TextLookup::load` / `addFile` / `addText` |
| Exact 128-bit `a * b / divisor` conversion | `hx_os_muldiv_u64` | `hx::Wide::multiplyDivide` |
| Register and invoke a named procedure | `hx_os_proc_register` / `hx_os_proc_call` | `hx::ProcedureHandler::add` / `call` |
| Format a portable UTC or local filename | `hx_os_filename_generate` / `format_string` | `hx::Calendar::filename` / `localFilename` |
| Sleep until deadline then spin briefly | `hx_os_sleep_until_precise` / `_ex` | `hx::Wait::precise` |
| Run bounded periodic work on the current thread | `hx_os_repeat` / `hx_os_repeat_until` | `hx::Wait::repeat` |
| Tokenize and decode quoted text | `hx_os_lexer_next` / `hx_os_token_string` | `hx::Lexer` / `hx::Token::value` |
| Declare a typed POD vector or queue | `HX_OS_DECLARE_VECTOR` / `HX_OS_DECLARE_QUEUE` | Existing C containers or standard C++ containers |
| Close and release a thread pool in one step | `hx_os_thread_pool_shutdown` / `_ex` | `hx::ThreadPool::shutdownUntil` |
| Create a thread pool with worker/queue counts | Existing `hx_os_thread_pool_options` | `hx::ThreadPool::withThreads` |

Buildable examples: [direct_calls.cpp](../examples/direct_calls.cpp), [settings.c](../examples/settings.c), [common_tasks.c](../examples/common_tasks.c), [thread_pool.cpp](../examples/thread_pool.cpp).

## Strings and encoding

When assembling text repeatedly in a loop, reuse a single `hx_string`: `hx_string_clear()` resets the length to zero while keeping the allocated buffer, and `hx_string_destroy()` frees the buffer when you are finished (call `hx_string_reserve()` up front if the approximate size is known).
- When you already know the byte length, pass an `hx_string_view` to `hx_string_append()`; use `hx_string_append_cstr()` for `NUL`-terminated strings.
- Both append functions validate UTF-8, preserve the existing string on failure, and safely accept input views borrowed from the target `hx_string` itself even when reallocation occurs. Because an `hx_string_view` can contain embedded `NUL` bytes, print arbitrary views using `fwrite()` or `%.*s` with `view.size` rather than `puts()`.

```c
hx_string text = HX_STRING_INIT;
hx_string_parts *parts = NULL;

hx_os_status status = hx_string_assign_cstr(&text, " one,two,中文 ", NULL);
if (status == HX_OS_OK)
	hx_string_trim_ascii_in_place(&text);

if (status == HX_OS_OK)
	status = hx_string_split(&text, hx_string_view_from_cstr(","),
		&parts, NULL);

// Check status before inspecting borrowed parts_at views.
hx_string_parts_destroy(&parts);
hx_string_destroy(&text);
```

Key string, file-decoding, and POD-macro rules:
- **In-place ASCII edits and replacement**: `trim`, `lower`, and `upper` touch only ASCII bytes and leave multi-byte UTF-8 sequences intact (performing no Unicode normalization or full case folding). The `_in_place` C functions require a non-`NULL` initialized `hx_string`, return `void`, and allocate nothing (in C++, `String::trimAscii`, `lowerAscii`, `upperAscii`, and `clear` return `*this` by reference for chaining). `hx_string_replace_all` replaces non-overlapping matches left to right, rejects empty search needles, and preserves the destination string on failure.
- **Splitting (`hx_string_split`)**: `hx_string_parts` owns an internal copy of the input string, so views returned by `hx_string_parts_at` remain valid even after the original `hx_string` is destroyed, until `hx_string_parts_destroy` is called. By default, empty pieces are kept and results are capped at `4096` pieces; `_ex` allows limits between `1` and `65,536`, returning `HX_OS_LIMIT` rather than truncating when exceeded.
- **Encoded file reading (`hx_os_file_read_text_encoded`)**: Reads up to `16 MiB` by default and strictly transcodes the selected encoding into UTF-8. When `HX_OS_ENABLE_BOM=ON`, a matching byte-order mark is stripped and a conflicting BOM returns `HX_OS_INVALID_ENCODING`; when `HX_OS_ENABLE_BOM=OFF`, BOM stripping is skipped and decoded `U+FEFF` is preserved (if a UTF-16LE BOM followed by `0x0000` is ambiguous with UTF-32LE, the caller's explicit encoding wins).
- **Typed POD container macros**: `HX_OS_DECLARE_VECTOR(name, type)` and `HX_OS_DECLARE_QUEUE(name, type)` wrap the bounded container APIs for trivially copyable (POD) types using a lowercase `name` prefix. Do not use POD macros for owning `hx_string`, `hx_variant`, or non-trivial C++ objects; use the standard container `copy`/`drop` callbacks for resource-owning elements.

## Borrowed text and retained settings strings

To avoid allocating and copying strings on hot lookup paths, use these borrowed or reference-counted string views:

| Getter | Lifetime and allocation cost |
| --- | --- |
| `ConfigDocument::stringView()` | Zero allocation; borrows immutable document bytes while the `ConfigDocument` remains alive |
| `CallDataView::stringView()` | Zero allocation; borrows parameter-pack bytes until the pack is mutated or destroyed |
| `hx_os_text_lookup_get_view` / `TextLookup::getView()` | Zero allocation; hits borrow the catalog table until mutated/destroyed, and misses borrow the caller's `fallback` view directly |
| `hx_settings_get_string_ref` / `Settings::stringView()` | Zero heap allocation; retains an atomic reference to an immutable string snapshot that stays valid even if the setting is later updated or `Settings` is closed, until the C lease is released or `SettingStringView` is destroyed |

> [!WARNING]
> A plain `hx::StringView` obtained from a `SettingStringView` borrows that lease and must not outlive the `SettingStringView` object. In C++, the borrowed getters on `ConfigDocument` and `TextLookup` delete rvalue overloads so you cannot accidentally borrow from a temporary owner.

## Immediate updates and directory visits

- **Chained settings and parameter updates**:
  - For runtime settings, use `hx_settings_update_begin`, typed setters, and `hx_settings_update_finish` (or `settings.update().setInt(...).setString(...).finish()` in C++). Each successful setter publishes and notifies immediately; the first error becomes sticky and skips subsequent setters while keeping earlier writes (`finish` reports the sticky status and the count of completed writes—it is not a rollback transaction or batched notification).
  - For stack parameter packs (`hx_os_calldata`), `hx_os_calldata_edit_*` and `CallData::edit()` provide the same sticky-error chaining pattern.
- **Single-level synchronous directory visits (`hx_os_directory_visit` / `Directory::forEach`)**:
  - `hx_os_directory_visit(path, callback, user, error)` opens, iterates, and closes a single directory on the calling thread (reusing one growable buffer for entry names up to `4096` bytes including `NUL`, with a default limit of `65,536` callback invocations; `_ex` accepts a custom `context`, entry limit, and completed-count output).
  - Entry names are unsorted borrowed UTF-8 views valid **only during the callback**. Returning `HX_OS_OK` continues enumeration, returning `HX_OS_EOF` stops cleanly (making the outer call succeed with `HX_OS_OK`), and any other status aborts enumeration and propagates that error. Exceeding the entry limit returns `HX_OS_LIMIT`. Every exit path closes the directory handle and frees the name buffer, and the function works even when `HX_OS_ENABLE_FILE_UTILS` and `HX_OS_ENABLE_FILE_INFO` are disabled.
  - In C++11, `Directory::forEach(path, callback, maxEntries, context)` returns `hx::Result<size_t>` with the number of callbacks started. Your `noexcept` callback may return `void` (continue), `bool` (`true` to continue, `false` to stop cleanly), `hx_os_status`, or `hx::Result<void>`.
  - To search subdirectories recursively with wildcard patterns, use `Directory::findFirstFile` or `Directory::findFiles` / `hx_os_directory_find_files`; see [recursive search](FILE_SEARCH.md).

## Translation catalogs

`hx_os_text_lookup` (wrapped in C++11 by `hx::TextLookup` in `<hx/TextLookup.hpp>`) loads flat `key=value` translation files and overlays custom overrides on top of a base language catalog:

```ini
# language.ini
hello="Hello"
window.title="Main window"
shortcut="First line\nSecond line"
url=https://example.invalid/
```

```cpp
#include <hx/TextLookup.hpp>

hx::Result<hx::String> window_title() noexcept
{
	auto language = hx::TextLookup::load("language.ini");
	if (!language)
		return hx::Failure(language.error());

	auto added = language.value().addFile("custom.ini");
	if (!added)
		return hx::Failure(added.error());

	return language.value().get("window.title");
}
```

Catalog syntax and overlay rules:
- Accepts UTF-8 BOM, `CRLF`/`LF` line endings, `#` comments, and quoted keys or values with JSON escape sequences and UTF-16 surrogate pairs. Unquoted values are trimmed of leading/trailing spaces and tabs, with `#` starting a comment (a `#` inside quotes is kept as literal text). Sections (`[...]`), file includes, and variable interpolation are not used.
- Duplicate keys overwrite earlier values; looking up a missing key returns the key string itself.
- Defaults cap catalogs at `4096` entries and `1 MiB` of combined key/value text. Overlay calls (`add_file` / `add_text`) stage a candidate table first, so syntax, I/O, allocation, or limit errors leave all existing entries untouched. Concurrent read-only lookups are safe while the catalog is not being modified or destroyed.

## Wide arithmetic and timing

```c
uint64_t interval_ns;

hx_os_status status = hx_os_muldiv_u64(1001, 1000000000, 30000,
	HX_OS_ROUND_NEAREST, &interval_ns, NULL);
// On HX_OS_OK: 33366667 ns.
```

- **Exact 128-bit multiply-divide (`hx_os_muldiv_u64` / `hx::Wide::multiplyDivide`)**: Retains the full 128-bit intermediate product `a * b` before dividing by `divisor`, supporting downward (`HX_OS_ROUND_DOWN`), upward (`HX_OS_ROUND_UP`), and nearest (`HX_OS_ROUND_NEAREST`, ties rounding upward) modes. Division by zero returns `HX_OS_INVALID`, and a quotient exceeding `UINT64_MAX` returns `HX_OS_RANGE` without modifying the output variable. Low/high 128-bit primitives (`hx_os_uint128`) are also exposed directly, using compiler 128-bit integers where available and portable 32-bit limb arithmetic elsewhere—with zero heap allocation, locks, or floating-point rounding error.
- **Sleep plus tail spin (`hx_os_sleep_until_precise` / `hx::Wait::precise`)**: Sleeps until shortly before an absolute monotonic deadline and then spins for the final `200 us` (default window; `_ex` allows `0..10 ms`, where `0` performs an ordinary sleep). Passing `HX_OS_DEADLINE_NOW` succeeds immediately, and `HX_OS_DEADLINE_INFINITE` returns `HX_OS_INVALID`. It does not alter global OS timer resolution or thread priorities; on Windows, `QueryPerformanceCounter` independently bounds the spin budget so coarse tick updates cannot cause unbounded spinning (see [Microsoft's clock documentation](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-queryunbiasedinterrupttime)).
- **Bounded synchronous repeat (`hx_os_repeat` / `hx::Wait::repeat`)**: Runs a callback synchronously on the calling thread at a positive millisecond interval for a bounded number of ticks (the first callback fires after one interval, and missed periods coalesce into a single tick). Returning `HX_OS_OK` continues, `HX_OS_EOF` stops cleanly, and any other status aborts and propagates the error. `_until` adds an overall deadline and outputs the number of callbacks started. For asynchronous background timers, use the timer/event-loop module instead.

## Procedures and lifetime

`hx_os_proc_handler` (wrapped in C++11 by `hx::ProcedureHandler` in `<hx/Procedure.hpp>`) lets components register callbacks under string names and invoke them synchronously on the calling thread:

```cpp
#include <hx/Procedure.hpp>

hx::Result<hx_os_proc_registration>
register_double(hx::ProcedureHandler &procedures) noexcept
{
	return procedures.add("double",
		[](hx::CallData &data) noexcept -> hx::Result<void> {
			auto value = data.view().integer("value");
			if (!value)
				return hx::Failure(value.error());
			if (value.value() > INT64_MAX / 2 ||
				value.value() < INT64_MIN / 2)
				return hx::Failure(HX_OS_RANGE);
			return data.setInt("answer", value.value() * 2);
		});
}
```

- **Naming and parameter commit**: Procedure names are case-sensitive strict UTF-8 strings (`1..127` bytes). Registering a duplicate name returns `HX_OS_EXISTS`, and calling an unknown name returns `HX_OS_NOT_FOUND`. Parameters are isolated per call and committed back to the caller only when the callback returns `HX_OS_OK`.
- **Concurrency and bounds**: Callbacks execute outside internal locks so calls from multiple threads can overlap (defaults allow `256` registrations, `64` concurrent calls, and a nesting depth of `16`, all configurable via `options`). Once registration succeeds, any supplied `cleanup` callback is adopted and invoked once outside locks after the last in-flight call finishes.
- **Unregistration and plugin unloading**: `unregister_until` blocks new calls to that procedure and waits for active calls to finish; on timeout, the registration ID remains valid so the caller can retry (waiting on the current call itself or an ancestor call returns `HX_OS_DEADLOCK`). Always unregister procedures or close the handler before unloading a plugin DLL/shared object that owns the callback code or user data.

## Filenames and lexing

- **Portable timestamped filenames**:
  - `hx_os_filename_generate` / `hx_os_filename_format_string` (C++ `hx::Calendar::filename` / `filenameNow`) format UTC timestamps (default pattern `%CCYY-%MM-%DD_%hh-%mm-%ss`).
  - `hx_os_filename_generate_local` / `hx_os_filename_format_local_string` (C++ `hx::Calendar::localFilename` / `localFilenameNow`) use local wall-clock time.
  - All filename formatters sanitize Windows-invalid characters, control characters, trailing spaces/dots, and reserved device names (`CON`, `NUL`, etc.) while keeping valid Unicode characters. Extensions take an optional leading dot plus up to 32 ASCII alphanumeric, `-`, or `_` characters, and the resulting filename is capped at 240 UTF-8 bytes.
- **Local time display and UTC offsets**:
  - For human-readable timestamps without filename sanitization, call `hx_os_wall_time_format_local_string` (C++ `hx::Calendar::local`), which defaults to ISO-style output with an explicit offset such as `2024-03-10T03:00:00-04:00`.
  - `hx_os_wall_time_local_offset_at` / `Calendar::localOffsetAt` return the local offset in **seconds** for a specific Unix-millisecond timestamp along with the DST flag (`0`/`1`/`-1`), whereas the legacy current-offset API continues to return **minutes**.

| Tokens | Meaning |
| --- | --- |
| `%CCYY`, `%Y`; `%YY`, `%y` | Four-digit year; two-digit year |
| `%MM`, `%m`; `%DD`, `%d` | Two-digit month (`01..12`); two-digit day of month (`01..31`) |
| `%hh`, `%H`; `%I`, `%p` | 24-hour hour (`00..23`); 12-hour hour (`01..12`) and English `AM`/`PM` |
| `%mm`, `%M`; `%ss`, `%S` | Two-digit minute (`00..59`); two-digit second (`00..59`) |
| `%a`, `%A`; `%b`, `%B` | Short/full English weekday and month names |
| `%j`; `%u`, `%w` | Day of year (`001..366`); weekday (`%u` Monday=`1`..Sunday=`7`, `%w` Sunday=`0`) |
| `%L`; `%s` | Milliseconds (`000..999`); floored Unix seconds (timezone-independent) |
| `%z`, `%:z` | Signed UTC offset as `+HHMM` or `+HH:MM` (appending seconds when non-integral) |
| `%Z` | Explicit numeric label `UTC` or `UTC+HH:MM[:SS]` (never ambiguous zone abbreviations) |
| `%%` | Literal `%` |

Unknown format tokens return `HX_OS_SYNTAX` and leave the output unchanged. UTC formatting supports proleptic Gregorian years `0001..9999`; local-time conversion follows the host C runtime's supported range (on Windows, `_localtime64_s` conservatively accepts UTC instants from `1970` through `3000`; see [Microsoft's local-time contract](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/localtime-s-localtime32-s-localtime64-s)).

```cpp
// Include <hx/Calendar.hpp>; these are checked like other Result<String> calls.
auto display = hx::Calendar::local(milliseconds);
auto name = hx::Calendar::localFilename(
	milliseconds, "dat", "%Y-%m-%d_%H-%M-%S_%L");
```

- **Lightweight lexer (`hx_os_lexer` / `hx::Lexer`)**:
  - Owns a copy of strict UTF-8 input (accepting a leading BOM and rejecting embedded `NUL` bytes) and emits identifiers, numbers (decimal, fractional, scientific, and hexadecimal), quoted strings, single-character punctuation, and newlines while skipping spaces and comments (block comments do not nest).
  - C token views borrow from the lexer until `hx_os_lexer_destroy`, whereas C++ `hx::Token` objects own their decoded text. Reaching the end of input returns `HX_OS_LEXER_TOKEN_END`.
  - Lexical errors remain sticky until `reset`, and `position` reports the 1-based line and Unicode codepoint column where the failure began. Default bounds are `16 MiB` of input, `1 MiB` per token, and `1,048,576` tokens.

## Trimming and cost

- **CMake feature switches**: `HX_OS_ENABLE_WIDE_MATH`, `HX_OS_ENABLE_PRECISE_WAIT`, `HX_OS_ENABLE_LEXER`, and `HX_OS_ENABLE_PROCEDURES` default to `ON` and can be disabled independently. Filename formatting requires `HX_OS_ENABLE_CALENDAR`; translation catalog loading requires `HX_OS_ENABLE_TEXT_LOOKUP` and `HX_OS_ENABLE_LEXER` (plus `HX_OS_ENABLE_FILE_UTILS` when loading from files); enabling either `HX_OS_ENABLE_SIGNALS` or `HX_OS_ENABLE_PROCEDURES` enables stack parameter packs (`hx_os_calldata`). Disabled modules retain their exported symbols, clear their capability bits, and return `HX_OS_NOT_SUPPORTED`.
- **Allocation and call overhead**: In-place ASCII edits and 128-bit arithmetic allocate nothing; `replace_all` and `join` stage results in a temporary string so failures leave the destination intact; `split` allocates one shared copy of the input text. Calling a registered procedure allocates no context memory, but still scans bounded slots, acquires a lock, and invokes an indirect callback—use it for decoupled module boundaries rather than replacing direct C function calls when the target function is already statically known.

## Explicit debugger breakpoint

`hx_os_debug_break()` in `<hx_os_util.h>` triggers a native debugger breakpoint on the calling thread in both Debug and Release builds (`DebugBreak()` on Windows, `SIGTRAP` on POSIX).
- An attached debugger or custom signal/exception handler can inspect state and resume execution; if no debugger or handler is present, the operating system terminates the process.
- The library never calls `hx_os_debug_break()` internally to report runtime errors; invoke it only where your application deliberately wants to break into a debugger.
