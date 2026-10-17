# 只有安装包时，怎样运行 C 和 C++ 示例

[English](INSTALL.md) · [入门教程](USAGE.zh-CN.md#installed-project) · [示例目录](../examples/README.zh-CN.md)

**本页不需要 hx_c_platform 源码。** 假设你拿到编译好的 include、lib、bin，把它作为业务项目的第三方 SDK；下面从新建自己的 main 开始，读取 hello.txt，再显示文字。C 和 C++ 是两种任选其一的写法，不要求把它们放进同一个程序。

## 1. 先检查收到的包

需要 CMake 3.16+ 和与你的 SDK 匹配的编译器。Windows MSVC 包不能当作 MinGW 包使用；x64、ARMv7、AArch64 也不能混用。Windows 构建配置及 CRT 要与包匹配，例如 x64 Debug 配 `/MDd`，x64 Release 配 `/MD`。Linux 包还要匹配目标 libc 和最低系统要求。

把 SDK 放进业务目录，保留内部文件和相对位置：

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

`lib/cmake` 是 lib 的一部分，必须保留所有包配置，不只是 hx_c_platform 自己的目录。include 也要完整复制，不能只挑 hx_os.h。Windows 共享包需要 bin 内本库及依赖的 DLL；Linux 共享包的 `.so` 和符号链接通常在 lib 中，bin 可以为空或不存在。

`share` 中的说明和示例不参与本例编译；如果供应方提供了它们，建议一起保留，发布时也保留随包的许可证说明。如果缺少 `hx_c_platformConfig.cmake`，就不属于下面 find_package 流程所需的完整包，应先向供应方取得 CMake 导出文件及匹配产物。

不需要 git clone、git submodule、add_subdirectory，也不需要运行库源码里的依赖构建器。CMake 从包中取得头文件路径、库路径和传递依赖。

如果应用只是可选使用本库，可以调用 `find_package(hx_c_platform CONFIG QUIET)`，检查 `hx_c_platform_FOUND` 后再链接。SDK 依赖缺失时，这种可选查找会返回未找到，不会被内部强制变成 REQUIRED；应用必须依赖本库时再使用 `REQUIRED`。无论找到包还是发生非致命查找失败，都不会改变调用者的 `CMAKE_PREFIX_PATH` 和 OpenSSL 静态／动态偏好。

## 2. C99：复制两个文件即可构建

在 hx_demo 中保存以下完整 CMakeLists.txt：

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

保存为 main.c：

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

读取成功后使用 text，最后销毁字符串；读取函数已经负责关闭文件。到第 4 节准备输入并运行。

## 3. C++11：完整的替代配置

如果使用 C++，用下面的配置替换第 2 节的 CMakeLists.txt，源码保存为 main.cpp。无需自己再添加 C 核心的链接行，`hx_c_platform::cpp` 已经连接同一个 C 库。

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

保存为 main.cpp：

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

readText 返回 Result：失败时读取状态，成功后才取 value。字符串会自动释放，没有 try/catch，也不要求异常支持。

## 4. 准备输入、构建并运行

在 hx_demo 目录创建 UTF-8 的 hello.txt，内容如下。两种程序读取相同文件；运行时的当前目录要是 hx_demo。

```text
Hello, hx_c_platform!
你好，SDK！
```

下方命令均从 hx_demo 执行。build 是你业务工程已有的构建目录，没有时才首次创建。C、C++ 两种写法每次选一种；切换源码后重新配置并构建，不需要另建目录。不要混用 Windows 和 WSL 的构建目录。

### 4.1 Windows：Visual Studio / PowerShell

下面按 x64 Release SDK 举例；Debug SDK 把构建和运行命令中的 Release 都改为 Debug。首次配置选择与 SDK 相匹配的生成器，现有目录沿用原生成器：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target hx_demo

$env:PATH = "$PWD\deps\hx_c_platform\bin;$env:PATH"
chcp 65001
.\build\Release\hx_demo.exe
```

PATH 只对当前终端及它启动的程序生效，用于找到 hx_c_platform.dll 及同包的 OpenSSL、SQLite 等实际依赖。已打开的 Visual Studio 不会自动继承这个终端后来设置的 PATH。Windows Ninja 通常使用 `build/hx_demo.exe`，并在配置时用 `-DCMAKE_BUILD_TYPE=Release` 选择配置。

**使用 cmake-gui 和 .sln：**源码目录选 hx_demo，构建目录选已有 build，Configure / Generate 后打开项目。在 Visual Studio 中将 hx_demo 设为启动项目，选择 SDK 对应的配置。示例 CMake 已把调试工作目录设为 hx_demo，因此 F5 能找到 hello.txt。为使 F5 找到 DLL，可从配置好 PATH 的终端启动 Visual Studio，或把包的 DLL 复制到生成的 exe 旁：

```powershell
Copy-Item -Path .\deps\hx_c_platform\bin\*.dll `
    -Destination .\build\Release\
```

这一复制步骤适用于 Windows 共享包；Debug 时目标目录同样改成 Debug。不要只复制 hx_c_platform.dll 而漏掉它依赖的 DLL，也不要混入其他版本的同名 DLL。

### 4.2 Linux / WSL

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target hx_demo --parallel 2

hx_sdk_lib="$PWD/deps/hx_c_platform/lib"
LD_LIBRARY_PATH="$hx_sdk_lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  ./build/hx_demo
```

这条命令只为本次运行补充共享库路径，避免把 SDK 路径写进系统配置。CMake 可能已经为构建产物写入该 SDK 的运行路径，但程序移动或发布后仍需安排运行库位置；不能依赖开发电脑上的原路径。复制 Linux 包时保留 `.so` 的符号链接链；lib64 布局按实际目录替换。

**两种程序的预期输出相同：**

```text
Hello, hx_c_platform!
你好，SDK！
```

退出码应为 0。文件尾有换行时，puts 可能再输出一个空行；文字和成功状态不变。若看到 `Read failed: not found`，先检查运行目录和 hello.txt，不是重新安装库。

## 5. 换成网络、协程或序列化示例

安装包带 share 时，公开示例在 `share/doc/hx_c_platform/examples`；只有 include/lib/bin 时，仍可直接使用本页完整程序，或复制在线专题中的完整例子。不需要库的 src、deps 源码或私有 tests。

每次用一个完整例子替换 main，保留同目录配套头文件，然后选择链接目标：

| 使用方式 | 链接目标 | 需要注意 |
| --- | --- | --- |
| C99 接口 | `hx_c_platform::hx_c_platform` | 按状态码检查结果；保存和释放对应对象 |
| C++11 包装 | `hx_c_platform::cpp` | 使用第 3 节的无异常配置 |
| C++20 无栈协程 | `hx_c_platform::cpp20` | CXX_STANDARD 改成 20，并使用支持 `<coroutine>` 的编译器及标准库 |

网络服务端、客户端各有 main，需要分别建立 executable。序列化示例的 serialization_schema.h、网络示例的协议辅助头也要一起复制，具体清单见各专题。安装后的 CMake 包导出库目标，不会生成 `hx_os_go`、`hx_os_serialization` 等源码仓库的演示目标；不要在自己工程里照搬这些目标名来构建。

安装包的能力在供应方编译时确定。在消费者工程传 `HX_OS_ENABLE_...=ON` 不会把已裁剪的模块重新编译进二进制；需要包含相应能力的 SDK。本页读取文本需要 FILE_UTILS。普通 C/C++ 示例不需要 Python 或 Clang 代码生成器；只有显式选择格式化源码生成语法时才需要那些工具。

SDK 放在别处时，可以把 `-DCMAKE_PREFIX_PATH="你的 SDK 绝对路径"` 加在配置命令后，或在 cmake-gui 填写同名变量；运行时的 PATH / LD_LIBRARY_PATH 也改为对应目录。路径指向包含 include/lib/bin 的根目录，不是 lib 文件本身。

更换 SDK 后，如果 CMake 仍引用旧路径，检查缓存里的 hx_c_platform_DIR 和各依赖的目录。可在 cmake-gui 中删除缓存，再使用原构建目录和匹配工具链重新配置；CMake 3.24+ 也可在配置命令中加 `--fresh`。普通增量构建不需要清缓存。工程里若同时保留多套 SDK，应只让本例的搜索路径指向本次选用的一套。

## 6. 出错时先检查这几项

| 现象 | 先检查 |
| --- | --- |
| 找不到 hx_c_platformConfig.cmake | SDK 根路径，以及 lib/cmake 是否完整；不要把源码包误当安装包 |
| 提示找不到 OpenSSL、CURL、utf8proc 等 | 相应依赖的头文件、库和 CMake 配置是否随同 SDK 提供；不要随意混入系统里的另一个版本 |
| 链接时报机器类型或 CRT 不匹配 | 编译器、x86/x64/ARM、Debug/Release、MSVC `/MD` 或 `/MDd` |
| Windows 提示缺少 DLL | exe 旁或当前进程 PATH 中是否有同包运行库；检查完整依赖而非只看主 DLL |
| Linux 提示 cannot open shared object file | 库路径、`.so` 文件及符号链接是否完整；用 `ldd build/hx_demo` 查看未找到项 |
| 程序报告找不到 hello.txt | 当前工作目录应是 hx_demo；F5 使用上述调试工作目录 |
| 中文显示乱码 | 输入文件为 UTF-8，终端也使用 UTF-8；不要把显示问题当成链接问题 |
| 接口返回 not supported | SDK 的模块开关与目标系统能力，不能只修改消费者 CMake 来补回模块 |

静态链接本库也可能依赖共享的 OpenSSL／SQLite 等，是否还需要 bin 或 `.so` 取决于实际 SDK；通过导出目标链接，让 CMake 处理传递依赖。供应方制作安装包及原生 SDK 调用见[依赖说明](DEPENDENCIES.zh-CN.md)和[原生依赖](NATIVE_DEPENDENCIES.zh-CN.md)。
