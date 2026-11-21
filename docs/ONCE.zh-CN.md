# 一次初始化

[English](ONCE.md)

当多个工作线程都依赖同一份懒加载配置或共享资源，且必须保证该初始化逻辑只成功执行一次、所有后续线程都能安全读取初始化结果时，可以使用 `hx_os_once`。

### 快速运行示例

完整示例位于 [once.c](../examples/once.c)：创建一个 `hx_os_once` 对象并启动 4 个工作线程，每个线程都先调用 `hx_os_once_run()`，成功后再读取初始化好的端口号，最后在主线程 `join` 所有工作线程并销毁 `once` 对象。

按[示例运行方法](../examples/README.zh-CN.md#run)启用示例后，构建并运行 `hx_os_once`（无需命令行参数或输入文件）：

```sh
cmake --build build --config Release --target hx_os_once
```

预期输出中，`Settings initialized once` 只会打印 1 次，而 `Worker uses port 8080` 会打印 4 次。在实际工程中，你也可以参考[最小工程配置](USAGE.zh-CN.md#c-project)直接将完整示例作为 `main.c` 模板。

## 1. 共享数据只初始化一次

`hx_os_once` 是由调用方显式持有生命周期的同步对象，不会隐式创建后台线程，也不会污染进程全局状态。

下面这段直接可运行的 `main.c` 连续调用了两次 `hx_os_once_run()`，但初始化函数只会执行一次（多线程并发竞争的完整写法见 [once.c](../examples/once.c)）：

```c
#include <hx_os.h>
#include <stdio.h>

static hx_os_status initialize(void *user, hx_os_error *error)
{
	int *port = user;

	(void)error;
	*port = 8080;
	puts("Settings initialized once");

	return HX_OS_OK;
}

int main(void)
{
	hx_os_once *once = NULL;
	int port = 0;

	hx_os_status status = hx_os_once_create(NULL, &once, NULL);
	if (status == HX_OS_OK)
		status = hx_os_once_run(once, initialize, &port, NULL);
	if (status == HX_OS_OK)
		status = hx_os_once_run(once, initialize, &port, NULL);

	if (status == HX_OS_OK)
		printf("Port: %d\n", port);
	else
		fprintf(stderr, "Initialize: %s\n", hx_os_status_name(status));

	(void)hx_os_once_destroy(&once, NULL);

	return status == HX_OS_OK ? 0 : 1;
}
```

- **内存可见性保证**：当初始化回调返回 `HX_OS_OK` 后，所有当前正在等待以及后续调用 `hx_os_once_run()` 的线程都会收到 `HX_OS_OK`，并且无需额外加锁即可安全看到初始化回调写入共享数据（如 `port`）的结果。
- **失败可重试**：如果某次初始化回调返回了非 `HX_OS_OK` 的错误状态，`once` 对象**不会**被永久锁死在失败状态，后续调用者可以再次尝试初始化。注意：初始化回调在返回错误前，必须自行清理本次失败过程中已分配的半成品资源。

## 2. 等待和生命周期

除了无限等待的 `hx_os_once_run()`，还可以使用 `hx_os_once_run_until(once, fn, user, deadline, token, error)` 为锁竞争等待阶段指定单调时钟截止时间和可选的取消令牌：
- **不中断正在执行的初始化回调**：截止时间与取消令牌仅作用于“等待其他线程完成初始化”的阻塞阶段，无法强行打断已经在执行中的初始化回调。
- **已完成快路径优先**：一旦初始化已经成功完成，后续调用 `hx_os_once_run_until()` 无论传入的是 `HX_OS_DEADLINE_NOW` 还是事先已取消的令牌，都会立即走快路径返回 `HX_OS_OK`，不会重复执行回调。当对象尚未初始化且当前无其他线程正在初始化时，传入 `HX_OS_DEADLINE_NOW` 也会直接开始执行初始化回调。
- **递归重入检测**：如果在初始化回调函数内部再次对同一个 `hx_os_once` 对象调用 `run` / `run_until`，会立即返回 `HX_OS_DEADLOCK`。

> [!IMPORTANT]
> **销毁边界与业务资源管理**
> - 调用 `hx_os_once_destroy()` 前，必须确保所有工作线程均已结束对 `once` 的调用（若检测到仍有线程正在执行或等待初始化，会返回 `HX_OS_BUSY` 并保留原对象）。
> - `hx_os_once` 故意不提供 `reset` 重置接口，以防已发布给其他线程无锁读取的共享数据在中途失效。
> - `hx_os_once_destroy()` 只销毁同步原语本身，**不会**释放你在 `initialize` 回调中创建的业务资源；请在所有工作线程退出后按业务规则清理共享数据。

在 C++11 中，`<hx/Once.hpp>` 提供了无异常的 `hx::Once::run`，支持传入返回 `void` 或 `Result<void>` 的函数对象与 lambda。

## 3. 示例

构建并运行 `hx_os_once` 即可验证 [once.c](../examples/once.c) 中的 4 线程并发初始化流程。该模块由 `HX_OS_ENABLE_ONCE` 开关控制；可取消等待依赖线程协调模块（`HX_OS_ENABLE_THREAD_EX`），普通初始化无需调用任何全局库初始化函数。
