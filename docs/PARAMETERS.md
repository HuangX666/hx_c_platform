# Growable parameter packs

[简体中文](PARAMETERS.zh-CN.md) · [README](../README.md)

When plugin calls, intra-process notifications, or named procedures need to pass named integers, floating-point numbers, UTF-8 strings, and binary blobs—and the number of fields or payload size can exceed the fixed stack limits of `hx_os_calldata`—use the growable parameter pack `hx_os_params` (or `hx::Parameters` in `<hx/Parameters.hpp>` for C++11). String and byte setters deep-copy their inputs into bounded heap storage owned by the pack, while view getters borrow directly from that internal storage.

To see the full lifecycle, inspect [parameters.c](../examples/parameters.c): it creates an `hx_os_params` instance, populates a device ID, name, boolean flag, and a 4-byte binary array inside `fill_request` (where the local byte array goes out of scope before the read to demonstrate deep ownership), reads the named fields back in `show_request`, and destroys the pack.

Follow the [example run instructions](../examples/README.md#run) to configure with `-DHX_OS_BUILD_EXAMPLES=ON` and build `hx_os_parameters` (no command-line arguments or input files are required):

```text
Device 42 (camera): 4 binary bytes
```

You can also use the example source as `main.c` in the [minimal project](USAGE.md#c-project).

## 1. Choose a pack

`hx_os_params` passes named values between functions or callbacks within a single process. It complements the fixed-size stack pack `hx_os_calldata` without changing the layout or behavior of existing `hx_os_calldata` APIs:

| Pack type | Best fit | Storage model |
| --- | --- | --- |
| `hx_os_calldata` / `hx::CallData` | Small signal or procedure calls with few fields | Fixed stack buffer (up to 16 fields and 512 text bytes), zero heap allocation |
| `hx_os_params` / `hx::Parameters` | Larger field counts, longer UTF-8 strings, or binary blobs | Bounded heap storage that grows on demand within configured limits |

> [!NOTE]
> `hx_os_params` is an in-process container rather than a wire serialization format. Raw pointer fields (`set_pointer`) borrow an address in the current process, and cloning a pack copies the pointer address without cloning the pointee. Use the serialization/IPC modules when data must cross process boundaries, and never cast `hx_os_calldata *` to `hx_os_params *` or vice versa.

## 2. Run the C example

In an examples-enabled build directory, build and run [parameters.c](../examples/parameters.c):

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

After creating a pack with `hx_os_params_create(NULL, &params, &error)`, you can either call individual `hx_os_params_set_*` functions or chain several writes through an `hx_os_params_edit` cursor and check the status once at the end:

```c
hx_os_params_edit edit = hx_os_params_edit_begin(params);

hx_os_params_edit_set_u64(&edit, "device_id", 42);
hx_os_params_edit_set_cstr(&edit, "name", "camera");
hx_os_params_edit_set_bool(&edit, "enabled", 1);

hx_os_status status = hx_os_params_edit_finish(&edit, &error);
```

- **Edit cursor semantics**: Each `hx_os_params_edit_set_*` call applies its change immediately. When the first failure occurs, the cursor records the error and skips subsequent setters while retaining the fields that already succeeded (it is a sticky-error helper, not a rollback transaction). Each individual setter is transactional on its own field: allocation, UTF-8 encoding, or capacity errors leave that field's previous value intact.
- **Exact-type reads and cleanup**: Read fields back with matching getters such as `hx_os_params_get_u64(params, "device_id", &id, &error)`. Types must match exactly—reading an unsigned integer field with `hx_os_params_get_i64` returns `HX_OS_TYPE_MISMATCH`, and looking up a missing field name returns `HX_OS_NOT_FOUND`. Call `hx_os_params_destroy(&params, &error)` when finished; on success, `params` is set to `NULL`.

## 3. C++11 usage

Include `<hx/Parameters.hpp>` to use the move-only RAII wrapper `hx::Parameters`. All factory and access methods return `hx::Result<T>` without throwing exceptions:

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

(This snippet assumes an enclosing function returning `hx_os_status`; include `<cstdio>` for `std::puts` and return `HX_OS_OK` at the end.)

String and binary getters offer both owning and borrowed forms:
- `params.string("name")` deep-copies the field into an owned `hx::String`.
- `params.stringView("name")` and `params.bytesView("payload")` return borrowed spans that must not outlive the next mutation, move, or destruction of `params`.
- `hx::ParametersView` (read-only borrow) and `hx::ParametersRef` (mutable borrow) wrap an existing pack without destroying it on scope exit. Calling `params.clone()` creates an independent `hx::Parameters` copy (borrowed raw pointers still point to the same external objects).

## 4. Supported values

| Value kind | C setters / getters (prefix `hx_os_params_`) | C++11 setters / getters |
| --- | --- | --- |
| Boolean | `set_bool` / `get_bool` | `setBool` / `boolean` |
| Signed 64-bit integer (`int64_t`) | `set_i64` / `get_i64` | `setInt` / `integer` |
| Unsigned 64-bit integer (`uint64_t`) | `set_u64` / `get_u64` | `setUnsigned` / `unsignedInteger` |
| Double (including `NaN` / `Inf`) | `set_double` / `get_double` | `setDouble` / `real` |
| UTF-8 string | `set_string`, `set_cstr` / `get_string` | `setString` / `string`, `stringView` |
| Arbitrary byte span | `set_bytes` / `get_bytes` | `setBytes` / `bytesView` |
| Borrowed pointer (`void *`) | `set_pointer` / `get_pointer` | `setPointer` / `pointer` |

Field naming, string encoding, and runtime inspection rules:
- **Strings and byte spans**: `hx_os_params_set_string` accepts an explicit byte length and embedded `NUL` characters, and always appends a trailing `NUL` terminator internally (`NULL` is permitted only when the length is `0`; `set_cstr` requires a non-`NULL` C string). `hx_os_params_set_bytes` accepts any byte sequence including invalid UTF-8. Getters leave output variables unchanged on failure.
- **Field names**: Names must be non-empty, valid UTF-8 strings between `1` and `127` bytes long, and are compared by exact case-sensitive UTF-8 byte order.
- **Runtime-tagged access and indexed enumeration**: When a field's type is known only at runtime, `hx_os_params_get()` outputs a borrowed tagged `hx_os_param_value` struct, and `hx_os_params_set()` accepts that same struct (deep-copying any string or byte span it references). Use `hx_os_params_count()` and `hx_os_params_at()` to iterate over all fields in `0..count-1` UTF-8 byte order.

## 5. Bounds, ownership and cost

- **Default and maximum limits**: `hx_os_params_create` defaults to at most `256` fields (`max_items`) and `1 MiB` of retained name/value storage (`max_storage_bytes`). To customize these bounds, initialize `hx_os_params_options` with `HX_OS_PARAMS_OPTIONS_INIT` and call `hx_os_params_create_with_options` (up to `65,535` fields and `64 MiB`).
- **Capacity reuse and reclamation**: Updating an existing field reuses its current buffer allocation whenever the new value fits. Shrinking a field's value does not immediately return bytes to the budget: `hx_os_params_storage_bytes()` reports the **retained capacity**, and only `hx_os_params_remove()` or `hx_os_params_clear()` frees that storage (`clear()` also frees the entry array). Consequently, overwriting a long string with an empty string near the budget limit may still require `remove()` or `clear()` before a new field can be inserted. Because updates stage new buffers before replacing old ones, a single replacement can temporarily hold both the old and new buffers.
- **Lookup complexity and concurrency**: Fields are stored in a sorted array searched with binary search (`O(log n)`), and inserting or removing a field shifts array entries. The pack has no internal mutexes or worker threads:
  - Concurrent read-only lookups are safe while the pack and its stored bytes remain immutable.
  - Serialize all mutations, moves, destruction, and `clone` calls (cloning marks the source pack active to guard against allocator reentry) against all other accesses.
  - Custom allocator callbacks must not reenter the active pack; detected reentry returns `HX_OS_DEADLOCK` while leaving the outer operation intact. While a pack holds allocations, `hx_os_context_destroy` on its allocator context returns `HX_OS_BUSY`.
- **CMake feature switch**: `HX_OS_ENABLE_PARAMETERS` defaults to `ON` and can be toggled independently of `HX_OS_ENABLE_SIGNALS` and `HX_OS_ENABLE_PROCEDURES`. Query `hx_os_params_available()` at runtime; when disabled, active operations return `HX_OS_NOT_SUPPORTED` while destroying a `NULL` handle remains a safe no-op.

## 6. Signals and named procedures

An `hx_os_params` handle (or `hx::Parameters` object) can be passed directly to mutable signal dispatchers and growable named procedures. Synchronous callbacks borrow the pack for the duration of the call; attempting to call `hx_os_params_destroy` while borrowed returns `HX_OS_BUSY` (never move or destroy the owning `hx::Parameters` inside a callback). For the difference between signal and procedure failure/publication rules, see [the signal/procedure guide](SIGNALS.md#6-change-parameters-during-a-notification).
