# 一次初始化

[English](ONCE.md)

## 1. 共享数据只初始化一次

`hx_os_once` 让多个线程共用一次成功初始化的结果。对象独立持有，不自动启动线程，不修改进程全局配置。

```c
static hx_os_status initialize(void *user, hx_os_error *error)
{
	int *port = user;

	(void)error;
	*port = 8080;
	return HX_OS_OK;
}

hx_os_once *once = NULL;
int port = 0;

hx_os_status status = hx_os_once_create(NULL, &once, NULL);
if (status == HX_OS_OK)
	status = hx_os_once_run(once, initialize, &port, NULL);
```

工作线程使用期间，`once` 和 `port` 都要保持有效。各调用者应使用同一种初始化逻辑及同一份共享数据。初始化返回 `OK` 后，后续成功调用者可以安全看到初始化写入的数据。失败不会永久锁定对象，之后允许重试；回调需要自行回收本次失败时已经创建的资源。

## 2. 等待和生命周期

`run_until` 可以为等待其他线程初始化增加单调截止时间及取消令牌，不能打断正在执行的初始化回调。对象尚未初始化时，`NOW` 也允许直接开始初始化；已经成功时优先返回成功。回调重新进入同一个 once 对象返回 `DEADLOCK`。

销毁前要排除新增及正在执行的调用。检测到正在使用时返回 `BUSY`，对象仍可继续使用。不提供重置，避免让已经发布的数据突然失效。销毁 once 不会销毁业务初始化的资源，那些资源仍由业务管理。

初始化已经成功后，即使传入的取消令牌事先已取消，或截止时间是 NOW，run_until 仍返回 OK。这个完成路径不再订阅取消通知，也不重复执行初始化回调。

C++ `hx::Once::run` 接受返回 `void` 或 `Result<void>` 的函数／lambda，不使用异常。对象必须活到所有调用结束，业务数据也要独立管理生命周期。

## 3. 示例

构建并运行 `hx_os_once`。[once.c](../examples/once.c) 启动四个线程，初始化提示只出现一次，每个线程都读取端口 8080。开关为 `HX_OS_ENABLE_ONCE`；取消功能需要协调模块，普通初始化无需全局初始化入口。
