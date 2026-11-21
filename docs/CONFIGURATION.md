# Configuration documents

[简体中文](CONFIGURATION.zh-CN.md) · [README](../README.md)

When your application needs to load configuration files or in-memory text at startup and query ports, feature flags, nested objects, or arrays through a read-only tree, this module provides a unified C99 and C++11 interface covering seven formats: **JSON, YAML, XML, INI, TOML, ENV (dotenv), and Java-style properties**.

To see a complete working example, open [configuration.c](../examples/configuration.c). The program parses an embedded TOML snippet with `hx_os_config_parse()`, looks up the `server.port` node, reads its integer value with `hx_os_config_node_get_i64()`, and releases the document with `hx_os_config_release()`. Follow the [example run instructions](../examples/README.md#run) to enable examples and build `hx_os_configuration` (it needs no command-line arguments or external input files):

```text
Server port: 8080
```

To load directly from a file on disk, call `hx_os_config_load()` instead (see the [complete JSON program](USAGE.md#configuration) for a file-based example, or drop the example source into the [minimal project](USAGE.md#c-project) as `main.c`).

**Scope and Where to Go Next:**
- **Immutable snapshots**: Parsing or loading produces an **immutable document**. External edits to the file on disk never mutate an already loaded document in memory—call `hx_os_config_load()` again when you want to reload.
- **In-memory editing & struct serialization**: This guide covers the supported grammar profiles, parser backend selection, and lifetime contracts. To insert, replace, or remove keys in memory and save the updated file, see [Editing configuration documents](CONFIG_EDIT.md) or the [settings tutorial](USAGE.md#settings). To map C/C++ structs directly to and from JSON, YAML, or TOML, see the [serialization tutorial](SERIALIZATION.md).
- **Default built-in backends**: Every format defaults to the first-party C99 `HX_OS_CONFIG_BACKEND_BUILTIN` parser—enabling optional third-party libraries at build time never changes parsing behavior behind your back. Select an optional backend explicitly via `options.backend` and check `hx_os_config_backend_available()` (disabled or mismatched backends return `HX_OS_NOT_SUPPORTED` without publishing a document).

## Input and lifetime

- **Strict UTF-8 inputs**: Input text, file paths, keys, and string values must be valid UTF-8. A leading UTF-8 BOM is accepted and stripped automatically, whereas UTF-16 BOMs and invalid UTF-8 sequences fail immediately without falling back to the active ANSI code page or mutating the process locale (convert legacy encodings explicitly with `hx_os_text_convert()` or `hx_string_assign_encoded()` first).
- **Reference counting & concurrent read queries**:
  - `hx_os_config_parse()` borrows the input slice and `options` during the call and copies all retained keys and values into the document; `hx_os_config_load()` reads files through the cross-platform file API (including UTF-8-to-UTF-16 long-path handling on Windows). On failure, the output pointer is cleared and all temporary allocations are freed.
  - A parsed document owns its memory through its bound `hx_context` until the final `hx_os_config_release()`. Pair `hx_os_config_retain()` and `hx_os_config_release()` when sharing ownership.
  - All `hx_os_config_node *` pointers and `hx_string_view` slices returned by query functions **borrow from the owning document**. As long as the final `release` is excluded while readers are active, multiple threads can query a retained immutable document concurrently. Never call `retain` on an already released pointer.
- **Embedded NUL bytes & XML structure**: Returned `hx_string_view` slices carry an explicit byte length plus an extra trailing `\0` in storage. Because valid escape sequences (such as `\u0000` in JSON) can produce embedded `NUL` bytes even inside object keys, pass the explicit slice length to `hx_os_config_node_member()` rather than using `strlen()`. In XML documents, child elements and text/CDATA nodes are preserved in document order on the child list while attributes live in a separate attribute object; XML namespace prefixes (such as `ns:tag`) remain literal strings without URI expansion or XPath evaluation.

## Format profiles

Constructs outside the profiles below fail explicitly rather than being silently flattened or guessed:

| Format | Built-in profile (`BUILTIN`) | Optional backend (opt-in via CMake) |
|---|---|---|
| **JSON** | Strict RFC 8259 values, objects, arrays, Unicode escapes, exact numeric text | `cJSON 1.7.19` |
| **YAML** | Single document, block maps/sequences, empty `{}` / `[]`, scalar keys and values | `libyaml 0.2.5` |
| **XML** | UTF-8 XML 1.0 elements/attributes, mixed text, CDATA, predefined & numeric references | `Mini-XML 4.0.6` |
| **INI** | `[section]` headers and single-line string key/value pairs | `inih r62` |
| **TOML** | Standard tables, dotted keys, single-line scalars, arrays, single-line inline tables | `tomlc17 R260821` |
| **ENV** | Explicit `.env` (dotenv) dialect | Built-in only (`BUILTIN`) |
| **properties** | UTF-8 Java-style `.properties` syntax | Built-in only (`BUILTIN`) |

**Format Details and Backend Differences:**
- **JSON**: Accepts top-level scalars, objects, or arrays, and rejects trailing garbage, trailing commas, non-standard numbers, and unpaired UTF-16 surrogates. Numeric nodes preserve their exact lexical text, so 64-bit integers like `9007199254740993` are never rounded through `double` during tree construction.
- **YAML**:
  - **Built-in backend**: Supports space indentation, block sequences (`- item`), mappings inside sequences, single/double quotes, comments, optional `---` / `...` document markers, and empty flow collections (`{}` and `[]`) so serialized empty containers round-trip cleanly. Unquoted `true`/`false` become booleans, `null`/`~` and empty values become `null`, JSON-formed numbers become numeric nodes, and all other unquoted scalars (including `yes`, `on`, and dates) as well as all quoted values remain strings. Built-in double-quoted strings support standard JSON escapes plus `\UXXXXXXXX`.
  - **libyaml backend**: Adds non-empty flow containers, multiline plain and quoted strings, block scalars (`|` and `>`), directives, compact nested sequences, and YAML-specific escapes while enforcing the exact same scalar typing rules.
  - **Shared boundaries**: Both backends reject anchors (`&`), aliases (`*`), complex non-scalar keys, and custom tags—no alias expansion or `<<` merge key processing occurs (this is not a full YAML 1.2 implementation).
- **XML**: Supports predefined entities (`&lt;`, `&amp;`, etc.) and numeric character references, validates XML character/name rules and matching closing tags, and normalizes `CR`/`CRLF` line endings to `LF`. Literal whitespace in attributes is normalized to spaces while character-reference whitespace is preserved. Comments and processing instructions are omitted from the configuration tree. An optional leading `<?xml ...?>` declaration must specify version `1.0` and (if present) `UTF-8` encoding. `DOCTYPE`/`DTD` declarations and external entities are rejected. Repeated sibling elements and adjacent text/CDATA nodes remain distinct.
- **INI**: Accepts `=` or `:` key/value separators, leading `#` or `;` comments, and inline `;` comments preceded by whitespace. Keys, values, and section names are trimmed of surrounding whitespace while quotes and backslashes inside values remain literal. Repeating a `[section]` header reopens and merges into that section, whereas a name collision between a scalar key and a section fails. Multiline continuations are disabled in both backends. Note that `inih`'s fixed internal buffer limits untrimmed section names to `49` UTF-8 bytes and lines to `1 MiB` (the adapter rejects longer inputs before truncation occurs); the built-in backend uses the general `options` limits. A space followed by a semicolon inside a section or key belongs to its name. Inline comments start in the value.
- **TOML**:
  - **Built-in backend**: Supports single-line basic and literal strings, `true`/`false`, signed 64-bit integers (decimal, `0x`, `0o`, `0b`, and valid underscores), floating-point numbers, arrays (including multiline arrays and trailing commas), dotted keys, standard `[table]` headers, and single-line inline tables `{ a = 1, b = 2 }`.
  - **tomlc17 backend**: Adds multiline strings, `[[array_of_tables]]`, date/time values (`DATETIME` nodes preserve their exact lexical text, fractional precision, and timezone offset), and TOML 1.1 parsing, subject to upstream ceilings (`10` dotted key components, `30` bracket/brace nesting levels, and `16K` entries per table or array). Duplicate keys or tables fail in both backends, and numbers always retain their source spelling.
- **ENV (dotenv)**: Keys match `[A-Za-z_][A-Za-z0-9_]*` with an optional `export ` prefix. Values may be empty, single-quoted (literal), double-quoted (supporting `\\`, `\"`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t`, `\uXXXX`, and escaped `\$`, `\'`, `\#`), or unquoted (where `#` at the start or after whitespace begins a comment). Variable expansion (`${VAR}`) and shell command substitution are never evaluated.
- **properties**: Supports unescaped `=`, `:`, or whitespace separators, `#` or `!` comments, backslash-escaped keys/separators/spaces, odd-backslash line continuations (stripping leading continuation whitespace while preserving trailing value spaces), and surrogate-validated `\uXXXX` escapes. Input must be UTF-8 (convert legacy ISO-8859-1 files first).
- **Control characters**: Raw unescaped ASCII control characters other than `\t`, `\r`, and `\n` are rejected across all formats (`properties` additionally allows formfeed `\f`).

## Queries and errors

- **Tree navigation**:
  - `hx_os_config_node_member()` performs exact, case-sensitive byte matching.
  - `hx_os_config_node_pointer()` implements **RFC 6901 JSON Pointer** across objects and arrays: `""` selects the root, `"/a~1b"` selects key `"a/b"` (`~0` escapes `~`, `~1` escapes `/`), and array indices are unsigned decimal numbers without leading zeros (dots `.` are not path separators).
  - For XML elements, traverse `first_child` / `next_sibling` and query the attribute object. Indexed sibling access via `at()` walks the sibling list linearly.
- **Typed value getters**:
  - Missing nodes return `HX_OS_NOT_FOUND` and incompatible node types return `HX_OS_TYPE_MISMATCH`; failed getters never modify the caller's output variable.
  - `get_i64()` and `get_u64()` accept both integer nodes and complete integer strings (including TOML radix prefixes), checking for overflow and never truncating fractional values.
  - `get_double()` parses floating-point values using a private C numeric locale and reports range errors on `ERANGE`. On target SDKs that lack per-call locale support, `get_double()` returns `HX_OS_NOT_SUPPORTED` (`HX_OS_CAP_CONFIG_DOUBLE` reflects this capability, and CMake rejects enabling `cJSON`/`tomlc17` on such toolchains).
  - `get_bool()` accepts only boolean nodes or complete `"true"` / `"false"` strings.
- **Duplicate key policy**: Duplicate keys default to failing with `HX_OS_EXISTS`. For JSON, YAML, INI, ENV, and properties, `options` can select `FIRST` or `LAST` (`LAST` replaces the value while preserving the key's original sibling position); duplicate TOML keys and duplicate XML attributes always fail. Overwritten duplicate entries still count toward parse work, node, and memory budgets.
- **Diagnostics without leaking secret values**: `hx_os_error` records a copied UTF-8 source name, the status code, and a controlled English diagnostic message that **never echoes raw configuration values** (preventing accidental logging of secrets). Error locations report a byte offset plus 1-based Unicode code-point line and column numbers (`CRLF` counts as one newline, `\t` as one column, and a leading BOM as one code point; unknown locations report `SIZE_MAX / 0 / 0`). Grammar errors return `HX_OS_SYNTAX`, allocation failures return `HX_OS_NO_MEMORY`, exhausted budgets return `HX_OS_LIMIT`, and recognized out-of-profile features return `HX_OS_NOT_SUPPORTED`.

## Limits and isolation

Initialize parse options with `hx_os_config_options_init(&options)`:
- **Default bounds**: `1 MiB` input size, `16 MiB` parse memory, `65,536` nodes (including temporary nodes during construction), nesting depth `64` (configurable up to `128`; setting any limit to `0` is rejected with `HX_OS_INVALID`), and `16,777,216` work units.
- **Memory accounting**: The `16 MiB` parse memory budget tracks the document tree, block metadata, retained scratch buffers, and all redirected third-party `malloc`/`calloc`/`realloc`/`strdup` allocations (accounting for both old and new buffers during `realloc`). Caller input buffers, `load()` file-read buffers, and post-parse `get_double()` conversion buffers live outside the parse tree budget.
- **Vendor symbol isolation & thread safety**: All five optional third-party parsers (`cJSON`, `libyaml`, `Mini-XML`, `inih`, `tomlc17`) are compiled with private prefixed symbols and routed through nested compiler-TLS allocation scopes bound to the caller's `hx_context`—their global allocator hook setters are never called. Because upstream `cJSON` mutates a global error struct even when called through its options API, `cJSON` backend parsing is serialized internally with a reentrant lock (built-in JSON parsing and all read-only queries are lock-free).

## Build and delivery

All five optional parser backends default to `OFF`, while `HX_OS_ENABLE_CONFIG` defaults to `ON`. When enabled, the optional parsers are pulled from pinned Git submodules (or via `-DHX_OS_DEPENDENCY_PROVIDER=FETCHCONTENT`) without requiring manual source path variables:

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

(On Windows multi-configuration generators, the compiled binary is located at `build-parsers/bin/Release/hx_os_configuration.exe`.)

- Setting `-DHX_OS_ENABLE_CONFIG=OFF` strips all format parsers for minimal embedded builds while keeping ABI query stubs that return `HX_OS_NOT_SUPPORTED` (enabling a parser backend while `HX_OS_ENABLE_CONFIG=OFF` is a CMake configuration error).
- Enabled third-party parsers are compiled privately into the library binary, so downstream consumers never need vendor headers or extra link targets; their license notices are installed automatically (see `THIRD_PARTY_NOTICES.md`).
