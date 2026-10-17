# hx_c_platform task tutorials

[简体中文](USAGE.zh-CN.md) · [Home](../README.md) · [Detailed reference](REFERENCE.md)

Choose further tasks from the [runnable example directory](../examples/README.md), which lists use cases, build targets, inputs and expected results.

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

Each section solves one concrete problem, such as reading a port from configuration or sending a calculation to a worker. Complete section 2, then pick a task. Each C/C++ block is an independent program: save one as `main.c` or `main.cpp`; do not combine several main functions.

Follow this order: prepare input, build/run, compare output, then substitute application data. Custom allocators and platform internals are unnecessary for these tasks; consult the [reference](REFERENCE.md) when you need them.

<a id="project"></a>

## 2. Prepare a small project

### 2.1 Directory and source

For source integration, run in an MSVC developer terminal or Linux shell:

```sh
mkdir hx_demo
cd hx_demo
mkdir deps
cd deps
git clone --branch dev https://github.com/HuangX666/hx_c_platform.git
git -C hx_c_platform submodule update --init --recursive
cd ..
```

You are now in `hx_demo`. Create `CMakeLists.txt` and the selected program here. Only file/configuration examples need input files; other examples run without them.

```text
hx_demo/
├── CMakeLists.txt
├── main.c
├── hello.txt       # Needed by the text-file example.
├── config.json     # Needed by the configuration example.
└── deps/
    └── hx_c_platform/
```

The file example uses UTF-8 `hello.txt`:

```text
Hello, hx_c_platform!
```

The configuration example uses UTF-8 `config.json`:

```json
{"port": 8080}
```

<a id="c-project"></a>

### 2.2 Build a C program

Save the selected C program as `main.c` and use this complete `CMakeLists.txt`:

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

`add_subdirectory` builds the library; `target_link_libraries` connects it to your program. The seven switches disable modules needing a separate native SDK. Files, strings, built-in JSON, settings, containers and pools remain available for these tutorials. To add those modules, prepare their SDKs using the [dependency guide](DEPENDENCIES.md), then enable them with the corresponding `-DHX_OS_ENABLE_MODULE=ON` configure option.

Run from `hx_demo`:

```sh
cmake -S . -B build
cmake --build build --config Release
```

For a Visual Studio Release build on Windows:

```powershell
.\build\Release\hx_demo.exe
```

For a Ninja/Makefile build on Linux:

```sh
./build/hx_demo
```

Stay in `hx_demo` when running so relative input paths resolve here. Replace `main.c` and rebuild to try another task; reuse the directory with the same toolchain. A Windows Ninja executable is normally `build/hx_demo.exe`.

<a id="cpp-project"></a>

### 2.3 Build a C++ program

Save the selected C++ program as `main.cpp` and use this complete configuration. Build/run commands remain the same as section 2.2.

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

`hx_c_platform::cpp` supplies C++ wrappers and links the same C core. This setup selects C++11 and disables exceptions. Check a C++ Result for success before using its `.value()`.

<a id="installed-project"></a>

### 2.4 Use an existing include/lib/bin SDK

Place the complete SDK in your application's `deps/hx_c_platform`; no library source checkout is required. Follow the [standalone SDK guide](INSTALL.md) for complete C99 and C++11 CMakeLists.txt files, mains, input, Windows/Linux commands and cmake-gui/Visual Studio launch steps.

Preserve the core and dependency metadata under lib/cmake and matching runtimes under bin or lib. Use find_package to import the prebuilt library, without add_subdirectory or source-example build switches. Selecting only headers and the main library while omitting metadata or dependencies is not a complete link/runtime setup.

### 2.5 Read the recurring notation

| Form | Meaning you need here |
| --- | --- |
| `hx_os_status status` | The operation's outcome; compare with `HX_OS_OK` for success |
| `hx_string text = HX_STRING_INIT` | An empty string ready to receive or store text |
| `&text` | Allows the function to write into that string |
| Final `NULL` error argument | Omits detailed native diagnostics; still check the status |
| C++ `if (!result)` | Checks failure |
| C++ `result.value()` | Obtains the actual string, number or object after success |

The meaning of NULL depends on its parameter position; not every pointer is optional. Each task explains the arguments it uses.

<a id="files"></a>

## 3. Read and display a text file

**Goal:** read `hello.txt` and display its contents. Section 2 prepares the input. You do not create a file handle or write a read loop.

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

The sequence is:

1. `HX_STRING_INIT` creates an empty string to receive the content.
2. `hx_os_file_read_text` fills `&text`. Its final `NULL` declines extra diagnostics; the status is still checked.
3. Use the result only on `HX_OS_OK`. `hx_string_c_str(&text)` exposes a NUL-terminated pointer for `puts`.
4. Call `hx_string_destroy` on success or failure. The read function already closed the file.
5. The program returns 0 on success and 1 on failure.

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

`readText` returns `Result<hx::String>`, containing a string or an error. `if (!text)` handles failure before accessing `text.value()`. `hx::String` manages its own memory; no manual string release is needed.

**Normal output for either program:**

```text
Hello, hx_c_platform!
```

If the file is missing, check the working directory and `hello.txt`. This convenience reads strict UTF-8, removes an initial UTF-8 BOM and limits input to 16 MiB. Invalid encoding or excess input fails. Use extended entries for other encodings, binary data or larger files; see the [file/encoding reference](REFERENCE.md#section-7).

<a id="strings"></a>

## 4. Edit strings

**Goal:** remove spaces around `"  Hello  "` and convert ASCII uppercase letters to lowercase. This task needs no input file.

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

**Normal output:**

```text
hello
```

Assignment/copy can allocate, so check it before editing. `trim` removes surrounding whitespace; `lower` converts ASCII uppercase letters. In-place edits create no second string and need no extra result checks. Chinese text and emoji remain unchanged; these entries are not full Unicode case conversion.

C calls `hx_string_destroy`; C++ releases automatically. For splitting, joining, conversion or independent copies, see [string utilities](COMMON_UTILITIES.md).

<a id="configuration"></a>

## 5. Read JSON configuration

**Goal:** read `config.json` and retrieve its root integer `port`. The input is `{"port":8080}`: `port` is the key and `8080` its value.

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

C uses three steps: `load` reads/parses the file; `pointer(..., "/port", ...)` selects the node; `get_i64` reads a 64-bit integer. A failed step skips later queries. `hx_os_config_release` releases the document and its nodes; do not separately free or reuse a node afterward.

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

**Normal output:**

```text
Port: 8080
```

`/port` selects `port` at the root. For `{"server":{"port":8080}}`, use `/server/port`. It is not a filesystem path; dots do not separate levels here.

Malformed files, absent keys and failed integer conversion report failure rather than silently becoming 0. Default config input is bounded to 1 MiB. Documents are read-only after creation; use `hx_settings` in the next task for mutable settings. See [configuration formats](CONFIGURATION.md) for other formats and backend differences.

<a id="settings"></a>

## 6. Change settings in memory

**Goal:** create an in-memory settings object, write `port=8080` and read it back. This example reads/saves no file, demonstrating the purpose of settings versus a parsed document.

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

`create` makes the object, `set_i64` writes an integer, `get_i64` reads it, and `destroy` closes/releases it. Closing also returns a status. This example preserves an earlier error, or checks the close result when prior work succeeded.

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

**Normal output:**

```text
Port: 8080
```

`owner` stores the internal object's pointer. `hx::Settings settings(owner)` provides C++ methods; `create()` actually creates the resource. `setInt` writes an integer, `integer` returns the read result, and `closeUntil` explicitly closes. `HX_OS_DEADLINE_NOW` attempts close immediately; this example has no observers or concurrent callers.

### 6.3 Write several values

In the program in section 6.2, replace `auto changed = settings.setInt("port", 8080);` with the assignment below to write two values. Keep the following result check, read and close steps from that program:

```cpp
auto changed = settings.update()
		       .setInt("port", 8080)
		       .setBool("enabled", true)
		       .finish();
```

On success, read with `settings.integer("port")` and `settings.boolean("enabled")`, checking each result. `finish()` does not commit delayed work; it reports the writes already performed. Every setter applies immediately.

If the first succeeds and the second fails, the first remains and later writes are skipped. For this interface, `changed.value()` also contains the successful count on failure. Explicitly call and check `settings.save("settings.json")` to save a file. [Settings contracts](PLATFORM_UTILITIES.md#mutable-settings-and-retained-snapshots-batch-23b) cover defaults, observers, snapshots and close retries.

<a id="containers"></a>

## 7. Use C vector and queue

### 7.1 vector: store integers in order

**Goal:** store `10` and `20`, then read the count and first value. A vector is a growable contiguous array; this example limits it to 16 items. Put the macro outside functions; it generates named C functions for the element type without C++ templates.

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

**Normal output:**

```text
Count: 2, first: 10
```

`int_vector_create` creates the integer container; `push` appends; `size` reports count; `at(..., 0)` points to the first integer. Both appends succeeded before this example reads it. Indexes start at 0; out-of-range `at` returns NULL. `destroy` releases the container.

### 7.2 queue: first in, first out

**Goal:** enqueue `10`, then `20`. Removing one item gives `10` and leaves one item queued.

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

**Normal output:**

```text
Removed: 10, remaining: 1
```

`push` adds at the back; `pop` removes from the front into `value`. These typed macros suit integers and other byte-copyable values. Owning strings and nontrivial C++ objects need copy/release-aware entries; do not simply replace `int` with `hx_string`. C++ can also call these C containers. See [C containers](CONTAINERS.md) for other kinds and element rules.

<a id="thread-pool"></a>

## 8. Submit a C++ pool task

**Goal:** create two workers, submit `21 × 2`, wait for the result and close the pool. No input file is needed. `21` is passed by value, not through a borrowed local pointer.

The diagram separates the steps. Admission means the task was accepted; result retrieval confirms its outcome.

```mermaid
flowchart LR
    A["Create pool"] --> B["Submit calculation"]
    B --> C["Wait and check result"]
    C --> D["Explicitly close pool"]
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

**Normal output:**

```text
Answer: 42
```

Key steps:

1. `withThreads(owner, 2)` creates two workers. `owner` retains the internal pool for closing/retry.
2. `submit` takes a callable and arguments. `value * 2` is the work; `noexcept` declares its no-exception contract.
3. On accepted submission, `submitted.value()` gives the task. `getUntil` returns `answer`, which still needs checking: acceptance is not successful execution.
4. `hx_os_deadline_after_ms(2000)` calculates a waiting deadline two seconds from now. Result retrieval and shutdown each use the example's chosen budget; the library does not impose two seconds on every application.
5. `shutdownUntil` closes/releases the pool. `HX_OS_POOL_CANCEL_PENDING` skips work not yet started and waits for active work to exit without forcibly killing it.

This complete program is a standalone command-line demonstration; failed close reports the problem and ends with failure. In a long-running application, keep `owner` in persistent state covering close retries. A timeout requires retaining the pool, task data and relevant code before retrying close. Destroying the `hx::ThreadPool` facade neither waits secretly nor discards that pointer. Read [pool contracts](CPP_POOL.md) before using long tasks, cancellation, concurrent close or plugin unload.

C programs can use the underlying C pool. See the [C pool reference](REFERENCE.md#section-15) for creation, submission, waiting and closing.

<a id="variant"></a>

## 9. Store different types in one value

A variant holds one value at a time but can change that value's type. Replacement releases the old contents. The C example stores 42, then hello, then Point(2, 5). No input files are needed; use the C build configuration from section 2:

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

The C++11 version declares integer, floating-point, string and Point alternatives. Use section 2's C++ configuration with exceptions disabled:

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

Both programs print:

```text
Integer: 42
String: hello
Point: 2.0, 5.0
```

C declares point_type to describe copying the ordinary structure; C++ supplies it from the type. Set owns a copy. Check results before using a number, string view or structure reference. Replacement releases the former value; C destroys the final owner, and C++ releases it on scope exit.

Reading the wrong type fails explicitly: a Point is not readable with get<int32_t>. Structures with hx_string/heap resources need proper copy/drop operations. Find callbacks, emplace/visit, mixed vector and allocator details in the [variant guide](VARIANT.md).

<a id="questions"></a>

## 10. First-use questions

### 10.1 Why HX_OS_OK, Result and value?

Files can be absent, configuration invalid and allocation unsuccessful. C reports a status; `HX_OS_OK` means success. C++ stores the value/error in Result: `if (!result)` checks failure and `result.value()` accesses successful data. Do not silently turn failure into an empty string or zero. Use an error record for native details; see [errors](REFERENCE.md#section-6).

### 10.2 Does NULL always mean the same thing?

Its position matters. NULL at an allocator/options parameter selects defaults; at an error-record output it declines extra details. Required objects/output pointers cannot arbitrarily be NULL. These tutorials need no custom allocator; consult context details when required.

### 10.3 Why release C resources, and explicitly close some C++ objects?

C has no automatic destructor, so release its strings, documents and containers. C++ strings/read-only documents can release at scope exit. Pools and other owners may have background work, so explicit close preserves retry after timeout. Keep borrowed data and code alive while resources still use them.

### 10.4 Why can Chinese text look wrong in a Windows terminal?

Library paths/strings use UTF-8. File reading and terminal display are separate: the terminal also needs to decode UTF-8 output bytes. These examples use ASCII output to check the call sequence. Unicode file paths/content still use the library's UTF-8 adaptation. The library does not change process-wide character encoding or locale just to print an example.

### 10.5 Why does a task fail after disabling modules?

The starter project keeps the modules used in these examples enabled. JSON needs CONFIG; containers need CONTAINERS; variant needs VARIANT; settings need SETTINGS; pools need THREAD_POOL. Disabled/unsupported features return a clear unsupported result rather than fake success. See [build options](REFERENCE.md#section-27) for switches, optional parser/encoding dependencies and minimal embedded builds.

### 10.6 I need another feature

| Task | Document |
| --- | --- |
| Dates/filenames, translations, lexing and directory visits | [Common utilities](COMMON_UTILITIES.md) |
| Find files recursively and run a search in a pool | [File search](FILE_SEARCH.md) |
| Network, processes, pipes, mappings and metrics | [Platform utilities](PLATFORM_UTILITIES.md) |
| Configure keepalive, select a multicast interface and list IP addresses | [Socket options and discovery](NETWORK_OPTIONS.md) |
| Locks, events, thread-local storage and resource lifetimes | [Core contracts](CONTRACTS.md) |
| Installed static/shared find_package integration | [Installation reference](REFERENCE.md#section-5) |
| Platform requirements and limits | [Platform support](PLATFORMS.md) |
