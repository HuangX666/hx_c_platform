# hx_c_platform 常用任务教程

[English](USAGE.md) · [首页](../README.zh-CN.md) · [详细参考](REFERENCE.zh-CN.md)

更多专题先从[可运行示例目录](../examples/README.zh-CN.md)选择：每项列出使用场景、构建目标、所需输入和预期结果。

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

每一节解决一个具体问题，例如“读取端口配置”或“把一个计算交给后台”。先完成第 2 节接入，再挑一节运行。每个 C／C++ 代码块都是独立程序：一次只把其中一个保存为 `main.c` 或 `main.cpp`，不要把多个 main 拼在一起。

阅读顺序是：准备输入 → 编译运行 → 对照输出 → 替换成业务数据。暂时不需要自定义分配器或深入平台原理；这些细节在[接口参考](REFERENCE.zh-CN.md)按功能查询。

<a id="project"></a>

## 2. 准备一个最小项目

### 2.1 目录和源码

从源码开始时，在 MSVC 开发者终端或 Linux 终端执行：

```sh
mkdir hx_demo
cd hx_demo
mkdir deps
cd deps
git clone --branch dev https://github.com/HuangX666/hx_c_platform.git
git -C hx_c_platform submodule update --init --recursive
cd ..
```

现在位于 `hx_demo`，在这里创建 `CMakeLists.txt` 和本节要运行的程序。仅文件／配置例子需要额外输入文件，其余例子可以直接运行。

```text
hx_demo/
├── CMakeLists.txt
├── main.c
├── hello.txt       # 文本读取例子需要
├── config.json     # 配置读取例子需要
└── deps/
    └── hx_c_platform/
```

文件读取例子使用 UTF-8 的 `hello.txt`：

```text
Hello, hx_c_platform!
```

配置读取例子使用 UTF-8 的 `config.json`：

```json
{"port": 8080}
```

<a id="c-project"></a>

### 2.2 编译 C 程序

把选中的 C 程序保存为 `main.c`，`CMakeLists.txt` 使用下面的完整内容：

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

`add_subdirectory` 构建本库；`target_link_libraries` 让自己的程序能调用本库。上面的七个开关关闭需要额外原生 SDK 的模块，文件、字符串、内置 JSON、设置、容器和线程池仍可用于本教程。需要那些可选模块时，再按[依赖指南](DEPENDENCIES.zh-CN.md)准备 SDK，并通过对应的 `-DHX_OS_ENABLE_模块名=ON` 配置参数开启。

在 `hx_demo` 中执行：

```sh
cmake -S . -B build
cmake --build build --config Release
```

Windows 的 Visual Studio Release 构建：

```powershell
.\build\Release\hx_demo.exe
```

Linux 的 Ninja／Makefile 构建：

```sh
./build/hx_demo
```

运行时继续留在 `hx_demo`，程序就能按相对路径找到这里的输入文件。换例子时替换 `main.c` 并重新构建；同一工具链复用同一个构建目录。Windows 使用 Ninja 时，执行文件通常是 `build/hx_demo.exe`。

<a id="cpp-project"></a>

### 2.3 编译 C++ 程序

把选中的 C++ 程序保存为 `main.cpp`，用下面的完整 CMake 配置。编译和运行命令仍与第 2.2 节相同。

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

`hx_c_platform::cpp` 提供 C++ 头文件包装并连接同一个 C 核心。这里选择 C++11、关闭异常。C++ 调用先检查 Result 是否成功，再通过 `.value()` 使用里面的结果。

<a id="installed-project"></a>

### 2.4 已经有 include／lib／bin 安装包

把完整 SDK 放到业务项目的 `deps/hx_c_platform`，无需克隆本库源码。按[安装包独立接入教程](INSTALL.zh-CN.md)操作：其中提供可直接复制的 C99 和 C++11 完整 CMakeLists.txt、main、输入文件、Windows／Linux 运行命令，以及 cmake-gui／Visual Studio 的启动方法。

保留 lib/cmake 中本库和依赖的包配置，以及 bin 或 lib 中匹配的运行库。教程用 find_package 导入已编译库；不要对安装包调用 add_subdirectory，也不需要打开源码示例构建开关。仅复制头文件和主库、删掉包配置或依赖，不能保证链接和运行成功。

### 2.5 看懂示例中的几种写法

| 写法 | 你需要知道的意思 |
| --- | --- |
| `hx_os_status status` | 保存这次操作的结果；与 `HX_OS_OK` 比较判断成功 |
| `hx_string text = HX_STRING_INIT` | 准备一个空字符串，之后用它接收或保存文字 |
| `&text` | 允许函数把结果写进这个字符串 |
| 最后的 `NULL` 错误参数 | 暂时不需要详细系统诊断，仍要检查返回状态 |
| C++ 的 `if (!result)` | 检查调用失败 |
| C++ 的 `result.value()` | 成功后取得真正的字符串、数字或对象 |

`NULL` 的含义取决于参数位置，不是所有指针都能传 NULL。下面每节会解释实际用到的参数。

<a id="files"></a>

## 3. 读取并显示文本文件

**要完成的事：**读取 `hello.txt`，显示文件内容。输入文件已在第 2 节准备；不需要自己创建文件句柄或循环读取。

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

调用顺序如下：

1. `HX_STRING_INIT` 创建空字符串，用来接收文件内容。
2. `hx_os_file_read_text` 读取文件，第二个参数 `&text` 表示将内容写入这个字符串；最后的 `NULL` 表示本例只检查状态，不接收额外错误记录。
3. `status == HX_OS_OK` 时才能使用结果。`hx_string_c_str(&text)` 取得以 NUL 结束的字符指针，交给 `puts` 显示。
4. 无论成功还是失败，最后都调用 `hx_string_destroy` 结束字符串的生命周期。文件已由读取函数关闭。
5. `return` 返回 0 表示程序成功，1 表示程序失败。

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

`readText` 返回 `Result<hx::String>`，里面包含字符串或错误。`if (!text)` 表示读取失败，此时打印错误类别并退出；成功后才访问 `text.value()`。`hx::String` 自动管理自己的内存，程序结束不需要手工释放字符串。

**两种程序的正常输出：**

```text
Hello, hx_c_platform!
```

找不到文件时，先确认当前工作目录和 `hello.txt` 是否匹配。该便利入口默认严格读取 UTF-8，移除开头的 UTF-8 BOM，最多读取 16 MiB；无效编码或超限会失败。读取其他编码、二进制或大文件时使用对应扩展入口，见[文件与编码参考](REFERENCE.zh-CN.md#section-7)。

<a id="strings"></a>

## 4. 处理字符串

**要完成的事：**将 `"  Hello  "` 的两端空格去掉，再把 ASCII 大写字母变为小写。这个任务没有输入文件。

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

**正常输出：**

```text
hello
```

先赋值或复制字符串，因为这一步可能分配失败；检查成功后再做原地修改。`trim` 表示去掉两端空白，`lower` 表示转换 ASCII 大写字母。原地操作不创建另一份字符串，也不需要额外结果检查。中文、emoji 等字节保持原样；这两个函数不承担完整 Unicode 大小写转换。

C 程序最后调用 `hx_string_destroy`，C++ 程序由对象自动释放。需要分割、拼接、编码转换或独立字符串副本时，见[字符串工具](COMMON_UTILITIES.zh-CN.md)。

<a id="configuration"></a>

## 5. 读取 JSON 配置

**要完成的事：**读取 `config.json`，取得根对象中的整数 `port`。输入为 `{"port":8080}`；`port` 是键名，`8080` 是它的值。

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

C 版本分三步：`load` 读取并解析文件；`pointer(..., "/port", ...)` 找到键对应的节点；`get_i64` 将节点读为 64 位整数。每步失败就不执行后面的查询。`hx_os_config_release` 释放整个文档，节点由文档管理，不能再单独释放或继续使用。

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

**正常输出：**

```text
Port: 8080
```

`/port` 表示从根对象读取 `port`；如果输入是 `{"server":{"port":8080}}`，查询位置变为 `/server/port`。它不是文件路径，点号也不是这里的层级分隔符。

文件损坏、键缺失、值不能按整数读取都会报告失败，不会自动变成 0。默认配置输入上限为 1 MiB。文档创建后用于只读查询；想在内存中修改设置，使用下一节的 `hx_settings`。其他格式的范围和后端区别见[配置格式说明](CONFIGURATION.zh-CN.md)。

<a id="settings"></a>

## 6. 修改内存中的设置

**要完成的事：**创建一个内存中的设置对象，将 `port` 写为 `8080`，再读出来。这个例子不读取或保存文件，用来说明“设置”和“已解析文档”的不同用途。

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

`create` 创建对象，`set_i64` 以整数类型写入值，`get_i64` 读取该值，`destroy` 关闭并释放。关闭也会返回状态；若前面已经失败，本例保留前面的错误，否则检查关闭结果。

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

**正常输出：**

```text
Port: 8080
```

`owner` 保存库内部对象的指针；`hx::Settings settings(owner)` 提供 C++ 调用方式，`create()` 才真正创建资源。`setInt` 写入整数，`integer` 返回读取结果，`closeUntil` 显式关闭。这里使用 `HX_OS_DEADLINE_NOW`，表示立即尝试关闭；本例没有观察者或并行调用。

### 6.3 一次填写多个值

在第 6.2 节程序中，将 `auto changed = settings.setInt("port", 8080);` 这一句替换成下面的赋值，就能连续写入两个值。保留原程序后面的结果检查、读取和关闭步骤：

```cpp
auto changed = settings.update()
		       .setInt("port", 8080)
		       .setBool("enabled", true)
		       .finish();
```

成功后可分别用 `settings.integer("port")` 和 `settings.boolean("enabled")` 读取。每次读取也要检查结果。`finish()` 不执行延迟提交，它只返回此前写入的最终状态；每个 setter 调用时就已生效。

例如第一项成功、第二项失败，第一项保留，后面的项不执行。`changed.value()` 在该接口失败时也包含成功操作次数，可用于诊断。需要保存文件时显式调用 `settings.save("settings.json")` 并检查结果。[完整设置契约](PLATFORM_UTILITIES.zh-CN.md)说明默认值、观察者、快照及关闭重试。

<a id="containers"></a>

## 7. 使用 C vector 和 queue

### 7.1 vector：按顺序保存整数

**要完成的事：**保存 `10`、`20`，读取数量和第一个值。`vector` 是可增长的连续数组；本例指定最多 16 项。宏要放在函数外，它为指定元素类型生成有名字的 C 函数，不需要 C++ 模板。

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

**正常输出：**

```text
Count: 2, first: 10
```

`int_vector_create` 创建整数容器，`push` 追加元素，`size` 返回数量，`at(..., 0)` 返回第一个整数的位置；前面两次追加成功，所以本例直接读取它。下标从 0 开始，越界时 `at` 返回 NULL。`destroy` 释放容器。

### 7.2 queue：先放进去的先取出来

**要完成的事：**依次放入 `10`、`20`，取出一项后得到 `10`，队列里剩一项。

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

**正常输出：**

```text
Removed: 10, remaining: 1
```

`push` 放入队尾，`pop` 从队首取出并写入 `value`。这组类型化宏适用于整数等可以直接复制字节的元素；拥有内存的字符串、非平凡 C++ 对象需使用带复制/释放规则的接口，不能把上面的 `int` 随意换成 `hx_string`。C 容器也能被 C++ 调用。其他容器和元素规则见[C 容器说明](CONTAINERS.zh-CN.md)。

<a id="thread-pool"></a>

## 8. 提交 C++ 线程池任务

**要完成的事：**创建两个工作线程，提交计算 `21 × 2` 的任务，等待结果，然后关闭线程池。它不需要输入文件。`21` 按值交给任务，不借用调用线程的局部指针。

下面的图展示必须分开的几个步骤。提交成功只表示任务被接受，读取结果时才确认计算是否完成。

```mermaid
flowchart LR
    A["创建线程池"] --> B["提交计算任务"]
    B --> C["等待并检查结果"]
    C --> D["显式关闭线程池"]
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

**正常输出：**

```text
Answer: 42
```

关键步骤：

1. `withThreads(owner, 2)` 创建两个工作线程。`owner` 保留内部线程池，用于关闭和重试。
2. `submit` 接收任务函数和参数；lambda 中 `value * 2` 是真正的工作。`noexcept` 表示该函数不使用异常机制。
3. `submitted` 成功后，`submitted.value()` 取得任务对象；`getUntil` 等待执行结果。它返回的 `answer` 还要检查，任务被接受不等于已执行成功。
4. `hx_os_deadline_after_ms(2000)` 从当前时间计算 2 秒后的等待截止点。等待结果和关闭分别使用本例选定的预算，这不是库规定所有应用必须等 2 秒。
5. `shutdownUntil` 关闭并释放线程池；`HX_OS_POOL_CANCEL_PENDING` 跳过尚未开始的任务，并等待在途工作退出，不强杀正在执行的任务。

完整程序是独立命令行演示；关闭失败时报告问题并以失败结束。放到长期运行的程序中时，需要把 `owner` 放在覆盖关闭重试的持久状态中：超时后保留线程池、任务数据及相关代码，再继续关闭。`hx::ThreadPool` 包装对象析构不会暗中等到结束，也不会替你丢弃这个指针。涉及长任务、取消、并行关闭或插件卸载时，先看[线程池契约](CPP_POOL.zh-CN.md)。

C 程序可使用底层 C 线程池；创建、提交、等待和关闭的示例见[详细参考中的 C 线程池](REFERENCE.zh-CN.md#section-15)。

<a id="variant"></a>

## 9. 同一个变量保存不同类型

variant 每次保存一个值，但可以改变值的类型；修改时自动释放旧内容。C 示例先存整数 42，再存 hello，最后存坐标 Point(2, 5)。不需要输入文件，采用第 2 节的 C 构建配置：

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

C++11 可在类型列表中指定整数、浮点、字符串和自定义 Point。采用第 2 节的 C++ 构建配置，关闭异常：

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

两份程序输出相同：

```text
Integer: 42
String: hello
Point: 2.0, 5.0
```

C 用 point_type 告诉库怎样复制这个普通结构体，C++ 由类型自动生成描述。set 拥有副本；读取时先检查结果，再使用数字、字符串视图或结构体引用。改为其他类型时旧值自动释放，C 在末尾 destroy，C++ 离开作用域自动清理。

读取错误类型明确失败，例如保存 Point 后不能 get<int32_t>。结构体里含 hx_string/堆资源时需要深复制和清理；完整回调、emplace/visit、混合 vector 和分配器说明见[variant 专题](VARIANT.zh-CN.md)。

<a id="questions"></a>

## 10. 初次使用常见问题

### 10.1 为什么有 HX_OS_OK、Result 和 value？

文件不存在、配置错误或分配失败都可能发生。C 函数用状态码报告；`HX_OS_OK` 表示成功。C++ 把值和错误放到 Result 里，`if (!result)` 检查失败，成功后 `result.value()` 读取值。不要把失败直接当成空字符串或 0；需要原生错误详情时可传入错误记录，见[错误参考](REFERENCE.zh-CN.md#section-6)。

### 10.2 NULL 是不是同一种含义？

看它所在的参数：分配器/选项位置的 NULL 使用默认行为，输出错误记录位置的 NULL 表示不额外接收详情。必填的对象或输出位置不能随意用 NULL。本教程不要求创建自定义分配器；需要它时再看 context 的详细说明。

### 10.3 为什么 C 要释放，C++ 有时也要关闭？

C 没有自动析构，所以字符串、文档、容器用完需调用对应释放函数。C++ 的字符串和只读文档可以在离开作用域时释放；线程池等对象可能还有后台工作，使用显式关闭保留超时重试的机会。对象还活着时，不要先释放它借用的数据或卸载它要调用的代码。

### 10.4 中文内容为什么在 Windows 终端显示不对？

库的路径和字符串采用 UTF-8，文件读取和终端显示是两件事。终端也要按 UTF-8 解码输出字节。这里用 ASCII 输出验证调用流程；中文路径/内容的文件处理仍由库按 UTF-8 适配。库不会为了打印示例擅自修改整个进程的字符编码或 locale。

### 10.5 关闭了可选模块，为什么示例失败？

第 2 节的入门配置保留了这些示例所需的模块。JSON 需要 CONFIG，容器需要 CONTAINERS，variant 需要 VARIANT，设置需要 SETTINGS，线程池需要 THREAD_POOL。关闭模块或遇到目标不支持的能力时，接口明确返回不支持，不会用成功伪装。开关、可选解析器、编码依赖和最小嵌入式配置见[构建参考](REFERENCE.zh-CN.md#section-27)。

### 10.6 我想使用其他功能

| 想做什么 | 文档 |
| --- | --- |
| 日期/文件名、翻译、分词、目录遍历 | [常用工具](COMMON_UTILITIES.zh-CN.md) |
| 递归查找文件、在线程池里查询 | [文件查询](FILE_SEARCH.zh-CN.md) |
| 网络、进程、管道、映射、系统指标 | [平台工具](PLATFORM_UTILITIES.zh-CN.md) |
| 锁、事件、线程局部存储和资源生命周期 | [基础契约](CONTRACTS.zh-CN.md) |
| 静态/共享安装后 find_package 接入 | [安装参考](REFERENCE.zh-CN.md#section-5) |
| 目标平台的要求与限制 | [平台支持](PLATFORMS.zh-CN.md) |
