# 可增长参数包

[English](PARAMETERS.md) · [README](../README.zh-CN.md)

在插件调用、模块间事件通知或命名过程（Named Procedure）交互中，如果你需要按字段名传递一组整数、浮点数、UTF-8 字符串和二进制数据，并且字段数量或数据长度可能超出固定栈上参数包（`hx_os_calldata`）的限制，可以使用可增长参数包 `hx_os_params`（C++11 对应 `<hx/Parameters.hpp>` 中的 `hx::Parameters`）。写入字符串或字节数组时，参数包会在自身有界堆空间内深拷贝并持有数据；按名字读取视图时则直接借用内部存储。

完整可运行的 C99 示例位于 [parameters.c](../examples/parameters.c)：程序先调用 `hx_os_params_create` 创建参数包，在 `fill_request` 中连续写入设备 ID、名称、布尔标志和 4 个二进制字节（局部字节数组在函数返回时即离开作用域，以验证参数包已深拷贝持有数据），随后在 `show_request` 中按字段名读取并打印结果，最后调用 `hx_os_params_destroy` 释放资源。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 `-DHX_OS_BUILD_EXAMPLES=ON` 并只构建 `hx_os_parameters` 目标即可直接运行（无需命令行参数或输入文件），预期输出为：

```text
Device 42 (camera): 4 binary bytes
```

你也可以参照[最小工程配置](USAGE.zh-CN.md#c-project)将该示例源码作为自己工程的 `main.c`。

## 1. 怎样选择参数包

`hx_os_params` 用于在同一进程内按字段名向函数或回调传递键值集合。它与原有的固定大小栈上参数包 `hx_os_calldata` 并存，互不改变对方的 ABI 布局：

| 参数包类型 | 适用场景 | 存储与分配策略 |
| --- | --- | --- |
| `hx_os_calldata` / `hx::CallData` | 字段少、文本短的常规信号或过程调用 | 固定栈上空间（最多 16 项、512 字节文本），零堆内存分配 |
| `hx_os_params` / `hx::Parameters` | 字段数量较多、包含较长 UTF-8 文本或任意二进制载荷 | 有上限的堆空间，在配置预算内按需动态增长 |

> [!NOTE]
> `hx_os_params` 是进程内的数据传递结构，本身不是跨进程网络序列化协议。当存入原始指针（`set_pointer`）时，参数包仅保存借用地址，克隆参数包也不会深拷贝该指针指向的外部对象。如需跨进程传输，请配合序列化／IPC 模块使用。此外，`hx_os_calldata` 与 `hx_os_params` 是两种独立类型，切勿将二者的指针互相强制转换。

## 2. 运行 C 示例

在已开启 `HX_OS_BUILD_EXAMPLES=ON` 的构建目录中编译并运行 [parameters.c](../examples/parameters.c)：

```sh
cmake --build build --config Release --target hx_os_parameters
# Linux / macOS：
./build/bin/hx_os_parameters
# Windows / Visual Studio：
./build/bin/Release/hx_os_parameters.exe
```

运行输出：

```text
Device 42 (camera): 4 binary bytes
```

成功调用 `hx_os_params_create(NULL, &params, &error)` 后，既可以逐个调用 `hx_os_params_set_*` 并分别检查返回值，也可以使用编辑游标（`hx_os_params_edit`）连续写入多个字段，最后统一检查一次状态：

```c
hx_os_params_edit edit = hx_os_params_edit_begin(params);

hx_os_params_edit_set_u64(&edit, "device_id", 42);
hx_os_params_edit_set_cstr(&edit, "name", "camera");
hx_os_params_edit_set_bool(&edit, "enabled", 1);

hx_os_status status = hx_os_params_edit_finish(&edit, &error);
```

- **编辑游标的执行与失败语义**：游标中的每个 `edit_set_*` 都会立即写入参数包。一旦某一步发生失败，游标会记录该错误并自动跳过后续的 `edit_set_*` 调用，而此前已经成功写入的字段会保留下来（游标不是全有或全无的回滚事务）。单个 setter 自身则是原子更新：若发生内存分配失败、UTF-8 编码错误或超出容量上限，目标字段原有的旧值保持不变。
- **精确类型读取与销毁**：读取字段使用 `hx_os_params_get_u64(params, "device_id", &id, &error)` 等对应接口。读取类型必须与写入类型严格匹配——例如将无符号整数字段当作有符号整数 `get_i64` 读取会返回 `HX_OS_TYPE_MISMATCH`；查找不存在的字段名返回 `HX_OS_NOT_FOUND`。使用完毕后调用 `hx_os_params_destroy(&params, &error)` 释放内存并将句柄置为 `NULL`。

## 3. C++11 调用

C++11 工程包含 `<hx/Parameters.hpp>` 即可使用移动独占的 RAII 包装类 `hx::Parameters`。所有创建与读写方法均返回 `hx::Result<T>`，不使用 C++ 异常：

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

（以上片段位于返回 `hx_os_status` 的函数中，使用 `std::puts` 需包含 `<cstdio>`，成功路径末尾返回 `HX_OS_OK`。）

在 C++11 中读取字符串和字节时有两种所有权形式：
- `params.string("name")`：深拷贝并返回一个独立拥有内存的 `hx::String`。
- `params.stringView("name")` 与 `params.bytesView("payload")`：返回零拷贝借用视图，必须在修改、移动或销毁所属 `hx::Parameters` 对象之前使用完毕。
- 此外，头文件还提供只读借用包装 `hx::ParametersView` 与可写借用包装 `hx::ParametersRef`（常用于函数参数传递，析构时不会销毁底层参数包）。调用 `params.clone()` 会生成一份拥有独立键值存储的新 `hx::Parameters`（其中借用的裸指针字段仍指向同一批外部对象）。

## 4. 支持的值

| 字段值类型 | C 写入／读取接口（省略 `hx_os_params_` 前缀） | C++11 写入／读取方法 |
| --- | --- | --- |
| 布尔值 | `set_bool` / `get_bool` | `setBool` / `boolean` |
| 有符号 64 位整数（`int64_t`） | `set_i64` / `get_i64` | `setInt` / `integer` |
| 无符号 64 位整数（`uint64_t`） | `set_u64` / `get_u64` | `setUnsigned` / `unsignedInteger` |
| 双精度浮点数（`double`，含 `NaN`/`Inf`） | `set_double` / `get_double` | `setDouble` / `real` |
| UTF-8 字符串 | `set_string`、`set_cstr` / `get_string` | `setString` / `string`、`stringView` |
| 任意二进制字节序列 | `set_bytes` / `get_bytes` | `setBytes` / `bytesView` |
| 借用指针（`void *`） | `set_pointer` / `get_pointer` | `setPointer` / `pointer` |

关于字段名、字符串与动态自省的约定：
- **字符串与二进制边界**：`set_string` 支持显式字节长度与内嵌 `NUL` 字节，并在内部末尾额外补一个终止 `NUL`（仅当长度为 `0` 时允许传入 `NULL` 指针；`set_cstr` 则要求非 `NULL` 的 C 字符串）。`set_bytes` 接受包含 `0x00` 或非 UTF-8 字节的任意二进制数据。所有读取函数在失败时均保持调用方的输出变量不变。
- **字段名规范**：字段名必须是非空的严格 UTF-8 字符串，长度范围 `1..127` 字节，区分大小写。
- **运行时类型自省与按下标遍历**：当编译期不知道字段的具体类型时，可以调用 `hx_os_params_get()` 获取带类型标签的借用描述结构 `hx_os_param_value`（对应的 `hx_os_params_set()` 接受同一结构并深拷贝其中的字符串／字节）。调用 `hx_os_params_count()` 可获取当前字段总数，配合 `hx_os_params_at()` 即可按 `0..count-1` 下标遍历所有字段（内部按字段名的 UTF-8 无符号字节字典序排列）。

## 5. 容量、所有权和成本

- **默认与最大容量预算**：`hx_os_params_create` 默认允许最多 `256` 个字段（`max_items`）和 `1 MiB` 的名称＋值保留存储空间（`max_storage_bytes`）。如需调整预算，请使用 `HX_OS_PARAMS_OPTIONS_INIT` 初始化 `hx_os_params_options` 并调用 `hx_os_params_create_with_options`（最大可配置上限为 `65,535` 个字段和 `64 MiB`）。
- **容量复用与释放时机**：更新已有字段时，若新值长度不超过该字段当前已分配的槽位容量，参数包会直接原地复用这块内存而不再调用分配器。注意将长字符串替换成短字符串并不会立即缩减已保留容量——`hx_os_params_storage_bytes()` 返回的是当前**保留容量总和**。如果在接近 `max_storage_bytes` 预算上限时需要腾出空间给其他新字段，请先对该字段调用 `hx_os_params_remove()` 或对整个包调用 `hx_os_params_clear()`（`clear` 还会一并释放字段索引数组）。为保证“替换失败不破坏旧值”，单次更新在分配新缓冲区期间可能短暂同时持有新旧两份缓冲区。
- **查找复杂度与并发规则**：内部采用有序数组配合二分查找（`O(log n)` 查询），插入或删除字段名时会移动数组项，非常适合几十到几百项的有界参数列表。模块内部不含互斥锁或后台线程：
  - 当参数包内容保持不变时，允许多个线程并发执行只读查询。
  - 任何修改、移动、销毁以及 `clone`（源对象在克隆期间会设置防重入标记）都必须与所有其他读写互斥。
  - 自定义分配器回调不得重入当前正在操作的参数包（检测到重入时返回 `HX_OS_DEADLOCK` 并保持外层操作不变）。只要参数包还持有内存分配，销毁其所属的 `hx_os_context` 就会返回 `HX_OS_BUSY`。
- **构建裁剪开关**：`HX_OS_ENABLE_PARAMETERS` 默认开启（`ON`），可在 CMake 中独立关闭，也可在 `HX_OS_ENABLE_SIGNALS` 和 `HX_OS_ENABLE_PROCEDURES` 均关闭时单独开启。运行时可通过 `hx_os_params_available()` 查询；模块关闭后相关操作返回 `HX_OS_NOT_SUPPORTED`（销毁空句柄仍安全返回 `HX_OS_OK`）。

## 6. 信号与命名过程

`hx_os_params`（及 C++ `hx::Parameters`）可直接用于支持动态参数包的可写信号分发，也可作为命名过程的输入与输出参数包。在同步回调执行期间，回调只是**借用**该参数包，此时若在回调内部尝试调用 `hx_os_params_destroy` 会返回 `HX_OS_BUSY`（在 C++ 回调中同样切勿移动或销毁外层传入的 `hx::Parameters` 所有者）。信号通知与过程调用在回调失败时的修改保留／回滚规则有所不同，详见[信号／过程说明](SIGNALS.zh-CN.md#6-在通知过程中修改参数)。
