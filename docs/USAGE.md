# hx_c_platform Task Tutorials

[简体中文](USAGE.zh-CN.md) · [Home](../README.md) · [Detailed reference](REFERENCE.md)

To browse more complete programs by topic, start with the [runnable example directory](../examples/README.md), which lists each example's use case, CMake target, required inputs, and expected output.

- [1. How to use these tutorials](#using-guide)
- [2. Prepare a small project](#project)
- [3. Read and display a text file](#files)
- [4. Edit strings](#strings)
- [5. Read JSON configuration](#configuration)
- [6. Change settings in memory](#settings)
- [7. Use C vector and queue](#containers)
- [8. Submit a C++ pool task](#thread-pool)
- [9. Store different types in one value](#variant)
- [10. First-use questions](#questions)

<a id="using-guide"></a>

## 1. How to use these tutorials

Each section in this guide solves a specific, everyday engineering task—such as reading a port number from a JSON configuration file or running a calculation on a background thread pool.

Start by setting up the minimal project in section 2, then pick any task you want to try:
1. **Prepare inputs**: Create the sample input file if the section uses one (most examples run without external files).
2. **Build and run**: Every C and C++ code block in this guide is a complete, standalone program. Save one block at a time as `main.c` or `main.cpp`—do not paste multiple `main` functions into the same file.
3. **Verify and adapt**: Confirm that your output matches the expected output shown in the tutorial, then swap in your own file paths, keys, or application data.

You do not need custom memory allocators or deep platform knowledge to get started; when you need exact API boundaries, ownership contracts, or platform details, check the [detailed reference](REFERENCE.md).

<a id="project"></a>

## 2. Prepare a small project

### 2.1 Directory and source

To integrate from source, open an MSVC developer terminal (Windows) or a standard shell (Linux / macOS) and clone the repository with its submodules:

```sh
mkdir hx_demo
cd hx_demo
mkdir deps
cd deps
git clone --branch dev https://github.com/HuangX666/hx_c_platform.git
git -C hx_c_platform submodule update --init --recursive
cd ..
```

You are now in the `hx_demo` directory. Create `CMakeLists.txt` and your source file (`main.c` or `main.cpp`) here. Only the text-file and JSON-configuration tutorials require input files; all other tutorials run without them.

```text
hx_demo/
├── CMakeLists.txt
├── main.c
├── hello.txt       # Used by the text-file example in section 3.
├── config.json     # Used by the configuration example in section 5.
└── deps/
    └── hx_c_platform/
```

Section 3 reads a UTF-8 `hello.txt` file containing:

```text
Hello, hx_c_platform!
```

Section 5 reads a UTF-8 `config.json` file containing:

```json
{"port": 8080}
```

<a id="c-project"></a>

### 2.2 Build a C program

Save your chosen C example as `main.c` and use the following `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(hx_demo LANGUAGES C)

foreach(module CRYPTO ARCHIVE COMPRESSION HTTP URL UNICODE PERSISTENT_QUEUE)
	set(HX_OS_ENABLE_${module} OFF CACHE BOOL "")
endforeach()

add_subdirectory(deps/hx_c_platform)

add_executable(hx_demo main.c)
target_link_libraries(hx_demo PRIVATE hx_c_platform::hx_c_platform)
set_target_properties(hx_demo PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO)
```

Here `add_subdirectory` builds the library and `target_link_libraries` links `hx_c_platform::hx_c_platform` into your executable.

> [!TIP]
> The `foreach` loop disables seven optional modules that require external native SDKs (such as OpenSSL, libcurl, utf8proc, and SQLite) so you can build the starter project immediately without installing extra packages. Core facilities—files, strings, built-in JSON, mutable settings, containers, thread pools, and variants—remain enabled. When you need those optional SDK-backed modules later, prepare their SDKs using the [dependency guide](DEPENDENCIES.md) and enable them with `-DHX_OS_ENABLE_<MODULE>=ON`.

Configure and build from `hx_demo`:

```sh
cmake -S . -B build
cmake --build build --config Release
```

On Windows with a Visual Studio generator, run:

```powershell
.\build\Release\hx_demo.exe
```

On Linux or macOS (or with a Ninja / Makefile generator), run:

```sh
./build/hx_demo
```

Run the executable from `hx_demo` so relative paths (`hello.txt`, `config.json`) resolve in the current working directory. To try another tutorial, replace `main.c` and run `cmake --build build --config Release` again—you can reuse the same `build` directory across examples (on Windows with Ninja, the binary is located at `build/hx_demo.exe`).

<a id="cpp-project"></a>

### 2.3 Build a C++ program

Save your chosen C++ example as `main.cpp` and use the `CMakeLists.txt` below. The build and run commands are identical to section 2.2:

```cmake
cmake_minimum_required(VERSION 3.16)
project(hx_demo LANGUAGES C CXX)

foreach(module CRYPTO ARCHIVE COMPRESSION HTTP URL UNICODE PERSISTENT_QUEUE)
	set(HX_OS_ENABLE_${module} OFF CACHE BOOL "")
endforeach()

add_subdirectory(deps/hx_c_platform)

add_executable(hx_demo main.cpp)
target_link_libraries(hx_demo PRIVATE hx_c_platform::cpp)
set_target_properties(hx_demo PROPERTIES
	CXX_STANDARD 11
	CXX_STANDARD_REQUIRED YES
	CXX_EXTENSIONS NO)

if(MSVC)
	target_compile_options(hx_demo PRIVATE /EHs-c- /utf-8)
	target_compile_definitions(hx_demo PRIVATE _HAS_EXCEPTIONS=0)
else()
	target_compile_options(hx_demo PRIVATE -fno-exceptions)
endif()
```

Linking `hx_c_platform::cpp` adds the header-only C++11 facade on top of the C core. First-party C++ APIs report outcomes through `hx::Result<T>` rather than throwing exceptions, so they work cleanly with C++ exceptions disabled (`/EHs-c-` or `-fno-exceptions`). Always check whether a `Result` succeeded before calling `.value()`.

<a id="installed-project"></a>

### 2.4 Use an existing include/lib/bin SDK

If you already have a prebuilt SDK package (`include`, `lib`, and `bin`), place the complete SDK under `deps/hx_c_platform` in your project—no source checkout of `hx_c_platform` is needed. Follow the [standalone SDK guide](INSTALL.md) for copy-pasteable C99 and C++11 `CMakeLists.txt` files, sample sources, Windows and Linux commands, and `cmake-gui` / Visual Studio instructions.

> [!IMPORTANT]
> Keep the CMake package configuration files under `lib/cmake` along with the matching runtime libraries under `bin` or `lib`, and import the prebuilt targets with `find_package(hx_c_platform CONFIG REQUIRED)` (do not call `add_subdirectory` on an installed SDK or enable source-tree example switches). Copying only the header files and main library archive without the CMake export metadata and runtime dependencies will cause link or runtime failures.

### 2.5 Read the recurring notation

| Idiom | What it means in code |
| --- | --- |
| `hx_os_status status` | Holds a C function's status code; compare against `HX_OS_OK` to check for success |
| `hx_string text = HX_STRING_INIT` | Initializes an empty UTF-8 string ready to receive or store text |
| `&text` | Passes the string's address so the function can write output into it |
| Final `NULL` error argument | Skips detailed native OS error records; you still check the returned `hx_os_status` |
| C++ `if (!result)` | Checks whether an `hx::Result<T>` call failed |
| C++ `result.value()` | Retrieves the contained string, number, or object after confirming success |

Note that what `NULL` means depends on which parameter receives it—not every pointer parameter is optional. Each section below explains the arguments used in its recipe.

<a id="files"></a>

## 3. Read and display a text file

**Scenario**: Read the UTF-8 text file `hello.txt` into memory in one call and print its contents, without manually opening file handles or writing a read loop.

### 3.1 Complete C program

```c
#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_string text = HX_STRING_INIT;

	hx_os_status status = hx_os_file_read_text("hello.txt", &text, NULL);
	if (status == HX_OS_OK)
		puts(hx_string_c_str(&text));
	else
		fprintf(stderr, "Read failed: %s\n", hx_os_status_name(status));

	hx_string_destroy(&text);

	return status == HX_OS_OK ? 0 : 1;
}
```

How this works step by step:
1. `HX_STRING_INIT` initializes `text` as an empty string ready to receive the file contents.
2. `hx_os_file_read_text("hello.txt", &text, NULL)` opens the file, reads and validates its UTF-8 text into `&text`, and closes the file handle automatically. Passing `NULL` as the final argument skips detailed OS error records while still returning the `hx_os_status` code.
3. On `HX_OS_OK`, `hx_string_c_str(&text)` returns a NUL-terminated C string pointer for `puts`.
4. Call `hx_string_destroy(&text)` before exiting on both success and failure paths to free any allocated buffer.

### 3.2 Complete C++ program

```cpp
#include <hx/Files.hpp>
#include <cstdio>

int main()
{
	auto text = hx::Files::readText("hello.txt");
	if (!text) {
		std::fprintf(stderr, "Read failed: %s\n",
			hx_os_status_name(text.status()));

		return 1;
	}

	std::puts(text.value().c_str());

	return 0;
}
```

In C++, `hx::Files::readText` returns `hx::Result<hx::String>`. Check `if (!text)` first to handle errors before accessing `text.value().c_str()`. `hx::String` automatically frees its buffer when it goes out of scope.

**Expected output:**

```text
Hello, hx_c_platform!
```

> [!NOTE]
> - If reading fails with `HX_OS_NOT_FOUND`, check that `hello.txt` is in your current working directory.
> - `hx_os_file_read_text` and `hx::Files::readText` validate strict UTF-8, strip a leading UTF-8 BOM if present, and enforce a default 16 MiB size limit (returning an error on invalid encoding or oversized files). To read binary files, legacy encodings (such as GBK or UTF-16), or files larger than 16 MiB, see the [UTF-8 strings and encoding reference](REFERENCE.md#section-7).

<a id="strings"></a>

## 4. Edit strings

**Scenario**: Strip leading and trailing spaces from `"  Hello  "` and convert its ASCII uppercase letters to lowercase. No external input file is needed.

### 4.1 Complete C program

```c
#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_string text = HX_STRING_INIT;

	hx_os_status status = hx_string_assign_cstr(&text, "  Hello  ", NULL);
	if (status == HX_OS_OK) {
		hx_string_trim_ascii_in_place(&text);
		hx_string_lower_ascii_in_place(&text);
		puts(hx_string_c_str(&text));
	}
	else
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	hx_string_destroy(&text);

	return status == HX_OS_OK ? 0 : 1;
}
```

### 4.2 Complete C++ program

```cpp
#include <hx/Values.hpp>
#include <cstdio>

int main()
{
	auto text = hx::String::copy("  Hello  ");
	if (!text) {
		std::fprintf(stderr, "%s\n", hx_os_status_name(text.status()));

		return 1;
	}

	text.value().trimAscii().lowerAscii();
	std::puts(text.value().c_str());

	return 0;
}
```

**Expected output:**

```text
hello
```

Initializing or copying the string (`hx_string_assign_cstr` / `hx::String::copy`) allocates memory, so we check its status first. Once the string is allocated, `hx_string_trim_ascii_in_place` and `hx_string_lower_ascii_in_place` (or `.trimAscii().lowerAscii()` in C++) mutate the buffer in place without allocating a second string, so they cannot fail and require no extra error checks.

> [!NOTE]
> `trim_ascii` and `lower_ascii` only modify ASCII whitespace and `A`–`Z` characters; multi-byte UTF-8 sequences such as Chinese text or emoji are preserved untouched. For splitting, joining, formatting, or full Unicode normalization and case folding, see [string utilities](COMMON_UTILITIES.md).

<a id="configuration"></a>

## 5. Read JSON configuration

**Scenario**: Parse `config.json` (`{"port": 8080}`) and read the root integer field `port`.

### 5.1 Complete C program

```c
#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_os_config_document *document = NULL;
	const hx_os_config_node *node = NULL;
	int64_t port = 0;

	hx_os_status status = hx_os_config_load(
		NULL, HX_OS_CONFIG_JSON, "config.json", NULL, &document, NULL);
	if (status == HX_OS_OK)
		status = hx_os_config_pointer(
			hx_os_config_root(document), "/port", &node);

	if (status == HX_OS_OK)
		status = hx_os_config_get_i64(node, &port);

	if (status == HX_OS_OK)
		printf("Port: %lld\n", (long long)port);
	else
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	hx_os_config_release(document);

	return status == HX_OS_OK ? 0 : 1;
}
```

The C workflow has three steps:
1. `hx_os_config_load` reads and parses `config.json` into an immutable `document`.
2. `hx_os_config_pointer(hx_os_config_root(document), "/port", &node)` locates the `/port` node using JSON Pointer syntax.
3. `hx_os_config_get_i64(node, &port)` extracts the node's value as a 64-bit signed integer.
4. `hx_os_config_release(document)` frees the document and all nodes belonging to it. Node pointers (`node`) are borrowed from `document`—never free them separately or dereference them after releasing the document.

### 5.2 Complete C++ program

```cpp
#include <hx/Config.hpp>
#include <cstdio>

int main()
{
	auto config = hx::ConfigDocument::load("config.json");
	if (!config) {
		std::fprintf(
			stderr, "%s\n", hx_os_status_name(config.status()));

		return 1;
	}

	auto port = config.value().integer("/port");
	if (!port) {
		std::fprintf(stderr, "%s\n", hx_os_status_name(port.status()));

		return 1;
	}

	std::printf("Port: %lld\n", static_cast<long long>(port.value()));

	return 0;
}
```

**Expected output:**

```text
Port: 8080
```

> [!NOTE]
> - **JSON Pointer paths**: `"/port"` looks up `port` at the root object. For nested JSON such as `{"server": {"port": 8080}}`, use `"/server/port"` (levels are separated by `/`, not `.`).
> - **No silent fallback to zero**: Syntax errors, missing keys (`HX_OS_NOT_FOUND`), or non-integer values (`HX_OS_TYPE_MISMATCH`) return explicit errors rather than silently producing `0`.
> - **Read-only documents vs. mutable settings**: Parsed `hx_os_config_document` objects (bounded to 1 MiB by default) are read-only. To modify settings in memory at runtime, use `hx_settings` in section 6. For YAML, TOML, INI, XML, and `.properties` profiles, see [configuration formats](CONFIGURATION.md).

<a id="settings"></a>

## 6. Change settings in memory

**Scenario**: Create a mutable in-memory settings object (`hx_settings`), set `port` to `8080`, and read it back. Unlike a read-only parsed `hx_os_config_document`, `hx_settings` supports runtime updates, fallback defaults, and immutable snapshots.

### 6.1 Complete C program

```c
#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_settings *settings = NULL;
	int64_t port = 0;

	hx_os_status status = hx_settings_create(NULL, &settings, NULL);
	if (status == HX_OS_OK)
		status = hx_settings_set_i64(settings, "port", 8080, NULL);

	if (status == HX_OS_OK)
		status = hx_settings_get_i64(settings, "port", &port, NULL);

	if (status == HX_OS_OK)
		printf("Port: %lld\n", (long long)port);

	hx_os_status closed = hx_settings_destroy(&settings, NULL);
	if (status == HX_OS_OK)
		status = closed;

	if (status != HX_OS_OK)
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	return status == HX_OS_OK ? 0 : 1;
}
```

`hx_settings_create` allocates the settings object, `hx_settings_set_i64` writes the integer value, `hx_settings_get_i64` reads it back, and `hx_settings_destroy` closes and releases the object. Because closing also returns a status code, we preserve any earlier error first and otherwise check `closed`.

### 6.2 Complete C++ program

```cpp
#include <hx/Settings.hpp>
#include <cstdio>

int main()
{
	hx_settings *owner = NULL;

	hx::Settings settings(owner);

	auto created = settings.create();
	if (!created) {
		std::fprintf(
			stderr, "%s\n", hx_os_status_name(created.status()));

		return 1;
	}

	int exit_code = 1;

	auto changed = settings.setInt("port", 8080);
	if (changed) {
		auto port = settings.integer("port");
		if (port) {
			std::printf("Port: %lld\n",
				static_cast<long long>(port.value()));
			exit_code = 0;
		}
		else
			std::fprintf(stderr, "%s\n",
				hx_os_status_name(port.status()));
	}
	else
		std::fprintf(
			stderr, "%s\n", hx_os_status_name(changed.status()));

	auto closed = settings.closeUntil(HX_OS_DEADLINE_NOW);
	if (!closed) {
		std::fprintf(
			stderr, "%s\n", hx_os_status_name(closed.status()));

		return 1;
	}

	return exit_code;
}
```

**Expected output:**

```text
Port: 8080
```

In C++, `owner` holds the underlying `hx_settings *` pointer while `hx::Settings settings(owner)` provides the C++ wrapper; `settings.create()` allocates the resource. Before exiting, `settings.closeUntil(HX_OS_DEADLINE_NOW)` closes it immediately (this example has no active observer callbacks or concurrent callers).

### 6.3 Write several values

To write multiple settings in a chain without checking after every single call, replace `auto changed = settings.setInt("port", 8080);` in section 6.2 with:

```cpp
auto changed = settings.update()
		       .setInt("port", 8080)
		       .setBool("enabled", true)
		       .finish();
```

On success, read the values back with `settings.integer("port")` and `settings.boolean("enabled")`.

> [!IMPORTANT]
> Each setter in `settings.update()` **applies immediately** when called—`finish()` does not buffer or roll back a transaction. If the first setter succeeds and the second fails, the first update remains committed and subsequent setters in the chain are skipped; `changed.value()` reports how many setters succeeded before the error occurred. To persist settings to disk, explicitly call `settings.save("settings.json")` and check its result. For default layers, observers, retained snapshots, and close retries, see [mutable settings contracts](PLATFORM_UTILITIES.md#mutable-settings-and-retained-snapshots-batch-23b).

<a id="containers"></a>

## 7. Use C vector and queue

### 7.1 vector: store integers in order

**Scenario**: Create a dynamically resizable contiguous array (`vector`) of integers, append `10` and `20`, and print the element count and first item.

Place `HX_OS_DECLARE_VECTOR(int_vector, int)` at file scope outside any function. It generates type-safe C functions (`int_vector_create`, `int_vector_push`, etc.) without C++ templates. This example caps capacity at 16 elements:

```c
#include <hx_os.h>
#include <stdio.h>

HX_OS_DECLARE_VECTOR(int_vector, int)

int main(void)
{
	int_vector *numbers = NULL;

	hx_os_status status = int_vector_create(NULL, 16, &numbers, NULL);
	if (status == HX_OS_OK)
		status = int_vector_push(numbers, 10, NULL);

	if (status == HX_OS_OK)
		status = int_vector_push(numbers, 20, NULL);

	if (status == HX_OS_OK)
		printf("Count: %zu, first: %d\n", int_vector_size(numbers),
			*int_vector_at(numbers, 0));

	hx_os_status closed = int_vector_destroy(&numbers, NULL);
	if (status == HX_OS_OK)
		status = closed;

	if (status != HX_OS_OK)
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	return status == HX_OS_OK ? 0 : 1;
}
```

**Expected output:**

```text
Count: 2, first: 10
```

`int_vector_create` allocates the vector, `int_vector_push` appends elements, `int_vector_size` returns the item count, and `int_vector_at(numbers, 0)` returns a pointer to the element at index `0` (or `NULL` if out of bounds; here both pushes succeeded, so dereferencing index `0` is safe). Finally, `int_vector_destroy` frees the container.

### 7.2 queue: first in, first out

**Scenario**: Enqueue `10` followed by `20` into a FIFO queue, pop the front item (`10`), and check that 1 item remains.

```c
#include <hx_os.h>
#include <stdio.h>

HX_OS_DECLARE_QUEUE(int_queue, int)

int main(void)
{
	int_queue *queue = NULL;
	int value = 0;

	hx_os_status status = int_queue_create(NULL, 16, &queue, NULL);
	if (status == HX_OS_OK)
		status = int_queue_push(queue, 10, NULL);

	if (status == HX_OS_OK)
		status = int_queue_push(queue, 20, NULL);

	if (status == HX_OS_OK)
		status = int_queue_pop(queue, &value, NULL);

	if (status == HX_OS_OK)
		printf("Removed: %d, remaining: %zu\n", value,
			int_queue_size(queue));

	hx_os_status closed = int_queue_destroy(&queue, NULL);
	if (status == HX_OS_OK)
		status = closed;

	if (status != HX_OS_OK)
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	return status == HX_OS_OK ? 0 : 1;
}
```

**Expected output:**

```text
Removed: 10, remaining: 1
```

`int_queue_push` adds an item to the back of the queue, and `int_queue_pop` removes the front item into `value`. These C containers can also be called directly from C++.

> [!WARNING]
> The typed macros `HX_OS_DECLARE_VECTOR` and `HX_OS_DECLARE_QUEUE` are designed for trivially byte-copyable (POD) values such as `int` or plain structs. Do not pass owning types like `hx_string` or non-trivial C++ classes to these macros; for elements that require deep copying and custom cleanup, use the generic container APIs with `copy` / `drop` callbacks described in [C containers](CONTAINERS.md).

<a id="thread-pool"></a>

## 8. Submit a C++ pool task

**Scenario**: Create a thread pool with 2 worker threads, submit a task that computes `21 * 2`, wait for the result, and shut down the pool cleanly. The argument `21` is passed by value so the worker thread does not borrow a local stack pointer from the caller.

When working with the thread pool, keep task admission (`submit`) separate from task completion (`getUntil`):

```mermaid
flowchart LR
    A["1. Create pool"] --> B["2. Submit task (admission)"]
    B --> C["3. Wait and check result"]
    C --> D["4. Explicitly shut down pool"]
```

```cpp
#include <hx/ThreadPool.hpp>
#include <cstdio>

int main()
{
	hx_os_thread_pool *owner = NULL;

	auto created = hx::ThreadPool::withThreads(owner, 2);
	if (!created) {
		std::fprintf(
			stderr, "%s\n", hx_os_status_name(created.status()));

		auto closed = hx::ThreadPool(owner).shutdownUntil(
			hx_os_deadline_after_ms(2000));
		if (!closed)
			std::fputs("Cleanup pending; keep owner to retry.\n",
				stderr);

		return 1;
	}

	auto &pool = created.value();

	auto submitted =
		pool.submit([](int value) noexcept { return value * 2; }, 21);
	int exit_code = 1;

	if (submitted) {
		auto answer = submitted.value().getUntil(
			hx_os_deadline_after_ms(2000));
		if (answer) {
			std::printf("Answer: %d\n", answer.value());
			exit_code = 0;
		}
		else
			std::fprintf(stderr, "%s\n",
				hx_os_status_name(answer.status()));
	}
	else
		std::fprintf(
			stderr, "%s\n", hx_os_status_name(submitted.status()));

	auto closed = pool.shutdownUntil(
		hx_os_deadline_after_ms(2000), HX_OS_POOL_CANCEL_PENDING);
	if (!closed) {
		std::fprintf(stderr, "Close pending: %s\n",
			hx_os_status_name(closed.status()));

		return 1;
	}

	return exit_code;
}
```

**Expected output:**

```text
Answer: 42
```

Key steps:
1. `hx::ThreadPool::withThreads(owner, 2)` starts two worker threads and stores the underlying pool pointer in `owner` for shutdown and retry.
2. `pool.submit(...)` enqueues a `noexcept` callable along with its argument `21`.
3. `if (submitted)` confirms the task was accepted into the queue; `submitted.value().getUntil(...)` then waits for the worker to finish and returns `answer`. Check `if (answer)` before reading `answer.value()`.
4. `hx_os_deadline_after_ms(2000)` computes a deadline 2 seconds from now (2 seconds is simply the timeout chosen for this demo, not a hardcoded library policy).
5. `pool.shutdownUntil(..., HX_OS_POOL_CANCEL_PENDING)` closes and releases the pool. `HX_OS_POOL_CANCEL_PENDING` cancels queued tasks that have not started yet and waits for running tasks to return naturally, never force-killing worker threads.

> [!IMPORTANT]
> **Shutdown retry contract in long-running applications**: While this short CLI demo exits with an error if shutdown times out (`HX_OS_TIMEOUT`), the `hx::ThreadPool` wrapper destructor **never** blocks indefinitely behind the scenes and **never** discards a live `owner` pointer. In a long-running service, keep `owner`, any data borrowed by running tasks, and loaded plugin code alive in your shutdown state machine if `shutdownUntil` returns `HX_OS_TIMEOUT`, and retry closing later until it returns `HX_OS_OK`. Before using long-running tasks, cooperative cancellation, concurrent close, or plugin unloading, read the [C++ pool contracts](CPP_POOL.md). C programs can use the underlying C thread pool directly; see the [C thread pool reference](REFERENCE.md#section-15).

<a id="variant"></a>

## 9. Store different types in one value

**Scenario**: Use `hx_variant` (or `hx::TypedVariant` in C++) to hold an integer (`42`), then a string (`"hello"`), and finally a custom `Point{2, 5}` struct in the same variable. Assigning a new value automatically releases the previous value's storage.

### 9.1 Complete C program

No input files are required; build with the C configuration from section 2.2:

```c
#include <hx_variant.h>
#include <stdio.h>

typedef struct point {
	double x;
	double y;
} point;
HX_VARIANT_DEFINE_TRIVIAL_TYPE(point_type, point);

int main(void)
{
	hx_variant value = HX_VARIANT_INIT;
	point position = {2, 5};
	const void *object = NULL;
	hx_string_view text;
	int32_t count;
	hx_os_status status;

	if (!(hx_os_capabilities() & HX_OS_CAP_VARIANT)) {
		puts("Variant support is disabled");

		return 0;
	}

	status = hx_variant_set_i32(&value, 42, NULL);
	if (status != HX_OS_OK)
		goto failed;

	status = hx_variant_get_i32(&value, &count, NULL);
	if (status != HX_OS_OK)
		goto failed;

	printf("Integer: %d\n", (int)count);

	status = hx_variant_set_cstr(&value, "hello", NULL);
	if (status != HX_OS_OK)
		goto failed;

	status = hx_variant_get_string_view(&value, &text, NULL);
	if (status != HX_OS_OK)
		goto failed;

	printf("String: %.*s\n", (int)text.size, text.data);

	status = hx_variant_set_custom(&value, &point_type, &position, NULL);
	if (status != HX_OS_OK)
		goto failed;

	status = hx_variant_get_custom(&value, &point_type, &object, NULL);
	if (status != HX_OS_OK)
		goto failed;

	printf("Point: %.1f, %.1f\n", ((const point *)object)->x,
		((const point *)object)->y);

	hx_variant_destroy(&value);

	return 0;

failed:
	fprintf(stderr, "Variant failed: %s\n", hx_os_status_name(status));
	hx_variant_destroy(&value);

	return 1;
}
```

### 9.2 Complete C++ program

In C++11, `hx::TypedVariant<...>` declares the allowed alternative types directly in its template parameter list (build with the exception-free C++ configuration from section 2.3):

```cpp
#include <hx/Variant.hpp>
#include <cstdio>

struct Point {
	double x;
	double y;
};

int main()
{
	if (!(hx_os_capabilities() & HX_OS_CAP_VARIANT)) {
		std::puts("Variant support is disabled");

		return 0;
	}

	hx::TypedVariant<int32_t, double, hx::StringView, Point> value;

	if (!value.set(int32_t(42)))
		return 1;

	auto count = value.get<int32_t>();

	if (!count)
		return 1;

	std::printf("Integer: %d\n", static_cast<int>(count.value()));

	if (!value.set("hello"))
		return 1;

	auto text = value.get<hx::StringView>();

	if (!text)
		return 1;

	std::printf("String: %.*s\n", static_cast<int>(text.value().size()),
		text.value().data());

	if (!value.set(Point{2, 5}))
		return 1;

	auto position = value.get<Point>();

	if (!position)
		return 1;

	std::printf(
		"Point: %.1f, %.1f\n", position.value().x, position.value().y);
}
```

**Expected output for both programs:**

```text
Integer: 42
String: hello
Point: 2.0, 5.0
```

In C, `HX_VARIANT_DEFINE_TRIVIAL_TYPE(point_type, point)` generates the type descriptor for a plain POD struct; in C++, the template generates it automatically. Calling `set` stores an owned copy and releases any previous value. C destroys the final variant with `hx_variant_destroy(&value)`, while C++ cleans it up automatically at scope exit.

> [!NOTE]
> Reading the wrong type fails explicitly with `HX_OS_TYPE_MISMATCH` (for example, calling `get<int32_t>()` while `value` holds a `Point`). If your custom struct owns an `hx_string` or heap memory, define custom copy and drop callbacks instead of using `HX_VARIANT_DEFINE_TRIVIAL_TYPE`; see the [variant guide](VARIANT.md) for custom callbacks, `emplace` / `visit`, heterogeneous vectors, and custom allocators.

<a id="questions"></a>

## 10. First-use questions

### 10.1 Why separate `HX_OS_OK`, `Result`, and `.value()`?

Files can be missing, configuration files can be malformed, and memory allocation can fail. C functions report outcomes via `hx_os_status` (`HX_OS_OK` means success), while C++ methods wrap the value or status inside `hx::Result<T>` (`if (!result)` checks for failure and `result.value()` accesses the value). This prevents failures from silently turning into empty strings or `0`. When you need native OS error codes and formatted messages, pass an `hx_os_error` record; see [error handling](REFERENCE.md#section-6).

### 10.2 Does `NULL` always mean the same thing?

No—its meaning depends on the parameter position:
- **Allocator context (`context`) or `options` parameter**: `NULL` selects the default allocator or default options.
- **Final `error` output parameter**: `NULL` skips detailed native error capture while still returning the `hx_os_status` code.
- **Required object handles or output pointers**: Cannot be `NULL` (passing `NULL` returns `HX_OS_INVALID`).

### 10.3 Why release C resources manually, and why explicitly close some C++ objects?

C has no destructors, so strings, config documents, and containers must be released explicitly with their `destroy` or `release` functions.

In C++, value objects like `hx::String`, `hx::ConfigDocument`, and `hx::TypedVariant` release their memory automatically at scope exit. However, asynchronous or concurrent owners such as `hx::ThreadPool` and `hx::Settings` may have in-flight background tasks or observer callbacks when closing. Explicit calls like `shutdownUntil` and `closeUntil` let you pass a deadline, check whether shutdown completed, and retain the owner for retry if a timeout (`HX_OS_TIMEOUT`) occurs. Never free borrowed data or unload plugin code while an owner is still active.

### 10.4 Why can Chinese text look garbled in a Windows terminal?

All paths and strings in `hx_c_platform` use UTF-8 (on Windows, file and system adapters convert UTF-8 to UTF-16 internally). However, reading UTF-8 text from disk and rendering UTF-8 bytes in a terminal window are separate steps: if your Windows console is still using a legacy ANSI codepage (such as codepage 936), printing raw UTF-8 bytes to `stdout` will look garbled. The library never alters process-wide console codepages or `locale` behind your back; run `chcp 65001` or use a UTF-8 terminal when inspecting UTF-8 console output on Windows.

### 10.5 Why does an example fail after I disable CMake module switches?

The starter `CMakeLists.txt` in section 2 keeps the modules required by these tutorials enabled. If you disable a required module (for example, JSON parsing requires `HX_OS_ENABLE_CONFIG`, containers require `HX_OS_ENABLE_CONTAINERS`, variants require `HX_OS_ENABLE_VARIANT`, mutable settings require `HX_OS_ENABLE_SETTINGS`, and thread pools require `HX_OS_ENABLE_THREAD_POOL`), calls into that module return `HX_OS_NOT_SUPPORTED` instead of faking success. For the full list of feature switches, optional third-party parsers, and minimal embedded configurations, see [build options and optional dependencies](REFERENCE.md#section-27).

### 10.6 Where do I go for other features?

| What you want to do | Guide |
| --- | --- |
| Format dates/filenames, load translation catalogs, tokenize text, or traverse directories | [Common utilities](COMMON_UTILITIES.md) |
| Recursively search for files by pattern or run file searches on a thread pool | [File search](FILE_SEARCH.md) |
| Use sockets, capture child process output, open pipes, map files, or query system metrics | [Platform utilities](PLATFORM_UTILITIES.md) |
| Configure TCP keepalive, select a multicast interface, or list local IP addresses | [Socket options and discovery](NETWORK_OPTIONS.md) |
| Understand mutexes, events, thread-local storage (TLS), and resource lifetime rules | [Core contracts](CONTRACTS.md) |
| Install static/shared builds and consume them via `find_package` | [Installation reference](REFERENCE.md#section-5) |
| Check OS baselines, compiler toolchains, and platform limits | [Platform support](PLATFORMS.md) |
