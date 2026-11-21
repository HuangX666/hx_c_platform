# hx_c_platform 常用任务教程

[English](USAGE.md) · [首页](../README.zh-CN.md) · [详细参考](REFERENCE.zh-CN.md)

如果你想直接按功能寻找更多完整程序，可以从[可运行示例目录](../examples/README.zh-CN.md)开始：其中列出了每个示例的适用场景、CMake 构建目标、所需输入和预期输出。

- [1. 怎样使用这份教程](#using-guide)
- [2. 准备一个最小项目](#project)
- [3. 读取并显示文本文件](#files)
- [4. 处理字符串](#strings)
- [5. 读取 JSON 配置](#configuration)
- [6. 修改内存中的设置](#settings)
- [7. 使用 C vector 和 queue](#containers)
- [8. 提交 C++ 线程池任务](#thread-pool)
- [9. 同一个变量保存不同类型](#variant)
- [10. 初次使用常见问题](#questions)

<a id="using-guide"></a>

## 1. 怎样使用这份教程

这份教程按具体工程任务组织，每一节解决一个常见需求，例如“从 JSON 配置文件读取端口号”或“把一段计算放到后台线程池执行”。

建议先按第 2 节搭好最小演示工程，再挑任意一节动手运行：
1. **准备输入**：按小节说明准备好示例文件（多数示例不需要外部文件）。
2. **编译运行**：本教程中的每个 C / C++ 代码块都是独立可运行的完整程序。每次只把其中一个代码块保存为 `main.c` 或 `main.cpp`，不要把多个 `main` 函数拼进同一个文件。
3. **对照输出**：确认控制台输出与教程一致后，再把示例中的文件路径、键名或参数替换成你的业务数据。

上手阶段不需要配置自定义内存分配器，也不必深究底层平台差异；当你需要查询具体的接口限制、生命周期契约或平台边界时，可以随时查阅[详细参考](REFERENCE.zh-CN.md)。

<a id="project"></a>

## 2. 准备一个最小项目

### 2.1 目录和源码

如果从源码接入，打开 MSVC 开发者终端（Windows）或普通 Shell（Linux / macOS），执行以下命令拉取仓库与子模块：

```sh
mkdir hx_demo
cd hx_demo
mkdir deps
cd deps
git clone --branch dev https://github.com/HuangX666/hx_c_platform.git
git -C hx_c_platform submodule update --init --recursive
cd ..
```

此时你位于 `hx_demo` 目录下。接下来在这里创建 `CMakeLists.txt` 和你要运行的源文件。本教程中只有“读取文本文件”和“读取 JSON 配置”两个例子需要额外输入文件，其余例子可以直接编译运行。

```text
hx_demo/
├── CMakeLists.txt
├── main.c
├── hello.txt       # 第 3 节文本读取示例使用
├── config.json     # 第 5 节配置读取示例使用
└── deps/
    └── hx_c_platform/
```

第 3 节文件读取示例使用 UTF-8 编码的 `hello.txt`：

```text
Hello, hx_c_platform!
```

第 5 节配置读取示例使用 UTF-8 编码的 `config.json`：

```json
{"port": 8080}
```

<a id="c-project"></a>

### 2.2 编译 C 程序

把选中的 C 示例代码保存为 `main.c`，并在 `CMakeLists.txt` 中写入以下完整配置：

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

其中 `add_subdirectory` 负责构建本库，`target_link_libraries` 将 `hx_c_platform::hx_c_platform` 链接到你的可执行程序。

> [!TIP]
> 上面的 `foreach` 循环关闭了 7 个依赖外部原生 SDK（如 OpenSSL、libcurl、utf8proc、SQLite 等）的可选模块，使最小工程无需预装第三方 SDK 即可直接编译。本教程用到的文件、字符串、内置 JSON、可变设置、容器、线程池和 `variant` 均属于核心或内置模块，不受影响。后续需要使用那些扩展模块时，请先按[依赖指南](DEPENDENCIES.zh-CN.md)准备对应 SDK，再通过 `-DHX_OS_ENABLE_<MODULE>=ON` 开启。

在 `hx_demo` 目录下执行构建：

```sh
cmake -S . -B build
cmake --build build --config Release
```

在 Windows 上使用 Visual Studio 生成器构建完成后，运行：

```powershell
.\build\Release\hx_demo.exe
```

在 Linux / macOS（或使用 Ninja / Makefile 生成器）构建完成后，运行：

```sh
./build/hx_demo
```

请保持在 `hx_demo` 目录下运行程序，这样程序才能通过相对路径找到同目录下的 `hello.txt` 和 `config.json`。尝试其他章节的例子时，只需替换 `main.c` 的内容并重新执行 `cmake --build build --config Release`，同一工具链下无需删除并重建 `build` 目录（Windows 下若使用 Ninja 生成器，可执行文件通常位于 `build/hx_demo.exe`）。

<a id="cpp-project"></a>

### 2.3 编译 C++ 程序

把选中的 C++ 示例代码保存为 `main.cpp`，并使用下面的 `CMakeLists.txt`。构建与运行命令与第 2.2 节完全相同：

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

链接 `hx_c_platform::cpp` 会引入 header-only 的 C++11 封装层并自动链接底层 C 核心库。本库的所有 C++ 接口都通过 `hx::Result<T>` 返回结果与错误状态，不抛出异常，因此在关闭异常（`/EHs-c-` 或 `-fno-exceptions`）的工程中也能直接使用。调用后先检查 `Result` 是否成功，再通过 `.value()` 取出结果。

<a id="installed-project"></a>

### 2.4 已经有 include / lib / bin 安装包

如果你拿到的是已经编译打包好的 SDK（包含 `include`、`lib`、`bin` 目录），只需将完整 SDK 放到业务工程的 `deps/hx_c_platform` 下，无需克隆本库源码。具体步骤请参考[安装包独立接入教程](INSTALL.zh-CN.md)，其中提供了可直接复制的 C99 与 C++11 `CMakeLists.txt`、示例源文件、Windows / Linux 运行命令，以及 `cmake-gui` 和 Visual Studio 的配置方法。

> [!IMPORTANT]
> 使用预编译安装包时，请完整保留 `lib/cmake` 下的 CMake 导出配置以及 `bin` / `lib` 中匹配的依赖运行库，并在宿主工程中使用 `find_package(hx_c_platform CONFIG REQUIRED)` 导入目标（不要对安装目录调用 `add_subdirectory`，也不需要开启源码示例构建开关）。如果只拷贝头文件和主库文件而丢弃了 CMake 包元数据或底层依赖库，链接或运行时将会失败。

### 2.5 看懂示例中的几种常见写法

| 常见写法 | 在代码中的含义 |
| --- | --- |
| `hx_os_status status` | 保存本次 C 接口调用的状态码；与 `HX_OS_OK` 比较以判断是否成功 |
| `hx_string text = HX_STRING_INIT` | 初始化一个空的 UTF-8 字符串对象，用来接收或保存文本 |
| `&text` | 将字符串对象的地址传给函数，允许函数把输出内容写进该字符串 |
| 末尾的 `NULL` 错误参数 | 表示当前调用不需要获取详细的原生系统错误记录，但仍需检查返回的 `hx_os_status` |
| C++ 的 `if (!result)` | 检查 `hx::Result<T>` 调用是否失败 |
| C++ 的 `result.value()` | 在确认 `result` 成功后，取出里面保存的字符串、数值或对象 |

需要注意，参数传 `NULL` 的含义取决于参数位置，并非所有指针参数都允许传 `NULL`。下面每一节都会结合具体代码说明关键参数的作用。

<a id="files"></a>

## 3. 读取并显示文本文件

**应用场景**：将磁盘上的 UTF-8 文本文件 `hello.txt` 一次性读入内存并打印出来，无需手动管理文件句柄或编写分块读取循环。

### 3.1 C 完整程序

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

这段代码的执行流程如下：
1. `HX_STRING_INIT` 将 `text` 初始化为空字符串，准备用来接收文件内容。
2. `hx_os_file_read_text("hello.txt", &text, NULL)` 自动完成打开文件、读取内容、校验 UTF-8 并关闭文件。第二个参数 `&text` 用于接收文本，最后一个参数传 `NULL` 表示只通过返回值判断状态，不额外收集底层系统错误码。
3. 当 `status == HX_OS_OK` 时，调用 `hx_string_c_str(&text)` 获取以 `\0` 结尾的 C 字符串指针并传给 `puts` 打印。
4. 无论读取成功还是失败，退出前都调用 `hx_string_destroy(&text)` 释放字符串占用的内存。

### 3.2 C++ 完整程序

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

在 C++ 中，`hx::Files::readText` 返回 `hx::Result<hx::String>`。先用 `if (!text)` 检查是否读取失败；确认成功后再通过 `text.value().c_str()` 访问字符串内容。`hx::String` 会在离开作用域时自动释放内存，无需手动调用销毁函数。

**预期输出：**

```text
Hello, hx_c_platform!
```

> [!NOTE]
> - 如果运行后提示找不到文件（`HX_OS_NOT_FOUND`），请检查终端当前工作目录下是否已经放置了第 2 节准备的 `hello.txt`。
> - `hx_os_file_read_text` / `hx::Files::readText` 默认按严格 UTF-8 读取，会自动剥离文件开头的 UTF-8 BOM，并将单文件读取上限设为 16 MiB；遇到非法 UTF-8 字节或超出大小上限时会返回错误。如果需要读取二进制数据、传统编码文本（如 GBK / UTF-16）或更大体积的文件，请参考[详细参考：字符串与编码转换](REFERENCE.zh-CN.md#section-7)。

<a id="strings"></a>

## 4. 处理字符串

**应用场景**：将带有首尾空格的字符串 `"  Hello  "` 去掉两端空白，再把其中的 ASCII 大写字母转换为小写。本示例不需要外部输入文件。

### 4.1 C 完整程序

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

### 4.2 C++ 完整程序

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

**预期输出：**

```text
hello
```

创建或复制字符串（`hx_string_assign_cstr` / `hx::String::copy`）需要分配内存，因此第一步要先检查返回状态。一旦字符串构建成功，后续的 `hx_string_trim_ascii_in_place` 和 `hx_string_lower_ascii_in_place`（或 C++ 的 `.trimAscii().lowerAscii()` 链式调用）都在现有缓冲区上原地修改，不会触发内存分配，因此不会失败，也不需要每一步都检查错误码。

> [!NOTE]
> 这里的 `trim_ascii` 和 `lower_ascii` 仅针对 ASCII 空白字符和 `A`–`Z` 字母处理，字符串中的中文、emoji 等多字节 UTF-8 内容会原样保留，不会被破坏。如果你需要按分隔符拆分、拼接、格式化字符串，或进行完整的 Unicode 规范化与大小写折叠，请参阅[常用工具：字符串处理](COMMON_UTILITIES.zh-CN.md)。

<a id="configuration"></a>

## 5. 读取 JSON 配置

**应用场景**：解析第 2 节准备的 `config.json`（内容为 `{"port": 8080}`），读取根对象下的整数配置项 `port`。

### 5.1 C 完整程序

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

C 语言读取配置分为三步：
1. `hx_os_config_load` 读取并解析 `config.json`，生成只读文档句柄 `document`。
2. `hx_os_config_pointer(hx_os_config_root(document), "/port", &node)` 从根节点按 JSON Pointer 路径查找 `/port` 对应的节点。
3. `hx_os_config_get_i64(node, &port)` 将该节点的值作为 64 位有符号整数读出。
4. 最后调用 `hx_os_config_release(document)` 释放整棵配置文档。文档内的节点指针（`node`）由 `document` 托管，无需也不能单独释放，且在 `document` 释放后不能继续访问。

### 5.2 C++ 完整程序

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

**预期输出：**

```text
Port: 8080
```

> [!NOTE]
> - **查询路径语法**：`/port` 是 JSON Pointer 路径，表示查找根对象下的 `port` 字段。如果配置是嵌套结构 `{"server": {"port": 8080}}`，查询路径应写为 `"/server/port"`（使用斜杠 `/` 分隔层级，而不是点号 `.`）。
> - **严格类型检查**：文件语法错误、键不存在（返回 `HX_OS_NOT_FOUND`）或字段类型不是整数（返回 `HX_OS_TYPE_MISMATCH`）都会明确报错，绝不会静默返回 `0`。
> - **只读文档 vs. 可变设置**：`hx_os_config_document`（默认输入上限 1 MiB）解析完成后是不可变的只读树。如果你需要在运行时动态修改配置项或保存回文件，请使用下一节的 `hx_settings`。关于 YAML、TOML、INI、XML 等其他格式的语法范围，请参阅[配置格式说明](CONFIGURATION.zh-CN.md)。

<a id="settings"></a>

## 6. 修改内存中的设置

**应用场景**：创建一个可在运行时读写的内存设置对象（`hx_settings`），将 `port` 设为 `8080` 并读回。与上一节只读的 `hx_os_config_document` 不同，`hx_settings` 专为运行时动态修改、默认值分层和并发快照读取而设计。

### 6.1 C 完整程序

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

这里 `hx_settings_create` 创建可变设置对象，`hx_settings_set_i64` 写入整数值，`hx_settings_get_i64` 读出该值，最后由 `hx_settings_destroy` 关闭并释放对象。由于销毁操作本身也会返回状态码，代码在前面步骤全部成功时继续检查 `closed` 的结果，若前面已经出错则优先保留最初的错误码。

### 6.2 C++ 完整程序

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

**预期输出：**

```text
Port: 8080
```

在 C++ 封装中，调用方持有底层指针 `hx_settings *owner = NULL`，`hx::Settings settings(owner)` 绑定该指针并提供面向对象的方法，调用 `settings.create()` 时才真正分配底层资源。退出前通过 `settings.closeUntil(HX_OS_DEADLINE_NOW)` 显式关闭（本例没有注册观察者回调或多线程并发访问，传入 `HX_OS_DEADLINE_NOW` 即可立即完成关闭）。

### 6.3 一次连续写入多个值

如果要连续写入多项配置，不想每写一项都写一段 `if` 判断，可以把第 6.2 节中的 `auto changed = settings.setInt("port", 8080);` 替换为下面的链式调用（其余检查、读取和关闭代码保持不变）：

```cpp
auto changed = settings.update()
		       .setInt("port", 8080)
		       .setBool("enabled", true)
		       .finish();
```

写入成功后，可分别用 `settings.integer("port")` 和 `settings.boolean("enabled")` 读取对应的值。

> [!IMPORTANT]
> `settings.update()` 的每个 setter 在调用当下就会**立即生效**，而不是攒到 `finish()` 才一次性提交事务。如果第一项写入成功、第二项失败，第一项的修改会保留，后续 setter 将被跳过；此时 `changed` 为失败状态，但你可以通过 `changed.value()` 查看在出错前已成功执行的写入次数。需要将设置持久化到磁盘时，请显式调用 `settings.save("settings.json")` 并检查返回值。更多关于默认值层、变更观察者（Observer）、不可变快照与关闭重试的规则，请参阅[平台工具：可变设置契约](PLATFORM_UTILITIES.zh-CN.md)。

<a id="containers"></a>

## 7. 使用 C vector 和 queue

### 7.1 vector：按顺序保存整数

**应用场景**：创建一个可动态增长的连续整数数组（`vector`），依次追加 `10` 和 `20`，然后打印当前元素个数与第一个元素的值。

`HX_OS_DECLARE_VECTOR(int_vector, int)` 宏需要放在函数外部，它会为 `int` 类型生成一套类型安全的 C 函数（如 `int_vector_create`、`int_vector_push` 等），无需依赖 C++ 模板。本例将容量上限设为 16 项：

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

**预期输出：**

```text
Count: 2, first: 10
```

`int_vector_create` 创建容器，`int_vector_push` 在末尾追加元素，`int_vector_size` 返回当前元素数量，`int_vector_at(numbers, 0)` 返回下标 `0` 处元素的指针（若下标越界则返回 `NULL`；本例前两次 `push` 均已成功，因此可以直接解引用），最后调用 `int_vector_destroy` 释放容器内存。

### 7.2 queue：先进先出队列

**应用场景**：创建一个先进先出（FIFO）队列，依次入队 `10`、`20`，弹出队首元素后得到 `10`，队列中剩余 1 项。

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

**预期输出：**

```text
Removed: 10, remaining: 1
```

`int_queue_push` 将元素放入队尾，`int_queue_pop` 从队首取出元素并写入 `value`。这些 C 容器同样可以在 C++ 代码中直接调用。

> [!WARNING]
> `HX_OS_DECLARE_VECTOR` 和 `HX_OS_DECLARE_QUEUE` 生成的类型化宏仅适用于 `int`、普通结构体等**可直接按字节复制（POD）**的元素类型。不要把 `int` 直接替换成持有堆内存的 `hx_string` 或包含析构逻辑的 C++ 对象；存放需要深拷贝和自定义释放的资源时，请使用带 `copy` / `drop` 回调的通用容器接口，详见[C 容器说明](CONTAINERS.zh-CN.md)。

<a id="thread-pool"></a>

## 8. 提交 C++ 线程池任务

**应用场景**：创建一个包含 2 个工作线程的线程池，将计算 `21 * 2` 的任务提交到后台执行，等待计算结果并安全关闭线程池。这里参数 `21` 按值传递给任务，避免跨线程借用调用方的局部栈指针。

在使用线程池时，需要把“任务已成功排入队列”和“任务已执行完毕并拿到结果”区分开：

```mermaid
flowchart LR
    A["1. 创建线程池"] --> B["2. 提交任务（入队准入）"]
    B --> C["3. 等待并检查执行结果"]
    C --> D["4. 显式关闭线程池"]
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

**预期输出：**

```text
Answer: 42
```

关键步骤解析：
1. `hx::ThreadPool::withThreads(owner, 2)` 启动 2 个工作线程，并将底层线程池句柄保存在 `owner` 中，供后续关闭和超时重试使用。
2. `pool.submit(...)` 接收可调用对象（带 `noexcept` 的 lambda）和参数 `21`。
3. `if (submitted)` 表示任务已被线程池队列接受；接着调用 `submitted.value().getUntil(...)` 等待工作线程算完并返回 `answer`，确认 `if (answer)` 成功后读取 `answer.value()`。
4. `hx_os_deadline_after_ms(2000)` 根据当前单调时钟计算 2 秒后的截止时间（2 秒只是本示例选择的等待预算，你可以根据业务需要自由设定）。
5. `pool.shutdownUntil(..., HX_OS_POOL_CANCEL_PENDING)` 显式关闭并销毁线程池。`HX_OS_POOL_CANCEL_PENDING` 会取消队列中尚未开始执行的任务，并等待正在运行中的任务自然退出，绝不会强行杀死工作线程。

> [!IMPORTANT]
> **长生命周期服务中的关闭重试契约**：上面的命令行小程序在关闭超时（`HX_OS_TIMEOUT`）时直接报错退出；但在长期运行的后台服务或宿主程序中，`hx::ThreadPool` 包装对象的析构函数**不会**暗中无限阻塞等待，也**不会**替你丢弃未完成的 `owner` 指针。如果 `shutdownUntil` 因在途长任务尚未退出而返回 `HX_OS_TIMEOUT`，你必须把 `owner`、任务访问的数据以及相关代码（如插件动态库）继续保留在关闭状态机中，稍后再次调用 `shutdownUntil` 重试，直到返回 `HX_OS_OK`。关于长任务协作取消、并行关闭与插件卸载的完整规则，请参阅[C++ 线程池契约](CPP_POOL.zh-CN.md)。C 语言工程也可直接使用底层 C 线程池接口，详见[详细参考：C 线程池与延迟任务](REFERENCE.zh-CN.md#section-15)。

<a id="variant"></a>

## 9. 同一个变量保存不同类型

**应用场景**：使用 `hx_variant`（或 C++ 的 `hx::TypedVariant`）在同一个变量中先后存放整数 `42`、字符串 `"hello"` 和自定义坐标结构体 `Point{2, 5}`。每次赋新值时，容器会自动释放旧值占用的资源。

### 9.1 C 完整程序

无需外部输入文件，使用第 2.2 节的 C 构建配置即可编译：

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

### 9.2 C++ 完整程序

C++11 可以用 `hx::TypedVariant<...>` 直接在模板参数列表中声明允许存放的类型集合（使用第 2.3 节的无异常 C++ 构建配置）：

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

**两份程序的预期输出：**

```text
Integer: 42
String: hello
Point: 2.0, 5.0
```

在 C 代码中，`HX_VARIANT_DEFINE_TRIVIAL_TYPE(point_type, point)` 宏为普通 POD 结构体生成类型描述符；在 C++ 中则由模板自动推导。调用 `set` 会保存数据的独立副本，赋新值时自动释放前一个值，退出前 C 程序调用 `hx_variant_destroy(&value)`，C++ 对象则在离开作用域时自动析构。

> [!NOTE]
> `variant` 读取时会严格核对当前保存的类型（例如当前存放的是 `Point` 时，调用 `get<int32_t>()` 会返回 `HX_OS_TYPE_MISMATCH` 错误）。如果自定义结构体内部持有 `hx_string` 或堆内存指针，不能使用 `TRIVIAL_TYPE` 宏，而应提供自定义的深拷贝与清理回调；关于自定义生命周期回调、`emplace` / `visit`、异构 `vector` 以及分配器绑定的完整用法，请参阅[variant 专题教程](VARIANT.zh-CN.md)。

<a id="questions"></a>

## 10. 初次使用常见问题

### 10.1 为什么要把状态码（`HX_OS_OK`）、`Result` 和 `.value()` 分开？

文件不存在、配置格式损坏、内存分配失败在真实环境中随时可能发生。C 接口统一通过返回 `hx_os_status` 报告结果（`HX_OS_OK` 表示成功）；C++ 接口则将返回值与错误打包进 `hx::Result<T>`，先用 `if (!result)` 判断是否出错，确认成功后再用 `result.value()` 取出结果。这样设计是为了避免把读取失败静默当成空字符串 `""` 或数字 `0` 继续往下跑。如果你需要获取底层操作系统的具体错误码和可读诊断文本，可以传入 `hx_os_error` 参数，详见[详细参考：错误状态与分配器](REFERENCE.zh-CN.md#section-6)。

### 10.2 函数参数里的 `NULL` 都是同一个意思吗？

不是，`NULL` 的含义取决于它所在的参数位置：
- **分配器上下文（`context`）或选项（`options`）参数传 `NULL`**：表示使用默认的全局内存分配器或默认配置参数。
- **末尾的错误记录（`error`）参数传 `NULL`**：表示调用方只需要 `hx_os_status` 状态码，不需要额外填充详细的 `hx_os_error` 结构。
- **必填的对象句柄或输出指针**：不能传 `NULL`（传 `NULL` 会返回 `HX_OS_INVALID`）。

### 10.3 为什么 C 对象要手动调用 `destroy`，而 C++ 有时也要显式调用 `closeUntil` / `shutdownUntil`？

C 语言没有析构函数，因此 `hx_string`、`hx_os_config_document`、容器等对象用完后必须调用对应的 `destroy` 或 `release` 函数归还内存。

在 C++ 中，像 `hx::String`、`hx::ConfigDocument`、`hx::TypedVariant` 这类纯内存对象会在离开作用域时自动析构释放；但像 `hx::ThreadPool`、`hx::Settings` 这类可能涉及后台工作线程或并发观察者的资源所有者，关闭过程可能会因为等待后台任务而超时（`HX_OS_TIMEOUT`）。显式调用 `shutdownUntil` / `closeUntil` 能让你检查关闭是否真正完成，并在超时后安全地保留对象以便重试。只要资源对象尚未成功关闭，就绝不能提前释放它借用的用户数据或卸载对应的插件代码。

### 10.4 为什么在 Windows 终端里打印中文有时会乱码？

本库内部的所有文件路径与字符串统一采用 UTF-8 编码（在 Windows 下调用系统文件 API 时会自动在内部转换为 UTF-16）。然而，“文件按 UTF-8 正确读入内存”和“终端窗口按什么编码显示标准输出字节”是两件独立的事。如果 Windows 控制台当前代码页仍是本地 ANSI（如活动代码页 936 GBK），直接把 UTF-8 字节写到 `stdout` 就会显示乱码。本库不会在后台擅自修改宿主进程的全局控制台代码页或 `locale`；在 Windows 终端中查看 UTF-8 输出时，可以先执行 `chcp 65001` 或使用默认 UTF-8 的终端环境。

### 10.5 为什么关闭某些 CMake 模块开关后，运行示例会报错？

第 2 节的入门 `CMakeLists.txt` 保留了本教程所有示例所需的模块。如果你额外关闭了核心功能开关（例如解析 JSON 需要 `HX_OS_ENABLE_CONFIG`，容器需要 `HX_OS_ENABLE_CONTAINERS`，`variant` 需要 `HX_OS_ENABLE_VARIANT`，可变设置需要 `HX_OS_ENABLE_SETTINGS`，线程池需要 `HX_OS_ENABLE_THREAD_POOL`），对应接口在调用时会明确返回 `HX_OS_NOT_SUPPORTED`，而不会用空实现伪装成功。完整的模块开关列表、可选第三方解析器与最小嵌入式裁剪配置见[详细参考：构建开关与可选依赖](REFERENCE.zh-CN.md#section-27)。

### 10.6 我想进一步使用其他功能，该看哪篇文档？

| 你想实现的功能 | 推荐阅读的文档 |
| --- | --- |
| 日期与安全文件名生成、多语言翻译表、词法分词、目录遍历 | [常用工具](COMMON_UTILITIES.zh-CN.md) |
| 按通配符递归查找文件、将查找任务放到线程池执行 | [文件查询](FILE_SEARCH.zh-CN.md) |
| 套接字网络、子进程输出捕获、管道、内存映射、系统指标查询 | [平台工具](PLATFORM_UTILITIES.zh-CN.md) |
| 配置 TCP Keepalive、选择组播网卡、枚举本机 IP 地址 | [网络选项与设备发现](NETWORK_OPTIONS.zh-CN.md) |
| 互斥锁、事件、线程局部存储（TLS）与资源生命周期规则 | [基础契约](CONTRACTS.zh-CN.md) |
| 静态库 / 动态库打包安装与 `find_package` 工程接入 | [工程接入、安装与链接](REFERENCE.zh-CN.md#section-5) |
| 各操作系统与编译工具链的版本基线及平台限制 | [平台支持](PLATFORMS.zh-CN.md) |
