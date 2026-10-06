# Growable parameter packs

[简体中文](PARAMETERS.zh-CN.md) · [README](../README.md)

## 1. Choose a pack

Use `hx_os_params` to pass named values between functions in one process. It owns copied names, UTF-8 strings and arbitrary byte spans. Unlike the original stack `hx_os_calldata`, it can grow beyond 16 fields and 512 text bytes. Existing stack packs keep their layout and behavior.

| Choice | Best fit | Storage |
| --- | --- | --- |
| `hx_os_calldata` / `hx::CallData` | Existing small signal/procedure calls | Fixed stack storage, no allocation |
| `hx_os_params` / `hx::Parameters` | More fields, larger strings or binary data | Bounded heap storage, grows on demand |

This pack does not itself serialize a wire protocol. A stored pointer borrows an object in the same process; copying the pack does not copy that object. Use the serialization/IPC modules when data must cross a process boundary. Dynamic-pack integration with the signal dispatcher is a separate extension; do not cast one pack type to the other.

## 2. Run the C example

[parameters.c](../examples/parameters.c) creates a device request, sets several values with one final error check, and reads the named values back. Its local binary buffer expires before the read, demonstrating that the bytes are copied.

```sh
cmake --build build --config Release --target hx_os_parameters
# Linux / macOS:
./build/bin/hx_os_parameters
# Windows / Visual Studio:
./build/bin/Release/hx_os_parameters.exe
```

Expected output:

```text
Device 42 (camera): 4 binary bytes
```

After successful `hx_os_params_create(NULL, &params, &error)`, fill several fields like this:

```c
hx_os_params_edit edit = hx_os_params_edit_begin(params);

hx_os_params_edit_set_u64(&edit, "device_id", 42);
hx_os_params_edit_set_cstr(&edit, "name", "camera");
hx_os_params_edit_set_bool(&edit, "enabled", 1);

hx_os_status status = hx_os_params_edit_finish(&edit, &error);
```

An edit cursor performs each setter immediately. At the first failure it skips subsequent setters and preserves earlier successful fields. It is not a transaction or deferred commit. A single setter remains transactional: allocation, encoding or capacity failure preserves that field's previous value.

Read one value with `hx_os_params_get_u64(params, "device_id", &id, &error)`. Use the exact stored type: requesting signed integer from an unsigned field reports `HX_OS_TYPE_MISMATCH`. Missing names report `HX_OS_NOT_FOUND`. Finally call `hx_os_params_destroy(&params, &error)`; successful destruction sets the owner to null.

## 3. C++11 usage

Include `<hx/Parameters.hpp>`. Creation and operations return `Result`; there are no exceptions. `Parameters` is a move-only RAII owner. Once creation succeeds, a common call sequence is:

```cpp
auto created = hx::Parameters::create();
if (!created)
	return created.status();

auto &params = created.value();
auto written = params.edit()
	.setUnsigned("device_id", 42)
	.setString("name", "camera")
	.setBool("enabled", true)
	.finish();
if (!written)
	return written.status();

auto name = params.string("name");
if (!name)
	return name.status();

std::puts(name.value().c_str());
```

This fragment belongs in a function returning `hx_os_status`; include `<cstdio>` for `std::puts` and return `HX_OS_OK` on the successful path. `string()` copies into an owned `hx::String`. `stringView()` and `bytesView()` return borrowed spans; finish using them before mutating, moving or destroying the owner.

`ParametersView` is a borrowed read-only facade and `ParametersRef` a borrowed mutable facade; neither destroys a pack. Ordinary application code can use `Parameters` directly. A `clone()` result owns independent copied storage, while borrowed raw pointer values still point at the same objects.

## 4. Supported values

| Value | C setters/getters | C++ setters/getters |
| --- | --- | --- |
| Boolean | `set_bool` / `get_bool` | `setBool` / `boolean` |
| Signed 64-bit integer | `set_i64` / `get_i64` | `setInt` / `integer` |
| Unsigned 64-bit integer | `set_u64` / `get_u64` | `setUnsigned` / `unsignedInteger` |
| Double, including nonfinite values | `set_double` / `get_double` | `setDouble` / `real` |
| UTF-8 string | `set_string`, `set_cstr` / `get_string` | `setString` / `string`, `stringView` |
| Arbitrary bytes | `set_bytes` / `get_bytes` | `setBytes` / `bytesView` |
| Borrowed pointer | `set_pointer` / `get_pointer` | `setPointer` / `pointer` |

Each abbreviated C name starts with `hx_os_params_`. Strings accept explicit lengths and embedded NULs; a trailing NUL is also provided. A null string pointer is allowed only with zero length in `set_string`; `set_cstr` requires a non-null C string. Byte spans accept zero, invalid UTF-8 and any other byte value. Nonempty spans require a valid pointer. Getters preserve output arguments on failure.

`hx_os_params_get()` returns a borrowed tagged `hx_os_param_value` when the type is chosen at runtime. `hx_os_params_set()` accepts the same descriptor and copies any pointed-to string/bytes. `hx_os_params_at()` enumerates fields by zero-based index in UTF-8 byte order; `hx_os_params_count()` gives the number of fields. This is case-sensitive byte ordering, not locale collation. Field names must be nonempty strict UTF-8, at most 127 bytes.

## 5. Bounds, ownership and cost

Defaults permit 256 fields and 1 MiB of retained name/value storage. To change them, initialize `hx_os_params_options` using `HX_OS_PARAMS_OPTIONS_INIT` and pass it to `create_with_options`; maximum limits are 65,535 fields and 64 MiB. Entries are allocated geometrically as needed. Name/value storage includes terminators; entry-array and allocator metadata have separate bounded costs.

An update reuses an existing name/value allocation if it fits. Reducing a value does not immediately return that capacity: `hx_os_params_storage_bytes()` reports retained capacity, and `remove()` or `clear()` releases it. A limit applies to retained capacities, so replacing a long value with an empty value may not make room for another field until you remove or clear it. `clear()` frees the entry array too. Allocation-before-publication can temporarily hold old and new buffers, at most twice the relevant configured bounds plus bounded metadata. Multiple cloned packs have independent budgets.

Lookups use a sorted array and binary search; inserting/removing a name may move entries. This suits bounded parameter lists and does not promise constant-time insertion. Updating an existing scalar or a string/byte value within retained capacity allocates nothing. Queries allocate nothing. There are no internal mutexes, worker threads, global settings or container/variant dependencies.

Serialize mutation, moving and destruction against all access. Cloning also requires exclusive access to its source because it guards against allocator reentry. Concurrent read-only lookups are permitted only while the owner and bytes remain stable. An allocator must not reenter the active pack; detected reentry reports `HX_OS_DEADLOCK` and leaves the in-progress operation intact. The active owner/context must outlive allocator calls. Context destruction reports busy while the pack owns allocations.

`HX_OS_ENABLE_PARAMETERS` defaults to ON and can be disabled independently, including with signals/procedures disabled. Query `hx_os_params_available()` at runtime. Disabled operations explicitly report `HX_OS_NOT_SUPPORTED`; null destruction remains harmless. No third-party library is required.

## 6. Signals and named procedures

Use this owner directly with mutable signal dispatch and as input/result of growable named procedures. A synchronous callback borrows its pack; `params_destroy` returns BUSY during that borrow. Do not move or destroy a C++ owner from its callback. See [the signal/procedure guide](SIGNALS.md#6-change-parameters-during-a-notification) for the two distinct failure/publication contracts.
