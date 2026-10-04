# UDP 业务示例：传感器上报与双向反馈

[English](UDP_BUSINESS.md) · [README](../README.zh-CN.md) · [socket C 接口](../include/hx_os_net.h)

## 1. 独立的 UDP 数据报程序

本例使用 `hx_os_socket_send_to()` 和 `hx_os_socket_receive_from()`，没有 IPC 连接、HXIP 帧或隐式握手。传感器端通过一个线程发送样本，通过另一个线程接收反馈；接收端保留两个设备的最新状态并回复。关闭 IPC 模块仍能运行。

UDP 保留丢失、重复和乱序的语义。这个例子选择“保留最新状态”，不会把每个样本都必须到达作为真实业务承诺。

## 2. 构建与运行

```sh
cmake -S . -B build-net -DHX_OS_BUILD_TESTS=ON -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build-net --config Release --parallel 4
```

Windows PowerShell，Visual Studio 构建：

```powershell
python examples/run_udp_business.py --bin-dir build-net/bin/Release
python examples/run_udp_business.py --simulate-gaps --bin-dir build-net/bin/Release
```

Linux/WSL 使用 `python3` 和 `--bin-dir build-net/bin`。也可使用已有 `build-shared-msvc/bin/Release` 或 `build-shared-linux/bin`；Windows Ninja 去掉 `Release`。脚本自动选端口，检查两个进程的状态和反馈。

手工运行，Windows **终端一**：

```powershell
.\build-net\bin\Release\hx_os_udp_dashboard.exe 39002
```

看到 `Listening:` 后，**终端二**：

```powershell
.\build-net\bin\Release\hx_os_udp_sensor.exe 39002 --simulate-gaps
```

去掉 `--simulate-gaps` 就是普通演示。Linux/WSL 分别运行 `./build-net/bin/hx_os_udp_dashboard 39002` 和 `./build-net/bin/hx_os_udp_sensor 39002 --simulate-gaps`。两端都使用 `127.0.0.1`；参数是端口号，不能填 `udp://`。每次重跑重新启动接收端。

## 3. 两个方向同时通信

```mermaid
flowchart LR
    TX[传感器发送线程] -->|SAMPLE 数据报| S[接收端：最新状态]
    S -->|LATEST / SUMMARY 数据报| RX[传感器接收线程]
    Main[主线程：socket、线程、join、释放] --> TX
    Main --> RX
```

示例正文为 `SAMPLE device=101 seq=10 temperature_centi=2510`。`device` 是设备编号；`seq` 是该设备本次运行的样本序号；`temperature_centi` 以百分之一摄氏度计，`2510` 表示 `25.10°C`。正文是一个最长 127 字节的 ASCII 数据报，不添加 TCP 长度前缀。

发送线程对设备 `101`、`102` 发 10 轮样本，每轮约间隔 30 毫秒。接收端每接受三个新的样本就向来源地址发送 `LATEST device=101 seq=N`；窗口结束后发送 `SUMMARY`。设备编号属于正文，来源由 `receive_from()` 取得的地址和端口确定。

接收端等待首条数据最多 10 秒，之后收集 1 秒。反馈线程等待 2 秒，到期正常结束；反馈丢失时不会无限等待。这些数字是演示的应用时限。

## 4. 缺号、重复与晚到

| 情况 | 接收端行为 |
| --- | --- |
| `seq` 大于已保存值 | 更新温度，累加观察到的序号空缺 |
| `seq` 等于或小于已保存值 | 累加 `stale`，不覆盖最新温度 |
| 数据报过长被截断 | 返回明确错误，不把剩余字节当成下一条 |
| 同设备来自另一个地址 | 当前演示忽略，保持首次来源绑定 |

`--simulate-gaps` 主动漏发 `3`、`7`，重复 `4`，在 `8` 后再次发送 `6`。这是受控业务输入，不是测量真实网络丢包率。

| 模式 | accepted | newest | gaps | stale |
| --- | --- | --- | --- | --- |
| 正常小批量回环 | 10 | 10 | 0 | 0 |
| 异常序号演示 | 8 | 10 | 2 | 2 |

```text
[UDP receive thread] SUMMARY device=101 accepted=8 newest=10 gaps=2 stale=2
[UDP receive thread] SUMMARY device=102 accepted=8 newest=10 gaps=2 stale=2
UDP SENSOR PASS: send/receive threads joined; feedback=6
```

`feedback=6` 是受控演示的预期值。C 程序允许 UDP 反馈缺失；自动测试要求这次小批量回环确实得到预期结果，缺失时报告用例失败。它没有给普通 UDP 增加可靠送达语义。

## 5. 源码与所有权

| 源码 | 重点 |
| --- | --- |
| [udp_sensor.c](../examples/udp_sensor.c) | `publish_samples()` 发送线程，`receive_feedback()` 接收线程，`main()` 持有资源 |
| [udp_dashboard.c](../examples/udp_dashboard.c) | `receive_sample()` 校验数值、更新每设备状态、回复来源 |
| [udp_business.h](../examples/udp_business.h) | UDP 完整数据报收发、容量限制、有限等待 |
| [run_udp_business.py](../examples/run_udp_business.py) | 独立的正常/异常序号检查 |

例如发送线程准备好正文与目标地址后使用：

```c
if (!udp_send_text(job->endpoint, &job->destination, sample))
	return 0;
```

这是示例私有辅助函数，内部直接 `send_to()`；没有转发给 IPC。两个线程的非阻塞 socket 调用用短互斥区串行，重试时在锁外休眠 1 毫秒。主线程先 join，再销毁互斥锁、socket 和 network；线程参数在整个借用期间有效。没有创建业务消息队列。

本例只覆盖一次进程运行。真实设备重启后如果会重复使用旧序号，应在自己的数据报协议加入会话编号并定义切换规则；地址绑定不是身份认证。要求可靠传输的业务请另看 [KCP 示例](KCP_BUSINESS.zh-CN.md) 或 [TCP 示例](TCP_BUSINESS.zh-CN.md)。

## 6. 自动测试

```sh
ctest --test-dir build-net -C Release -R "^udp_business_" --output-on-failure
```

分别运行 `udp_business_samples` 和 `udp_business_gaps`，检查两个设备的最终温度、序号统计、双向反馈和线程/进程结束。测试不使用 IPC 业务程序。具体目标验证见[验证记录](VALIDATION.zh-CN.md)。
