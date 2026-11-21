# Installed SDK Integration Guide (No Source Checkout Needed)

[简体中文](INSTALL.zh-CN.md) · [Getting-Started Guide](USAGE.md#installed-project) · [Runnable Examples](../examples/README.md)

When you have a prebuilt `hx_c_platform` SDK package (`include/`, `lib/`, and `bin/`), **no `hx_c_platform` source checkout, Git submodule setup, or dependency build is required**. You can consume the package directly in your project using CMake's `find_package()`.

This guide walks through creating a minimal `hx_demo` project from scratch in either C99 or C++11 to read and print `hello.txt`.

---

## 1. Verify Package Layout and Toolchain Compatibility

Before configuring your project, verify that your build environment matches the prebuilt SDK:

- **CMake**: Version 3.16 or newer.
- **Compiler and Architecture**: An MSVC SDK cannot be used with MinGW, and `x86`, `x64`, `ARMv7`, and `AArch64` binaries are not interchangeable.
- **CRT and Build Configuration**: On Windows, match `Debug` SDKs (`/MDd`) with `Debug` application builds and `Release` SDKs (`/MD`) with `Release` builds. On Linux, match the target system's `libc` baseline.

Place the SDK inside your project directory (for example, `hx_demo/deps/hx_c_platform`) while preserving its internal directory structure:

```text
hx_demo/
├── CMakeLists.txt
├── main.c                  # or main.cpp
├── hello.txt
└── deps/hx_c_platform/
    ├── include/
    │   ├── hx_os.h
    │   ├── hx/             # C++11 header-only wrappers
    │   └── ...
    ├── lib/
    │   ├── cmake/          # CMake package metadata for core and dependencies
    │   │   └── hx_c_platform/hx_c_platformConfig.cmake
    │   └── ...             # .lib / .a / .so and symlinks
    └── bin/                # Windows runtime DLLs (for shared builds)
```

> [!IMPORTANT]
> **Keep the SDK Directory Intact**:
> - Preserve all subdirectories under `lib/cmake/`—not just `hx_c_platform/`, but also the CMake metadata for transitive dependencies (OpenSSL, libcurl, compression codecs, etc.).
> - Copy the entire `include/` tree rather than picking `hx_os.h` alone.
> - On Windows, keep all runtime `.dll` files in `bin/`. On Linux, preserve `.so` files and their symlink chains (`cp -a`) in `lib/`.

> [!TIP]
> **Optional Package Lookup (`QUIET`)**:
> If `hx_c_platform` is optional for your application, call `find_package(hx_c_platform CONFIG QUIET)` and branch on `if(hx_c_platform_FOUND)`. Package lookup never mutates the caller's `CMAKE_PREFIX_PATH` or `OPENSSL_USE_STATIC_LIBS` settings.

---

## 2. Option A: C99 Project Setup

Save the following `CMakeLists.txt` in `hx_demo/`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(hx_demo LANGUAGES C)

list(PREPEND CMAKE_PREFIX_PATH
	"${CMAKE_CURRENT_SOURCE_DIR}/deps/hx_c_platform")
find_package(hx_c_platform CONFIG REQUIRED)

add_executable(hx_demo main.c)
target_link_libraries(hx_demo PRIVATE hx_c_platform::hx_c_platform)
set_target_properties(hx_demo PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
	target_compile_options(hx_demo PRIVATE /utf-8)
endif()
```

Save the following program as `main.c`:

```c
#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_string text = HX_STRING_INIT;

	hx_os_status status =
		hx_os_file_read_text("hello.txt", &text, NULL);
	if (status == HX_OS_OK)
		puts(hx_string_c_str(&text));
	else
		fprintf(stderr, "Read failed: %s\n",
			hx_os_status_name(status));

	hx_string_destroy(&text);

	return status == HX_OS_OK ? 0 : 1;
}
```

---

## 3. Option B: C++11 Project Setup

For C++11, use the `CMakeLists.txt` below (`hx_c_platform::cpp` automatically links `hx_c_platform::hx_c_platform` transitively):

```cmake
cmake_minimum_required(VERSION 3.16)
project(hx_demo LANGUAGES CXX)

list(PREPEND CMAKE_PREFIX_PATH
	"${CMAKE_CURRENT_SOURCE_DIR}/deps/hx_c_platform")
find_package(hx_c_platform CONFIG REQUIRED)

add_executable(hx_demo main.cpp)
target_link_libraries(hx_demo PRIVATE hx_c_platform::cpp)
set_target_properties(hx_demo PROPERTIES
	CXX_STANDARD 11
	CXX_STANDARD_REQUIRED YES
	CXX_EXTENSIONS NO
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
	target_compile_options(hx_demo PRIVATE /utf-8 /EHs-c-)
	target_compile_definitions(hx_demo PRIVATE _HAS_EXCEPTIONS=0)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
	target_compile_options(hx_demo PRIVATE -fno-exceptions)
endif()
```

Save the following program as `main.cpp`:

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

---

## 4. Prepare Input, Build, and Run

Create a UTF-8 file named `hello.txt` in `hx_demo/`:

```text
Hello, hx_c_platform!
你好，SDK！
```

### 4.1 Windows (Visual Studio / PowerShell)

The commands below assume an `x64 Release` SDK (replace `Release` with `Debug` when using a `Debug` SDK):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target hx_demo

# Add the SDK bin directory to PATH so Windows finds the core and dependency DLLs
$env:PATH = "$PWD\deps\hx_c_platform\bin;$env:PATH"
chcp 65001
.\build\Release\hx_demo.exe
```

> [!TIP]
> **Debugging with `F5` in Visual Studio IDE**:
> `VS_DEBUGGER_WORKING_DIRECTORY` is already set to `hx_demo` in `CMakeLists.txt`, so `F5` finds `hello.txt` automatically. To make sure the debugger also locates the runtime DLLs, either launch Visual Studio from a terminal where `PATH` is set or copy the SDK DLLs next to your executable:
> ```powershell
> Copy-Item -Path .\deps\hx_c_platform\bin\*.dll -Destination .\build\Release\
> ```

### 4.2 Linux / WSL

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target hx_demo --parallel 2

hx_sdk_lib="$PWD/deps/hx_c_platform/lib"
LD_LIBRARY_PATH="$hx_sdk_lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  ./build/hx_demo
```

**Expected output** (identical for both C and C++):

```text
Hello, hx_c_platform!
你好，SDK！
```

---

## 5. Running Other Examples (Networking, Coroutines, Serialization)

If your SDK includes `share/doc/hx_c_platform/examples`, you can copy any standalone example `.c` / `.cpp` file into `hx_demo` as `main.c` / `main.cpp` (remember to copy any companion headers in the same directory, such as `serialization_schema.h` or protocol helpers).

Link the target that matches the example's language level:

| Language / Layer | CMake Link Target | Notes |
| --- | --- | --- |
| **C99 Core API** | `hx_c_platform::hx_c_platform` | Check `hx_os_status` and release allocated C handles |
| **C++11 Facade** | `hx_c_platform::cpp` | Use Section 3's exception-free C++11 setup |
| **C++20 Coroutines** | `hx_c_platform::cpp20` | Set `CXX_STANDARD` to `20`; requires compiler/stdlib `<coroutine>` support |

> [!NOTE]
> - **Prebuilt Module Set**: Enabled modules are fixed when the SDK is built. Setting `-DHX_OS_ENABLE_...=ON` in a consumer project cannot add omitted code back into a prebuilt binary.
> - **Custom SDK Location**: If the SDK lives outside your project tree, pass `-DCMAKE_PREFIX_PATH="/absolute/path/to/sdk"` (pointing to the root containing `include/lib/bin`). After replacing an SDK at a different path, run `cmake --fresh` (CMake 3.24+) or clear the CMake cache if old paths remain cached.

---

## 6. Troubleshooting Common Integration Issues

| Symptom | What to Check First |
| --- | --- |
| `hx_c_platformConfig.cmake` not found | Verify `CMAKE_PREFIX_PATH` points to the SDK root and `lib/cmake/` is complete (do not point `find_package` at an unbuilt source checkout) |
| Missing `OpenSSL`, `CURL`, `utf8proc`, or other dependency package | Ensure all subdirectories under `lib/cmake/` and `lib/` were copied intact; avoid mixing unrelated system versions |
| Linker machine-type or CRT mismatch error | Match compiler architecture (`x86` vs `x64` vs `ARM`) and configuration (`Debug /MDd` vs `Release /MD`) with the SDK |
| Missing DLL error on Windows startup | Ensure `deps/hx_c_platform/bin` is on `PATH` or copy all `.dll` files from `bin/` next to `.exe` |
| `cannot open shared object file` on Linux | Check `LD_LIBRARY_PATH`, `.so` files, and symlinks; run `ldd build/hx_demo` to inspect missing libraries |
| `Read failed: not found` | Run the binary with `hx_demo/` as the current working directory so relative path lookup finds `hello.txt` |
| Garbled Chinese characters in console | Keep `hello.txt` in UTF-8 and run `chcp 65001` in Windows PowerShell/CMD |
| Function returns `HX_OS_NOT_SUPPORTED` | The prebuilt SDK was compiled with that module disabled, or the current OS does not support that platform extension |
