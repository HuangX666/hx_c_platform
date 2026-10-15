# 借用原生平台对象

[English](NATIVE_INTEROP.md) · [README](../README.zh-CN.md)

**已有系统接口需要原生文件句柄时再用这一层。** 先准备 [demo.txt](../examples/README.zh-CN.md#inputs)，构建 `hx_os_native_file`，在 build 目录把 demo.txt 作为参数运行。Windows 使用 `.\bin\Release\hx_os_native_file.exe demo.txt`；Linux 使用 `./bin/hx_os_native_file demo.txt`。

输出 `Native query: 10 bytes`。[native_file.c](../examples/native_file.c) 先用本库打开文件，再借用 HANDLE／fd 调用 GetFileSizeEx／fstat，最后由原所有者关闭文件。业务接原生 API 时可沿用这个顺序；不要关闭借来的句柄，也不要在原文件对象销毁后继续使用。

## 1. 什么时候使用

可移植的业务操作继续使用普通库接口。当另一个 API 必须接收原生文件、套接字、线程或进程对象时，才使用这里的互操作查询。每次取得的都是现有对象的借用值，不会复制对象、转移所有权、接管外部对象或延长生命周期。

包含 `<hx_os_native.h>` 或 `<hx_os.h>`。`HX_OS_ENABLE_NATIVE_INTEROP` 默认开启，`hx_os_native_interop_available()` 查询构建能力；套接字和进程还需要各自模块。关闭相应组合时返回 `HX_OS_NOT_SUPPORTED`。

| 查询 | Windows 结果 | POSIX 结果 |
| --- | --- | --- |
| `hx_os_file_native_borrow` | 转为 uintptr_t 的 HANDLE | 转为 uintptr_t 的非负文件描述符 |
| `hx_os_socket_native_borrow` | 转为 uintptr_t 的 SOCKET | 转为 uintptr_t 的非负套接字描述符 |
| `hx_os_process_native_borrow` | 转为 uintptr_t 的 HANDLE | 转为 uintptr_t 的子进程 ID，**不是描述符** |
| `hx_os_thread_native_borrow` | 将 HANDLE 复制到原生类型存储 | 原样复制 pthread_t，不假定它是整数 |

失败保持输出不变。查询不分配内存、不调用操作系统、不加锁、不启动线程；线程／套接字的状态检查使用已有原子字段。这些入口不会为原生操作增加取消能力。

## 2. 把已有文件交给原生查询

在返回 `hx_os_status` 的函数中，假设已经持有有效的 `hx_os_file *file`：

```c
uintptr_t native;
hx_os_status status = hx_os_file_native_borrow(file, &native, NULL);
if (status != HX_OS_OK)
	return status;

#if defined(_WIN32)
DWORD kind = GetFileType((HANDLE)native);
if (kind == FILE_TYPE_UNKNOWN)
	return HX_OS_IO_ERROR;
#else
struct stat info;
if (fstat((int)native, &info) != 0)
	return HX_OS_IO_ERROR;
#endif

return HX_OS_OK;
```

Windows 分支包含 `<windows.h>`，POSIX 分支包含 `<sys/stat.h>`。正式代码应按需保留原生查询的 GetLastError／errno 诊断。完成全部原生操作后，再通过本库关闭 file；不要对借来的值调用 CloseHandle／close。原生 seek／read／write 使用与库相同的打开状态和文件位置，不是独立的流；短读写和 POSIX SIGPIPE 由调用原生接口的一侧正确处理。

## 3. 线程与进程生命周期

C 调用方先包含系统头并声明真正的原生线程类型：

```c
#if defined(_WIN32)
HANDLE native;
#else
pthread_t native;
#endif

hx_os_status status = hx_os_thread_native_borrow(
	thread, &native, sizeof(native), NULL);
```

POSIX 需要 `<pthread.h>`，不必把 pthread_t 强转为整数。`hx_os_thread_native_size()` 供自行准备存储的工具使用；普通带类型调用已经知道 sizeof(native)。容量不足返回 OUTPUT_FULL，多余字节不改动。已 join 的线程返回 CLOSED，正在 join 时返回 BUSY。不要对借用线程执行原生 join／detach／cancel 或强杀，生命周期继续由本库完成。

进程的原生值只能在本库记录／回收子进程退出前借用。成功等待退出后，两端都返回 CLOSED，即使 Windows 内部要等到对象销毁才释放进程句柄。`hx_os_process_identifier()` 仍可查询诊断用 ID。不要从外部回收这个子进程或改变其所有权；系统以后可能复用原生编号。

## 4. 套接字与并发边界

套接字已经交给本库 loop 后，即使请求全部完成，借用仍返回 BUSY；存在待完成请求也不能借用。此时 I/O 由 loop 管理，不能再将原生句柄交给竞争的事件系统。库不会暗中脱离 loop、关闭对象或强行取消操作。

调用方要在**整个原生操作期间**保护所有者和状态，而不只是保护取得句柄的瞬间。期间排除关闭／销毁、join／子进程回收、loop 接管、模式变化和互相冲突的原生／库操作。返回一个数值不会给所有者增加引用。不要在库背后更改 nonblocking／append／overlapped 模式、继承性或已有事件注册。把句柄交给第三方 API 前，也要确认它只借用对象，不接管所有权或改变这些模式。

这里不导出内部互斥锁／条件变量存储；同步操作继续使用公共接口，避免消费者依赖原生实现的对象布局。

## 5. C++11 与可运行检查

[native_file.c](../examples/native_file.c) 是完整的跨平台程序，演示文件所有权和原生查询。构建 hx_os_native_file 后，运行 `build/bin/hx_os_native_file hello.txt`，或 Windows 上的 `build/bin/Release/hx_os_native_file.exe hello.txt`；程序打印文件大小，最后仍由本库关闭文件。命令行部分使用 SYSTEM_UTILS 获取 UTF-8 参数，互操作模块本身没有这项依赖。

包含 `<hx/Native.hpp>`：

```cpp
auto handle = hx::Native::file(file);
if (!handle)
	return handle.status();

// Use handle.value() synchronously with the appropriate native API.
// The original file owner must remain alive until that use finishes.
return HX_OS_OK;
```

Native::socket、Native::process、Native::thread 使用相同的 Result 方式。线程结果类型是 `hx::NativeThread`：Windows 对应 void 指针／HANDLE，POSIX 对应 pthread_t；便利头只在 POSIX 包含 pthread.h。不通过析构关闭借来的值，也不使用异常。

可运行的 [native_file.c](../examples/native_file.c) 演示先借用原生文件句柄，再通过库读取文件。构建 `hx_os_native_file` 后传入一个 UTF-8 文件路径运行。 平台边界见[平台支持](PLATFORMS.zh-CN.md)。
