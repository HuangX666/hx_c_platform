# Common utilities and direct calls

[简体中文](COMMON_UTILITIES.zh-CN.md) · [Home](../README.md)

These interfaces follow OBS's direct-entry/shared-implementation pattern.
No OBS source was copied and no dependency was added. C99 and the optional
C++11 facade share implementations; existing public layouts and calls remain.

## Choose an operation

| Task | C99 | C++11 |
| --- | --- | --- |
| Trim, replace, split, join UTF-8 | hx_string_trim_ascii / replace_all / split / join | hx::Strings |
| Decode a file to UTF-8 | hx_os_file_read_text_encoded | hx::Files::readText(path, encoding) |
| Compose an application config path | hx_os_path_config_file | hx::Paths::configFile |
| Load/overlay translations | hx_os_text_lookup_load / add_file / add_text | hx::TextLookup::load / addFile / addText |
| Exact a*b/divisor conversion | hx_os_muldiv_u64 | hx::Wide::multiplyDivide |
| Invoke one named procedure | hx_os_proc_register / call | hx::ProcedureHandler |
| Format a portable UTC filename | hx_os_filename_generate / format_string | hx::Calendar::filename |
| Sleep then explicitly spin | hx_os_sleep_until_precise / _ex | hx::Wait::precise |
| Bounded periodic work on this thread | hx_os_repeat / repeat_until | hx::Wait::repeat |
| Tokenize and decode quoted text | hx_os_lexer_next / hx_os_token_string | hx::Lexer / Token::value |
| Typed POD vector/queue | HX_OS_DECLARE_VECTOR / HX_OS_DECLARE_QUEUE | Existing C containers or standard C++ containers |
| Close and release a pool | hx_os_thread_pool_shutdown / _ex | ThreadPool::shutdownUntil |
| Select worker/queue counts | Existing pool options | ThreadPool::withThreads |

Buildable examples: [common_tasks.c](../examples/common_tasks.c),
[thread_pool.cpp](../examples/thread_pool.cpp); complete C++ call coverage:
[test_cpp_utilities.cpp](../tests/test_cpp_utilities.cpp).

## Strings and encoding

~~~c
hx_string text = HX_STRING_INIT;
hx_string_parts *parts = NULL;
hx_os_status status = hx_string_assign_cstr(&text, " one,two,中文 ", NULL);
if (status == HX_OS_OK)
	status = hx_string_trim_ascii(&text, NULL);
if (status == HX_OS_OK)
	status = hx_string_split(&text, hx_string_view_from_cstr(","),
		&parts, NULL);
/* Check status before inspecting borrowed parts_at views. */
hx_string_parts_destroy(&parts);
hx_string_destroy(&text);
~~~

Strings are UTF-8 with byte lengths and embedded NUL support. ASCII trim/case
operations leave Unicode unchanged; no normalization or Unicode case folding.
Replacement is nonoverlapping, left-to-right, permits aliased input views,
rejects empty needles, and preserves the destination/count on failure.
Join is transactional and uses the destination context.

Split owns an input copy. Views survive destruction of the original string
until parts_destroy. Defaults retain empty pieces and limit results to 4096;
_ex permits 1..65536. Excess results return LIMIT, never truncation. Mutation
of any individual object requires external synchronization.

Encoded file reads are bounded to 16 MiB and strictly decode the selected
encoding. With BOM enabled, a matching BOM is removed and a conflicting BOM
is INVALID_ENCODING. With BOM disabled, the decoder preserves U+FEFF.
An ambiguous UTF-16LE BOM followed by NUL honors the explicit encoding.
There is no encoding guessing; existing UTF-8 and optional iconv behavior stays.

POD macros wrap the existing bounded containers. Use lowercase prefix names.
Elements must be trivially byte-copyable, excluding owning hx_string values
and nontrivial C++ objects; those still need existing copy/drop callbacks.
Alignment comes from C99 offsetof, without packing. Views expire on mutation.

## Translation catalogs

~~~ini
# language.ini
hello="Hello"
window.title="Main window"
shortcut="First line\nSecond line"
url=https://example.invalid/
~~~

~~~cpp
auto language = hx::TextLookup::load("language.ini");
language.addFile("custom.ini");
std::string title = language.get("window.title");
~~~

Flat key=value catalogs accept UTF-8 BOM, CRLF/LF, # comments and quoted keys
or values with JSON escapes and surrogate pairs. Unquoted values are literal
text trimmed of surrounding spaces/Tabs; # starts a comment. Quoted # is
retained. Apostrophes inside words and numeric-looking text are ordinary text.
No sections, includes, interpolation or process locale changes. Existing INI
config parsing is unchanged.

Later duplicate keys win; a missing key returns itself. Default limits are
4096 entries and 1 MiB combined key/value text, also bounding file input.
An overlay builds a candidate; syntax/I/O/allocation/limit failures retain
every old entry. Old table, candidate and input can coexist; metadata is
additional. Quoted strings share the lexer decoder.
Concurrent immutable reads are allowed; mutation/loading/destruction requires
host exclusion. No global language state is introduced.

## Wide arithmetic and timing

~~~c
uint64_t interval_ns;
hx_os_status status = hx_os_muldiv_u64(1001, 1000000000, 30000,
	HX_OS_ROUND_NEAREST, &interval_ns, NULL);
/* On success: 33366667 ns. */
~~~

The full 128-bit product is retained. Rounding supports down/up/nearest
(ties upward). Division by zero is INVALID; uint64 overflow is RANGE.
Failures preserve outputs. Direct low/high multiplication and division are
available. Compiler 128-bit integers are used where supported; other targets
use shared 32-bit partial products and long division. No heap, locks,
floating-point approximation or new assembly.

Precise waiting accepts a library monotonic absolute deadline. The default
spin window is 200 us; _ex permits 0..10 ms, with zero using ordinary sleep.
NOW succeeds immediately; INFINITE is invalid. No global timer-resolution or
scheduler changes. Scheduler latency can exceed the deadline and skip the
whole spin window; there is no hard realtime or accuracy improvement promise.
The Win7-compatible deadline clock is tick-based. QPC separately bounds the
spin budget, then falls back to ordinary sleep without changing the deadline
epoch/suspend semantics. See [Microsoft's clock documentation](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-queryunbiasedinterrupttime).

repeat executes synchronously on the calling thread, with a positive
millisecond interval and positive tick limit. First call follows one interval;
missed periods coalesce. OK continues, EOF stops successfully, other callback
errors propagate. The callback gets a cleared non-NULL error record.
_until additionally counts callbacks started and limits waiting. It cannot
interrupt a callback. Use existing timer/loop for background periodic work.

## Procedures and lifetime

~~~cpp
hx_os_proc_handler *owner = NULL; // Retained by the host on failure.
hx::ProcedureHandler procedures(owner);
procedures.create();
procedures.add("double", [](hx::CallData &data) {
	data.setInt("answer", data.view().integer("value") * 2);
});
auto result = procedures.call("double", hx::CallData().setInt("value", 21));
hx_os_status closed = procedures.closeUntil(HX_OS_DEADLINE_NOW);
// Retain owner and retry on failure.
~~~

Names are copied strict UTF-8, 1..127 bytes, unique and case-sensitive.
Duplicates return EXISTS; absence returns NOT_FOUND. No signature DSL or
implicit coercion. Parameters are copied and committed only on successful
callback return. Borrowed pointee side effects cannot be rolled back.
C++ StatusError details survive; other exceptions become IO_ERROR.

Calls are synchronous; concurrent calls can overlap. Callbacks run outside
locks. Defaults: 256 registrations, 64 parallel calls, 16 nested calls;
options allow bounded changes. Signal retirement/wait code is shared, but
PROCEDURES works with SIGNALS disabled. Successful registration adopts cleanup;
rejected registration does not. Cleanup runs once outside locks after the last
active callback exits.

unregister_until prevents subsequent starts, then waits; timeout retains the
same registration ID for retry. Close and pool shutdown release/clear owner
only on success. Self/ancestor waits return DEADLOCK. Finish registration,
unregistration and control calls before final close; accepted callbacks may
finish concurrently. Exclude parallel final closes and new API entrants.
Same-handler allocator reentry during add/close is rejected;
stop during that reentry is ignored. Deadlines cannot preempt synchronous
callbacks/cleanup. Retire callbacks, allocator state and borrowed pointers
before plugin unload. C++ facades retain host-owned handles and never hide
infinite waits or lose the retry handle in a destructor.

## Filenames and lexing

Filenames use UTC and existing calendar tokens, defaulting to
%CCYY-%MM-%DD_%hh-%mm-%ss. Windows-invalid characters/controls, trailing
spaces/dots and reserved device stems are repaired; Unicode is retained.
Extensions allow optional leading dot plus up to 32 ASCII alphanumeric/-/_
bytes. Results are capped at 240 UTF-8 bytes. No directory creation,
uniqueness or implicit collision overwrite is provided.

Lexers own strict UTF-8 input, accept initial BOM and reject embedded NUL.
C token views borrow until lexer destruction; C++ tokens own text.
Default tokens: identifier, number, quoted string, single-character
punctuation, newline; spaces/comments are skipped. Non-ASCII identifier
codepoints are accepted without claiming Unicode identifier validation.
Numbers cover decimal/fraction/exponent/hex integers; signs are separate.
Quoted escapes follow JSON plus escaped apostrophes; block comments do not nest.

next/peek/reset require external synchronization. EOF returns END;
other failures retain the output. Lexical failure is sticky until reset;
position reports its start. Lines/columns are 1-based; columns count codepoints,
Tab counts one, CRLF is one newline. Defaults: 16 MiB input, 1 MiB per token,
1 Mi tokens; skipped tokens count too. This is not a C parser/preprocessor
or script runtime.

## Trimming and cost

WIDE_MATH, PRECISE_WAIT, LEXER and PROCEDURES default ON and are independently
optional. Filename formatting uses CALENDAR; catalog text loading requires
TEXT_LOOKUP+LEXER, file loading additionally FILE_UTILS. Either SIGNALS or
PROCEDURES enables parameter packs. Disabled modules retain exports, clear
capability bits and return NOT_SUPPORTED.

ASCII edits/wide arithmetic allocate nothing. Replace/join use a temporary for
failure preservation; split owns a copy. Substring matching is naive, with
no worst-case linear claim. Catalog loading copies/inserts bounded entries
and is not a hot-loop operation. Registered procedure calls allocate no context
storage, but scan bounded slots, acquire locks and invoke a callback indirectly.
Use direct calls where the callee is already known. Run hx_os_portable_baseline
for target-specific observations; do not interpret object size as throughput.
