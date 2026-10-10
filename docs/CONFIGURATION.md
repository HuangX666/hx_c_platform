# Configuration documents

[简体中文](CONFIGURATION.zh-CN.md) · [README](../README.md)

This optional, synchronous C99 module returns immutable documents through a C ABI. Application settings, reload watchers, environment mutation, variable expansion, defaults, schema validation, includes and save timing remain consumer policies. All seven formats now have output APIs; nested document construction, explicit replacement and schema-based object conversion are described in [serialization](SERIALIZATION.md). No background thread or queue is created.

The default backend is always BUILTIN. Installing dependencies never changes parsing implicitly. Select options.backend and check hx_os_config_backend_available. Disabled or mismatched backends return NOT_SUPPORTED and publish no document.

## Input and lifetime

Input, paths, keys and values are UTF-8. An initial UTF-8 BOM is accepted; UTF-16 BOMs and malformed UTF-8 fail. Convert legacy input explicitly with hx_os_text_convert or hx_string_assign_encoded. There is no ANSI-codepage fallback and no process-locale change.

parse borrows input/options during the call and copies retained names/values. load uses the regular-file API and its UTF-8-to-UTF-16 Windows path adapter. Failure clears the output document and releases this call's resources. Successful documents retain context allocations until their final release. retain/release balance references; nodes/slices borrow the document. Exclude final release while any reader uses borrowed data. Retained immutable documents support concurrent queries. Retaining a freed pointer is invalid.

Slices carry byte lengths plus an extra terminating NUL. An allowed escape can produce embedded NUL, even in JSON keys; pass the slice length to member instead of strlen. XML preserves ordered element/text children and a separate attribute object. Namespace prefixes remain literal names; no namespace expansion or XPath is provided.

## Format profiles

Unsupported structures fail rather than being silently flattened or guessed.

| Format | Builtin profile | Optional backend |
|---|---|---|
| JSON | Strict values, objects, arrays, Unicode escapes, exact numeric text | cJSON 1.7.19 |
| YAML | One document, block maps/sequences, scalar keys and values | libyaml 0.2.5 |
| XML | UTF-8 XML 1.0 elements/attributes, mixed text, CDATA, references | Mini-XML 4.0.6 |
| INI | Sections and string key/value pairs | inih r62 |
| TOML | Common tables, dotted keys, scalars, arrays, inline tables | tomlc17 R260821 |
| ENV | Explicit dotenv dialect | Builtin |
| properties | UTF-8 Java-style property syntax | Builtin |

JSON accepts top-level scalars and rejects trailing garbage/commas, malformed numbers and unpaired surrogates. Numbers retain lexical spelling; 9007199254740993 is not rounded to double while constructing the document.

Builtin YAML supports space indentation, block sequences, mapping sequence items, quotes, comments, and optional --- / ... document markers. Plain true/false are booleans; null/~ and empty values are null; JSON-style numbers are numeric; other plain values are strings. Yes/on/dates are strings; quoted values stay strings. Multiline plain/quoted scalars, nonempty flow containers, directives, block scalars, compact nested sequences and Unicode line-break extensions require libyaml. Both backends reject anchors, aliases, complex keys and custom tags. No alias expansion or YAML merge semantics occur. libyaml adds flow containers, multiline strings and block folding with the same scalar schema. Known standard scalar tags are checked against that schema. This is not a claim of complete YAML 1.2 support. Builtin double quotes support the common JSON escape set and UXXXXXXXX; other YAML-specific escapes require libyaml.

XML supports predefined entities and numeric character references, checks XML character/name rules and matching tags, and normalizes CR/CRLF. Literal attribute whitespace becomes spaces; referenced whitespace is preserved. Comments/processing instructions are intentionally omitted from the data tree. A leading declaration specifies XML 1.0 and, if present, UTF-8 encoding. DTD and other named entities are unsupported; nothing external is loaded. Repeated elements and adjacent text/CDATA nodes remain distinct. This configuration tree is not a round-trip XML editor.

INI accepts = or : separators, leading # / ; comments, and inline ; comments preceded by whitespace. It trims keys, values and section names. Quotes and backslashes in values remain literal. Repeated section headers reopen a section; scalar/section name conflicts fail. Indented lines are ordinary assignments; multiline continuation is disabled in both backends. inih's unchanged internal buffer limits its untrimmed section names to 49 UTF-8 bytes; its compiled line limit is 1 MiB. The adapter rejects overflow before truncation. The builtin uses the configured general limits.

Builtin TOML supports single-line basic/literal strings, true/false, signed 64-bit integers (decimal/hex/octal/binary and valid underscores), real values, arrays (including multiline/trailing commas), dotted keys, tables and inline tables. Inline tables have no newlines between entries or trailing commas; an array value can span lines. Multiline strings, arrays of tables and date/time values require tomlc17. Duplicate keys/tables always fail. The native backend adds these constructs and TOML 1.1 parsing, subject to upstream limits: 10 key components, 30 bracket/brace levels, and 16K entries per table/array. Both upstream and configured limits apply. DATETIME nodes retain source spelling, including fractional precision and timezone offsets. Numbers also retain spelling instead of formatting an upstream double back to text.

ENV keys match [A-Za-z_][A-Za-z0-9_]*, with an optional export prefix. Values may be empty, single quoted, double quoted or unquoted. Single quotes are literal. Double quotes accept backslash, quote, slash, b/f/n/r/t, uXXXX escapes, and escaped $, apostrophe and #. An unquoted # begins a comment at the start or after whitespace. Neither dollar expressions nor shell syntax are evaluated. This is an explicit dotenv dialect, not a universal standard.

properties supports unescaped = / : / whitespace separators, # / ! comments, escaped keys/separators/spaces, odd-backslash line continuation and uXXXX Unicode escapes with surrogate validation. Comments do not continue. Leading continuation whitespace is removed; trailing value spaces remain. Unknown escapes drop the backslash, as in Java properties. Input is UTF-8, not the legacy ISO-8859-1 default of some Java APIs; convert legacy data first.

Raw ASCII controls other than tab/CR/LF are rejected; properties also accepts formfeed. Escaped controls follow each format's rules.

## Queries and errors

member uses exact case-sensitive bytes. pointer implements RFC 6901 for objects/arrays: empty selects the root, /a~1b selects key a/b, and ~0 escapes a tilde. Array indices are unsigned decimal without leading zeros. Dots are not generic path separators. XML uses first/next and attributes instead. at walks siblings, so indexed access is linear.

Missing values return NOT_FOUND; wrong types return TYPE_MISMATCH. get_i64/get_u64 accept integer nodes and complete integer strings, including TOML radix spelling; they check overflow and never truncate fractions. get_double uses a private C numeric locale, may round, and reports ERANGE. An SDK without per-call locale support returns NOT_SUPPORTED for that getter; CMake rejects CJSON/TOMLC enablement there. get_bool accepts only true/false, including complete strings. Getter outputs are unchanged on failure. HX_OS_CAP_CONFIG_DOUBLE reports the numeric conversion capability.

Duplicate keys default to EXISTS. FIRST/LAST are available for JSON, YAML, INI, ENV and properties; LAST retains the key's original sibling position. TOML duplicates and duplicate XML attributes always fail. Discarded entries still consume parse work, nodes and memory.

Errors contain a copied UTF-8 source label, status, available backend/native details, and controlled English messages without configuration values. Positions are byte offsets plus one-based Unicode-codepoint line/column counts. CRLF is one line break, a tab one column, and BOM an input codepoint. Unknown positions are SIZE_MAX / 0 / 0. Backend messages that might include secrets are not forwarded. Libyaml character indices are translated to byte positions; TOMLC/Mini-XML failures without reliable positions report unknown. SYNTAX means invalid grammar; NO_MEMORY means allocation failure; LIMIT means a configured bound; recognizable out-of-profile features use NOT_SUPPORTED. Malformed syntax resembling an unsupported feature may still report SYNTAX.

## Limits and isolation

Initialize options with hx_os_config_options_init. Defaults: 1 MiB input, 16 MiB parse memory, 65,536 materialized/temporary nodes, depth 64, and 16,777,216 work units. Zero limits are invalid; maximum configured depth is
128. Work counts input/copy/comparison bytes and adapter operations, not vendor instructions or elapsed time. No hard deadline/cancellation is promised.

The memory budget includes the document, block metadata, allocation wrappers, retained scratch, and redirected vendor malloc/calloc/realloc/strdup calls. Reallocation accounts for old and replacement buffers simultaneously. Context allocator overhead, libc locale objects, OS locks, caller input and load's file-read buffers are outside this budget. During growth load can hold two buffers, each bounded by max_input_bytes. Numeric getters can allocate temporary conversion buffers/locales after parsing, outside the parse budget.

Synchronous vendor calls use nested compiler-TLS allocation scopes, with no owned TLS destructor, hidden worker, queue or global consumer settings. Allocator callbacks can reenter parsing on the same thread, but must not yield/switch fibers inside a native parse. They retain the core allocator's thread-safety, alignment and lifetime requirements.

Vendor symbols are private/prefixed and global hook setters are never called. cJSON writes global error state even with its options API; private cJSON parsing is serialized, allowing same-thread reentry. Builtin JSON and immutable queries do not take this lock. Mini-XML's private callback table stays read-only and allocates no TLS state. Numeric conversion uses a per-call C locale. Third-party source remains unchanged.

cJSON and Mini-XML do native parsing plus shared strict validation/tree materialization. This preserves embedded NUL, large JSON integers and exact XML normalization/mixed content that native trees alone do not guarantee. They are not independent lossless DOM alternatives or promised faster paths. libyaml, inih and tomlc17 build from native events/data. No throughput improvement is claimed.

## Build and delivery

The five optional parsers use the shared pinned dependency provider: Git submodules by default, or `HX_OS_DEPENDENCY_PROVIDER=FETCHCONTENT`. No source-path arguments or Python downloader are required. Explicit `HX_OS_<PARSER>_SOURCE_DIR` overrides remain available for existing integrations. The example disables unrelated OpenSSL/archive modules; use a prepared SDK when those are needed.

~~~sh
git submodule update --init --recursive
cmake -S . -B build-parsers \
  -DHX_OS_BUILD_EXAMPLES=ON \
  -DHX_OS_ENABLE_CJSON=ON -DHX_OS_ENABLE_LIBYAML=ON \
  -DHX_OS_ENABLE_MINIXML=ON -DHX_OS_ENABLE_INIH=ON \
  -DHX_OS_ENABLE_TOMLC=ON \
  -DHX_OS_ENABLE_CRYPTO=OFF -DHX_OS_ENABLE_ARCHIVE=OFF
cmake --build build-parsers --config Release --parallel 4
build-parsers/bin/hx_os_configuration
~~~

Windows multi-configuration builds place these programs in the corresponding `bin/Release/` directory with an `.exe` suffix.

All five backend switches default OFF; HX_OS_ENABLE_CONFIG defaults ON. CONFIG=OFF removes format parsers but keeps ABI queries/error entry points; parse/load return NOT_SUPPORTED. Backend=ON with CONFIG=OFF is an error. Embedded consumers can select builtin-only or omit the entire module.

Native sources are private object code inside the static library or DLL/so. Installed consumers need neither vendor headers nor separate parser libraries. Enabled dependency licenses are installed; see THIRD_PARTY_NOTICES.md. examples/configuration.c is the common C example and requires no C++.

Builtin YAML also accepts empty `{}` and `[]` to preserve empty collection types on output/readback. Nonempty flow containers still require libyaml.
