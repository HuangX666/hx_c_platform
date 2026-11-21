# 借用原生平台对象

[English](NATIVE_INTEROP.md) · [README](../README.zh-CN.md)

在绝大多数跨平台业务代码中，直接使用本库提供的文件、套接字、线程和进程接口即可。但如果你需要将本库已经打开的资源传给某个只接受操作系统原生句柄（如 Windows `HANDLE` / `SOCKET` 或 POSIX 文件描述符 `fd` / `pthread_t`）的系统 API 或第三方库，可以使用原生对象互操作接口（`<hx_os_native.h>` / `<hx/Native.hpp>`）。

你可以先编译并运行完整示例 [native_file.c](../examples/native_file.c) 来直观了解借用流程：先按[输入准备说明](../examples/README.zh-CN.md#inputs)在构建目录准备好 10 字节的 `demo.txt`，构建 `hx_os_native_file` 后在 `build` 目录运行：

```sh
# Linux / macOS：
./bin/hx_os_native_file demo.txt

# Windows PowerShell：
.\bin\Release\hx_os_native_file.exe demo.txt
```

程序会输出 `Native query: 10 bytes`。在 [native_file.c](../examples/native_file.c) 中，程序先通过本库打开文件，接着调用 `hx_os_file_native_borrow` 借出底层 `HANDLE` 或 `fd` 传给原生 `GetFileSizeEx` / `fstat` 查询文件大小，最后仍由本库的文件所有者统一关闭文件。

## 1. 什么时候使用

当外部系统接口必须接收原生文件、套接字、线程或进程标识时，使用本模块提供的显式借用查询。每次查询返回的都只是当前对象的**借用句柄（Borrowed Handle）**——它不会调用 `DuplicateHandle` / `dup` 复制新句柄，不会转移所有权，也不会延长原对象的生命周期。

包含 `<hx_os_native.h>` 或总头文件 `<hx_os.h>` 即可使用。该模块由 `HX_OS_ENABLE_NATIVE_INTEROP` 控制（默认开启），运行时可通过 `hx_os_native_interop_available()` 查询编译期能力；套接字和进程查询还分别要求启用各自所属的模块，关闭对应组合时调用会返回 `HX_OS_NOT_SUPPORTED`。

| 借用查询入口 | Windows 返回值 | POSIX 返回值 |
| --- | --- | --- |
| `hx_os_file_native_borrow` | 转为 `uintptr_t` 的文件 `HANDLE` | 转为 `uintptr_t` 的非负文件描述符（`fd`） |
| `hx_os_socket_native_borrow` | 转为 `uintptr_t` 的 `SOCKET` | 转为 `uintptr_t` 的非负套接字描述符（`fd`） |
| `hx_os_process_native_borrow` | 转为 `uintptr_t` 的子进程 `HANDLE` | 转为 `uintptr_t` 的子进程 `pid_t`（**注意：这是进程 ID，不是文件描述符**） |
| `hx_os_thread_native_borrow` | 将线程 `HANDLE` 复制到调用方原生类型变量 | 原样按字节复制 `pthread_t`（不假定 `pthread_t` 是整数类型） |

所有查询在失败时均保持调用方的输出变量不变。查询操作本身零内存分配、零系统调用、不加互斥锁、也不启动后台线程（线程与套接字状态检查直接读取已有的原子状态位）。请注意，借出原生句柄不会为原生阻塞调用增加取消能力。

## 2. 把已有文件交给原生查询

假设在一个返回 `hx_os_status` 的函数中，你已经持有一个打开的 `hx_os_file *file`，下面的代码演示如何借出原生句柄并调用平台特定的文件查询 API：

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

编译 Windows 分支需包含 `<windows.h>`，POSIX 分支需包含 `<sys/stat.h>`（生产代码中可根据需要保留原生 `GetLastError()` 或 `errno` 诊断信息）。

> [!WARNING]
> **原生文件句柄借用禁忌**
> - **严禁在外部关闭借用句柄**：完成所有原生 API 调用后，必须通过本库的 `hx_os_file_close` 关闭 `file`，**绝不能**对借出的 `native` 句柄调用 `CloseHandle()` 或 `close()`。
> - **共享同一底层文件状态与游标**：借出的句柄与 `hx_os_file` 指向同一个内核打开文件描述；通过原生 API 执行 `seek`、`read` 或 `write` 会直接改变本库观察到的文件读写偏移量与状态。短读短写和 POSIX `SIGPIPE` 信号也需由直接调用原生接口的代码自行处理。

## 3. 线程与进程生命周期

由于 POSIX 标准允许 `pthread_t` 实现为结构体或指针而非整数，借用原生线程句柄时应先包含平台原生线程头文件，声明真实的原生类型变量并传入 `sizeof(native)`：

```c
#if defined(_WIN32)
HANDLE native;
#else
pthread_t native;
#endif

hx_os_status status = hx_os_thread_native_borrow(
	thread, &native, sizeof(native), NULL);
```

在 POSIX 上包含 `<pthread.h>` 即可，无需将 `pthread_t` 强转为整数（如果是通用脚本绑定层需要动态分配存储，可先调用 `hx_os_thread_native_size()` 查询字节大小）。当传入的目标缓冲区容量不足时，函数返回 `HX_OS_OUTPUT_FULL`；容量大于所需大小时，多余尾部字节保持不变。

- **线程借用生命周期边界**：对已经完成 `join` 的线程调用借用会返回 `HX_OS_CLOSED`；对正在执行 `join` 的线程调用则返回 `HX_OS_BUSY`。**切勿对借出的原生线程调用 `pthread_join`、`pthread_detach`、`pthread_cancel`、`WaitForSingleObject` 或强杀线程**——线程的生命周期回收必须始终通过本库接口完成。
- **子进程借用生命周期边界**：子进程的原生句柄/PID 只能在本库等待并回收（Reap）该子进程退出之前借用。一旦通过本库成功等到子进程退出，无论在 POSIX 还是 Windows 上（哪怕 Windows 内部在销毁 `hx_os_process` 对象前仍持有内核 `HANDLE`），再次调用 `hx_os_process_native_borrow` 都会返回 `HX_OS_CLOSED`。如果仅需获取用于日志诊断的进程号，请使用 `hx_os_process_identifier()`。**切勿在外部调用 `waitpid` 抢先回收子进程或关闭其 Windows 句柄**，否则会破坏库内部的状态同步，甚至因操作系统复用 PID 而误操作其他进程。

## 4. 套接字与并发边界

- **已绑定事件循环的套接字不可借用**：一旦套接字已经附加（Attach）到本库的异步事件循环（`hx_os_loop`），哪怕当前没有任何进行中的请求，调用 `hx_os_socket_native_borrow` 也会返回 `HX_OS_BUSY`；同样，存在未完成的异步请求时也禁止借用。原因在于该套接字的 I/O 多路复用或 IOCP 完成端口已由 `loop` 接管，不能再将其原生描述符交给外部竞争的事件循环。
- **在整个原生调用期间保护所有者存活**：调用方不仅要在调用 `*_native_borrow` 的那一瞬间保证原对象有效，还必须在**整个后续原生 API 执行期间**阻止其他线程关闭/销毁该对象、执行 `join`/回收子进程、将其接入事件循环或发起冲突的读写操作。借出一个整数值并不会为原对象增加引用计数。
- **禁止篡改底层标志位**：不要在库背后修改描述符的 `O_NONBLOCK` / `O_APPEND` / `OVERLAPPED` 模式、句柄继承性（`CLOEXEC`）或事件多路复用注册；将借用句柄传给第三方库函数前，请确认该函数也只是临时借用句柄，不会夺取所有权或篡改上述底层模式。
- **不导出同步原语内部内存**：互操作层不暴露互斥锁（Mutex）或条件变量（Condition Variable）的内部原生存储字节，以避免业务代码与特定平台的底层结构布局产生耦合。

## 5. C++11 与可运行检查

在 C++11 中，只需包含 `<hx/Native.hpp>`，即可通过返回 `hx::Result<T>` 的包装函数安全借用原生句柄（全程不使用 C++ 异常，析构时也不会关闭借用值）：

```cpp
auto handle = hx::Native::file(file);
if (!handle)
	return handle.status();

// Use handle.value() synchronously with the appropriate native API.
// The original file owner must remain alive until that use finishes.
return HX_OS_OK;
```

`hx::Native::socket`、`hx::Native::process` 和 `hx::Native::thread` 采用完全一致的 `hx::Result` 调用方式。其中 `hx::Native::thread` 返回 `hx::Result<hx::NativeThread>`（在 Windows 上对应 `void *` / `HANDLE`，在 POSIX 上对应 `pthread_t`；`<hx/Native.hpp>` 仅在 POSIX 平台包含 `<pthread.h>`）。

完整示例 [native_file.c](../examples/native_file.c) 演示了先借出原生句柄查询文件大小、再通过本库接口读取文件内容并最终关闭的完整流程（注：该示例的命令行解析部分使用了 `HX_OS_ENABLE_SYSTEM_UTILS` 以支持跨平台 UTF-8 参数，但互操作模块本身不依赖 `SYSTEM_UTILS`）。各目标平台的详细约束见[平台支持](PLATFORMS.zh-CN.md)。

## 4. 导入自己持有的 socket

前文介绍的 `borrow` 接口适用于“资源由本库创建并持有、临时借给外部使用”的场景。反过来，如果你在外部已经创建了一个原生的非阻塞 TCP 或 UDP 套接字，希望将其交给本库管理，可以使用 `<hx_os_net_interop.h>` 中的 `hx_os_socket_import`：

- **`HX_OS_SOCKET_TAKE`（转移所有权）**：仅在导入成功时接管你传入的原生套接字句柄（若导入失败，原句柄仍由调用方自己负责关闭）。
- **`HX_OS_SOCKET_DUP`（复制新句柄）**：保留调用方手中的原句柄不变，在内部为同一个底层通信端点复制出第二个独立句柄交由本库管理。

无论选择哪种模式，导入前都必须确保该套接字上没有正在进行的 I/O、并发关闭或外部事件循环注册。在 Windows 上，调用方还必须保证传入的 `SOCKET` 在创建时已开启 `WSA_FLAG_OVERLAPPED` 重叠 I/O 能力，且尚未绑定到任何 IOCP 完成端口（Windows 内核不支持将已关联的 IOCP 句柄迁移到新的完成端口）。关于双句柄共享内核套接字状态及关闭超时的详细说明，请参阅[原生 socket 与关闭](NETWORK_IO.zh-CN.md)。
