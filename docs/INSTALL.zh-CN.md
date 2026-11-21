# SDK 安装包接入指南（无需源码编译）

[English](INSTALL.md) · [入门教程](USAGE.zh-CN.md#installed-project) · [可运行示例目录](../examples/README.zh-CN.md)

如果你拿到的是已经编译打包好的 `hx_c_platform` SDK（包含 `include/`、`lib/`、`bin/`），**无需克隆 `hx_c_platform` 源码或运行依赖构建器**，直接通过 CMake 的 `find_package()` 即可将它作为第三方库接入你的业务工程。

下面演示如何从零新建一个最小工程 `hx_demo`，用 C99 或 C++11 读取 `hello.txt` 并打印内容。

---

## 1. 检查 SDK 目录结构与工具链匹配

在接入前，请确保你的开发环境满足以下匹配条件：

- **CMake 版本**：3.16 或更高版本。
- **编译器与 ABI 匹配**：Windows MSVC 构建的 SDK 不能用于 MinGW；`x86`、`x64`、`ARMv7` 与 `AArch64` 架构不能混用。
- **Windows CRT 与配置匹配**：`Debug` SDK（`/MDd`）需配对宿主工程的 `Debug` 配置，`Release` SDK（`/MD`）需配对 `Release` 配置。Linux SDK 则需匹配目标系统的 `libc` 基线。

将收到的 SDK 完整放入你的工程目录（例如 `hx_demo/deps/hx_c_platform`），保持其内部目录层级不变：

```text
hx_demo/
├── CMakeLists.txt
├── main.c                  # 或 main.cpp
├── hello.txt
└── deps/hx_c_platform/
    ├── include/
    │   ├── hx_os.h
    │   ├── hx/             # C++11 header-only 封装
    │   └── ...
    ├── lib/
    │   ├── cmake/          # 核心库与传递依赖的 CMake 导出配置
    │   │   └── hx_c_platform/hx_c_platformConfig.cmake
    │   └── ...             # .lib / .a / .so 及符号链接
    └── bin/                # Windows 动态库（DLL）及依赖运行库
```

> [!IMPORTANT]
> **切勿只挑部分文件复制**：
> - `lib/cmake/` 中不仅有 `hx_c_platform` 的导出文件，还包含 OpenSSL、libcurl、压缩库等传递依赖的包配置，必须完整保留。
> - `include/` 需完整保留，不要只复制单个 `hx_os.h`。
> - Windows 动态库包需保留 `bin/` 下的所有 `.dll`；Linux 动态库包需保留 `lib/` 下的 `.so` 及其符号链接链（`cp -a`）。

> [!TIP]
> **可选依赖查找（`QUIET`）**：
> 如果你的工程只是将 `hx_c_platform` 作为可选组件，可以使用 `find_package(hx_c_platform CONFIG QUIET)` 并判断 `if(hx_c_platform_FOUND)`。查找过程不会污染调用方的 `CMAKE_PREFIX_PATH` 或 `OPENSSL_USE_STATIC_LIBS` 变量。

---

## 2. 方案 A：接入 C99 工程

在 `hx_demo/` 下创建 `CMakeLists.txt`：

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

创建 `main.c`：

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

## 3. 方案 B：接入 C++11 工程

如果使用 C++11，将 `CMakeLists.txt` 替换为以下内容（链接 `hx_c_platform::cpp` 时会自动传递链接底层 C 核心库，无需重复链接）：

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

创建 `main.cpp`：

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

## 4. 准备输入文件、构建与运行

在 `hx_demo/` 目录下创建 UTF-8 编码的 `hello.txt`：

```text
Hello, hx_c_platform!
你好，SDK！
```

### 4.1 Windows（Visual Studio / PowerShell）

以下命令以 `x64 Release` SDK 为例（若使用 `Debug` SDK，请将命令与路径中的 `Release` 替换为 `Debug`）：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target hx_demo

# 将 SDK 的 bin 目录加入当前终端 PATH，以便找到 DLL 及依赖运行库
$env:PATH = "$PWD\deps\hx_c_platform\bin;$env:PATH"
chcp 65001
.\build\Release\hx_demo.exe
```

> [!TIP]
> **在 Visual Studio IDE 中按 `F5` 调试**：
> 上面的 `CMakeLists.txt` 已经设置了 `VS_DEBUGGER_WORKING_DIRECTORY`，因此按 `F5` 时能直接找到 `hello.txt`。为了让调试器找到动态库 DLL，可以把 `deps/hx_c_platform/bin/*.dll` 拷贝到可执行文件同目录下：
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

**预期输出**（C 与 C++ 版本完全一致）：

```text
Hello, hx_c_platform!
你好，SDK！
```

---

## 5. 编译更多示例（网络、协程、序列化等）

如果 SDK 安装包中附带了 `share/doc/hx_c_platform/examples`，你可以直接将其中的示例 `.c` / `.cpp` 文件复制过来替换 `main.c` / `main.cpp`（注意一并复制示例同目录下的配套 `.h` 头文件，例如 `serialization_schema.h` 或网络协议辅助头）。

根据所使用的语言层级选择对应的 CMake 链接目标：

| 接口层级 | CMake 链接目标 | 配置要求 |
| --- | --- | --- |
| **C99 核心接口** | `hx_c_platform::hx_c_platform` | 检查 `hx_os_status` 状态码并成对释放资源 |
| **C++11 封装层** | `hx_c_platform::cpp` | 使用第 3 节的无异常 C++11 配置 |
| **C++20 无栈协程** | `hx_c_platform::cpp20` | 将 `CXX_STANDARD` 设为 `20`，需要编译器支持 `<coroutine>` |

> [!NOTE]
> - **关于模块开关**：预编译 SDK 包含哪些模块在打包编译时已经固定。在消费方工程中设置 `-DHX_OS_ENABLE_...=ON` 不会把已裁剪掉的模块重新塞回预编译二进制中。
> - **自定义 SDK 路径**：如果 SDK 不在工程目录内，可在配置 CMake 时传入 `-DCMAKE_PREFIX_PATH="/path/to/sdk"`（指向包含 `include/lib/bin` 的根目录）。更新 SDK 后若 CMake 仍命中旧缓存，可使用 `cmake --fresh`（CMake 3.24+）或清理构建缓存后重新配置。

---

## 6. 常见接入报错速查

| 报错现象 | 原因与排查方法 |
| --- | --- |
| `Could not find a package configuration file provided by "hx_c_platform"` | 检查 `CMAKE_PREFIX_PATH` 是否指向包含 `include/lib/bin` 的 SDK 根目录，且 `lib/cmake/hx_c_platform/hx_c_platformConfig.cmake` 存在（不要把源码仓库当成已安装 SDK） |
| 提示找不到 `OpenSSL`、`CURL`、`utf8proc` 等依赖包 | 检查 `lib/cmake/` 下的传递依赖目录是否被删减，保持 SDK 目录完整，避免混入系统里不兼容的其他版本 |
| 链接时报目标机器类型冲突或 CRT `_ITERATOR_DEBUG_LEVEL` 不匹配 | 检查编译器架构（`x86` vs `x64` vs `ARM`）以及构建配置（`Debug /MDd` vs `Release /MD`）是否与 SDK 一致 |
| Windows 运行时弹窗或退出提示缺少 `.dll` | 将 `deps/hx_c_platform/bin` 加入 `PATH`，或将该目录下的全部 `.dll` 复制到 `.exe` 同级目录 |
| Linux 运行时报 `cannot open shared object file` | 检查 `LD_LIBRARY_PATH` 是否指向 SDK 的 `lib/`（或 `lib64/`），并用 `ldd build/hx_demo` 检查 `.so` 符号链接是否完整 |
| 运行时报 `Read failed: not found` | 当前终端工作目录下没有 `hello.txt`；请切回 `hx_demo/` 目录运行 |
| 终端打印中文出现乱码 | 确保源文件与 `hello.txt` 均为 UTF-8 编码，Windows PowerShell 先执行 `chcp 65001` |
| 调用接口返回 `HX_OS_NOT_SUPPORTED` | 该预编译 SDK 在打包时未开启对应模块，或当前操作系统不支持该平台扩展 |
