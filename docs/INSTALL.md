# Run C and C++ examples using only an installed SDK

[简体中文](INSTALL.zh-CN.md) · [Usage guide](USAGE.md#installed-project) · [Examples](../examples/README.md)

**No hx_c_platform source checkout is required.** Start with a compiled include/lib/bin package, place it in your application's third-party directory, write your own main and read hello.txt. Choose either the C or C++ version below; they are separate programs, not two mains to combine.

## 1. Check the package

You need CMake 3.16+ and a compiler compatible with the SDK. An MSVC package is not a MinGW package; x64, ARMv7 and AArch64 are not interchangeable. Match the Windows configuration and CRT, for example x64 Debug with `/MDd` or x64 Release with `/MD`. Linux packages must also match the target libc and minimum system requirements.

Place the SDK in your application directory, preserving its contents and relative paths:

```text
hx_demo/
├── CMakeLists.txt
├── main.c                  # or main.cpp
├── hello.txt
└── deps/hx_c_platform/
    ├── include/
    │   ├── hx_os.h
    │   ├── hx/             # C++ wrappers
    │   └── ...
    ├── lib/
    │   ├── cmake/          # core and dependency package metadata
    │   │   └── hx_c_platform/hx_c_platformConfig.cmake
    │   └── ...             # .lib / .a / .so and symlinks
    └── bin/                # Windows DLLs when using shared libraries
```

Keep all of `lib/cmake`, including dependency package metadata, not only hx_c_platform's subdirectory. Copy all headers rather than selecting hx_os.h alone. A Windows shared SDK needs its core and dependency DLLs in bin. On Linux, shared libraries and symlinks normally live in lib; bin may be empty or absent.

Documentation and examples under share are not needed to compile this program. Keep them when supplied, and retain the package's license notices for distribution. Without `hx_c_platformConfig.cmake`, the package is incomplete for this find_package workflow; obtain its CMake exports and matching artifacts from the supplier.

No git clone, git submodule, add_subdirectory or source dependency builder is required. CMake obtains include directories, library locations and transitive dependencies from the package.

An optional consumer can use `find_package(hx_c_platform CONFIG QUIET)` and check `hx_c_platform_FOUND` before linking. Missing required SDK dependencies make that optional lookup fail without turning it into a REQUIRED lookup. Use `REQUIRED` when the application cannot operate without this package. Package lookup keeps the caller's `CMAKE_PREFIX_PATH` and OpenSSL static/shared preference unchanged on both success and nonfatal failure.

## 2. C99: two complete files

Save this as CMakeLists.txt inside hx_demo:

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

Save this as main.c:

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

Use text only after a successful read, then destroy the string. The read function closes the file itself. Continue to section 4 for input and execution.

## 3. C++11: a complete alternative

For C++, replace section 2's CMakeLists.txt with this version and save the program as main.cpp. `hx_c_platform::cpp` already links the C core; no second core link line is needed.

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

Save this as main.cpp:

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

readText returns a Result: inspect its status on failure and access value only on success. The string releases itself; no try/catch or exception support is needed.

## 4. Prepare input, build and run

Create UTF-8 hello.txt in hx_demo with the following contents. Both programs read it; run with hx_demo as the current working directory.

```text
Hello, hx_c_platform!
你好，SDK！
```

Run every command below from hx_demo. Use your application's existing build directory, creating one only for the first configuration. Choose one source version at a time; reconfigure and rebuild when switching C/C++, without another build directory. Keep Windows and WSL builds separate.

### 4.1 Windows: Visual Studio / PowerShell

These commands assume an x64 Release SDK. For a Debug SDK, replace Release in both build and run commands with Debug. Select a matching generator on first configuration; retain the generator of an existing build directory:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target hx_demo

$env:PATH = "$PWD\deps\hx_c_platform\bin;$env:PATH"
chcp 65001
.\build\Release\hx_demo.exe
```

PATH applies only to the current terminal and processes it starts. It locates hx_c_platform.dll and the package's actual OpenSSL, SQLite or other runtime dependencies. An already running Visual Studio does not inherit later PATH changes. Windows Ninja normally produces `build/hx_demo.exe`; select its configuration using `-DCMAKE_BUILD_TYPE=Release` at configure time.

**Using cmake-gui and .sln:** select hx_demo as the source directory and your existing build directory as the binary directory. Configure, Generate and open the project. Set hx_demo as the Visual Studio startup project and select the SDK's configuration. The example CMake sets hx_demo as the debugger's working directory so F5 can find hello.txt. For DLL lookup, launch Visual Studio from the terminal with PATH set, or copy the SDK DLLs beside the generated executable:

```powershell
Copy-Item -Path .\deps\hx_c_platform\bin\*.dll `
    -Destination .\build\Release\
```

This copy step is for a Windows shared SDK; use the Debug directory for a Debug build. Include required dependency DLLs, not only hx_c_platform.dll, and avoid mixing different versions of identically named DLLs.

### 4.2 Linux / WSL

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target hx_demo --parallel 2

hx_sdk_lib="$PWD/deps/hx_c_platform/lib"
LD_LIBRARY_PATH="$hx_sdk_lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  ./build/hx_demo
```

This sets a shared-library path for this invocation without changing system configuration. CMake may already embed an SDK runtime path in the build executable; deployment still needs a library lookup strategy after moving it. Do not depend on the development machine's original path. Preserve Linux `.so` symlink chains and use the actual directory if your package uses lib64.

**Both programs print:**

```text
Hello, hx_c_platform!
你好，SDK！
```

The exit code should be zero. A trailing newline in the file may cause puts to add one blank line; the text and successful result are unchanged. For `Read failed: not found`, check the working directory and hello.txt before reinstalling the library.

## 5. Use networking, coroutine or serialization examples

When share is supplied, public examples are under `share/doc/hx_c_platform/examples`. With only include/lib/bin, use this page's complete program or a complete example from the online guides. Library src, dependency sources and private tests are unnecessary.

Replace main with one complete example, copy its companion headers and select the matching target:

| Interface | Link target | Requirement |
| --- | --- | --- |
| C99 | `hx_c_platform::hx_c_platform` | Check status and observe object ownership/release |
| C++11 wrappers | `hx_c_platform::cpp` | Use section 3's exception-free configuration |
| C++20 stackless coroutines | `hx_c_platform::cpp20` | Set CXX_STANDARD to 20; the compiler and standard library must support `<coroutine>` |

Network clients and servers each contain main and need separate executables. Copy serialization_schema.h for the serialization examples and the protocol helpers for networking; each guide lists them. The installed CMake package exports library targets. It does not generate checkout demo targets such as `hx_os_go` or `hx_os_serialization`; use your own executable target name.

The supplier chooses SDK capabilities when building it. Passing `HX_OS_ENABLE_...=ON` in a consumer cannot restore a module omitted from the binary; obtain an SDK containing that capability. This page's text read needs FILE_UTILS. Normal C/C++ examples require no Python or Clang code generator; those tools are needed only when explicitly choosing the generated formatting syntax.

If your SDK is elsewhere, append `-DCMAKE_PREFIX_PATH="/absolute/path/to/sdk"` to configuration or set that variable in cmake-gui. Adjust runtime PATH / LD_LIBRARY_PATH accordingly. The prefix is the directory containing include/lib/bin, not an individual library file.

If CMake still selects an old SDK after a replacement, inspect cached hx_c_platform_DIR and dependency directories. Delete the cache in cmake-gui and configure the same build directory with the matching toolchain; CMake 3.24+ also accepts `--fresh` on the configure command. Ordinary incremental builds need no cache reset. If several SDKs are present, make this example's search path select only the intended one.

## 6. Resolve common failures

| Symptom | Check first |
| --- | --- |
| hx_c_platformConfig.cmake not found | SDK root and complete lib/cmake contents; a source archive is not an installed SDK |
| OpenSSL, CURL, utf8proc or another dependency not found | The package should include matching headers, libraries and CMake metadata; avoid substituting an unrelated system version |
| Machine-type or CRT link error | Compiler, x86/x64/ARM, Debug/Release and MSVC `/MD` versus `/MDd` |
| Missing Windows DLL | Matching runtimes beside the exe or on its process PATH, including the core DLL's dependencies |
| Linux cannot open shared object file | Library search path, `.so` files and symlinks; `ldd build/hx_demo` lists missing dependencies |
| hello.txt not found | Run from hx_demo; F5 uses the working directory set above |
| Garbled Chinese output | UTF-8 input and terminal settings; this is separate from linking |
| API returns not supported | SDK module switches and target capabilities; consumer CMake cannot add code to a prebuilt binary |

A static core can still depend on shared OpenSSL/SQLite or other libraries. Whether bin or `.so` files are needed depends on the actual SDK. Link imported targets so CMake carries transitive dependencies. Supplier packaging and direct native API use are covered in [dependencies](DEPENDENCIES.md) and [native dependencies](NATIVE_DEPENDENCIES.md).
