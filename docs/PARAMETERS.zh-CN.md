# 可增长参数包

[English](PARAMETERS.md) · [README](../README.zh-CN.md)

## 1. 怎样选择参数包

`hx_os_params` 用于在同一进程内，按名字向函数传递一组值。它自己复制持有名称、UTF-8 字符串和任意字节数据，可以超出原有栈上 `hx_os_calldata` 的 16 项、512 字节限制。原有栈上参数包的布局和行为保持不变。

| 选择 | 适用场景 | 存储方式 |
| --- | --- | --- |
| `hx_os_calldata`／`hx::CallData` | 现有较小的信号／过程调用 | 固定栈上空间，不分配内存 |
| `hx_os_params`／`hx::Parameters` | 更多字段、更大的字符串或二进制数据 | 有上限的堆空间，按需增长 |

参数包本身不是序列化后的通信协议。指针值借用同一进程中的对象，复制参数包不会复制那个对象。需要跨进程传输时，应使用序列化／IPC 模块。动态参数包与信号分发器的接入属于另外的扩展，不能把两种参数包互相强制转换。

## 2. 运行 C 示例

[parameters.c](../examples/parameters.c) 创建一份设备请求，连续设置多个字段，最后检查一次设置结果，再按名字取出数据。示例中的局部二进制数组在读取前已经离开作用域，用来演示数据已被复制持有。

```sh
cmake --build build --config Release --target hx_os_parameters
# Linux / macOS：
./build/bin/hx_os_parameters
# Windows / Visual Studio：
./build/bin/Release/hx_os_parameters.exe
```

预期输出：

```text
Device 42 (camera): 4 binary bytes
```

成功调用 `hx_os_params_create(NULL, &params, &error)` 后，可这样连续填写字段：

```c
hx_os_params_edit edit = hx_os_params_edit_begin(params);

hx_os_params_edit_set_u64(&edit, "device_id", 42);
hx_os_params_edit_set_cstr(&edit, "name", "camera");
hx_os_params_edit_set_bool(&edit, "enabled", 1);

hx_os_status status = hx_os_params_edit_finish(&edit, &error);
```

每个 setter 都立即执行。首次失败后跳过后续 setter，保留此前设置成功的项；它不是事务，也不是最后才提交。单个 setter 则保证失败保留该字段的旧值，包括分配失败、编码错误和超出容量。

读取一个值可使用 `hx_os_params_get_u64(params, "device_id", &id, &error)`。读取类型必须与写入类型一致：把无符号整数字段当成有符号整数读取，会返回 `HX_OS_TYPE_MISMATCH`；不存在的名称返回 `HX_OS_NOT_FOUND`。用完调用 `hx_os_params_destroy(&params, &error)`，成功后 owner 变为空。

## 3. C++11 调用

包含 `<hx/Parameters.hpp>`，创建和操作都返回 `Result`，不使用异常。`Parameters` 是只可移动的 RAII 所有者。常见调用可以这样写：

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

这个片段用于返回 `hx_os_status` 的函数；使用 `std::puts` 还需包含 `<cstdio>`，成功路径最后返回 `HX_OS_OK`。`string()` 会复制为自己拥有数据的 `hx::String`；`stringView()` 和 `bytesView()` 返回借用视图，必须在修改、移动或销毁 owner 之前用完。

`ParametersView` 是借用的只读包装，`ParametersRef` 是借用的可修改包装，两者都不负责销毁参数包。普通业务代码直接使用 `Parameters` 即可。`clone()` 的结果拥有独立复制的数据，但其中的裸指针值仍指向同一批外部对象。

## 4. 支持的值

| 值 | C 写入／读取 | C++ 写入／读取 |
| --- | --- | --- |
| 布尔值 | `set_bool`／`get_bool` | `setBool`／`boolean` |
| 有符号 64 位整数 | `set_i64`／`get_i64` | `setInt`／`integer` |
| 无符号 64 位整数 | `set_u64`／`get_u64` | `setUnsigned`／`unsignedInteger` |
| double，包括非有限值 | `set_double`／`get_double` | `setDouble`／`real` |
| UTF-8 字符串 | `set_string`、`set_cstr`／`get_string` | `setString`／`string`、`stringView` |
| 任意字节 | `set_bytes`／`get_bytes` | `setBytes`／`bytesView` |
| 借用的指针 | `set_pointer`／`get_pointer` | `setPointer`／`pointer` |

表中 C 名称省略了 `hx_os_params_` 前缀。字符串支持明确长度和内部 NUL，并额外提供结尾 NUL；`set_string` 只允许长度为零时传空指针，`set_cstr` 要求非空 C 字符串指针。字节数据可包含零、无效 UTF-8 和其它任意值，非空数据必须提供有效地址。读取失败不改变输出参数。

类型在运行时才能确定时，使用 `hx_os_params_get()` 获取带类型标记的借用值 `hx_os_param_value`；`hx_os_params_set()` 接受相同描述，并复制它指向的字符串／字节。`hx_os_params_at()` 按从零开始的下标遍历，字段按 UTF-8 字节顺序排列，项数由 `hx_os_params_count()` 查询。名称区分大小写，排序不是本地语言排序。字段名必须是非空、严格 UTF-8，最多 127 字节。

## 5. 容量、所有权和成本

默认最多 256 项、1 MiB 名称／值保留空间。需要调整时，用 `HX_OS_PARAMS_OPTIONS_INIT` 初始化 `hx_os_params_options`，传给 `create_with_options`；允许的最大配置是 65,535 项和 64 MiB。项数组按需倍增，名称／值预算包含终止符；项数组和分配器元数据另有受项数约束的空间开销。

新值装得下时，会复用当前名称／值缓冲区。缩短数据不会马上退还容量，`hx_os_params_storage_bytes()` 查询的是保留容量；`remove()` 或 `clear()` 才释放它。因此把长字符串替换为空字符串后，可能仍然没有足够预算插入另一个字段，需先删除或清空。`clear()` 也会释放项数组。为保证失败不破坏旧值，替换时可能暂时同时持有新旧缓冲区，最多达到相关配置上限的两倍，另加受限元数据。多份 clone 各自占用预算。

查询使用有序数组和二分查找；新增／删除名称时可能移动项。它适合有上限的参数列表，不承诺常数时间插入。修改现有标量，或在已有容量内替换字符串／字节，不再分配内存；读取也不分配。不含内部互斥锁、工作线程、全局配置，也不依赖容器／variant 模块。

修改、移动、销毁必须与其它访问互斥。clone 为防止分配器重入，也需要独占访问源参数包。只有 owner 和字节数据保持稳定时，才允许并发只读查询。分配器不得重入正在操作的参数包；检测到时返回 `HX_OS_DEADLOCK`，不会打断外层操作。owner／context 必须覆盖分配器调用生命周期。参数包仍持有分配时，销毁 context 会返回 busy。

`HX_OS_ENABLE_PARAMETERS` 默认开启，可独立关闭，也可以在 signals／procedures 都关闭时单独启用。运行时使用 `hx_os_params_available()` 查询。关闭功能后，操作明确返回 `HX_OS_NOT_SUPPORTED`；销毁空 owner 仍然安全。不需要第三方库。

## 6. 信号与命名过程

参数 owner 可直接用于可写信号，也可作为可增长命名过程的输入／结果。同步回调只借用参数包，借用期间 `params_destroy` 返回 BUSY；不要在回调里移动或销毁 C++ owner。两种接口的失败和结果交付规则不同，见[信号／过程说明](SIGNALS.zh-CN.md#6-在通知过程中修改参数)。
