# UDP 业务示例：传感器上报与双向反馈

[English](UDP_BUSINESS.md) · [README](../README.zh-CN.md) · [socket C 接口](../include/hx_os_net.h)

构建前先完成一次[示例配置](../examples/README.zh-CN.md#run)。下文的 build 是已有构建目录的占位名，换成自己的目录即可，不必为每种协议再建一个目录。命令从库源码根目录执行；只构建本例需要的两端程序。

## 1. 独立的 UDP 数据报程序

本例使用 `hx_os_socket_send_to()` 和 `hx_os_socket_receive_from()`，没有 IPC 连接、HXIP 帧或隐式握手。传感器端通过一个线程发送样本，通过另一个线程接收反馈；接收端保留两个设备的最新状态并回复。关闭 IPC 模块仍能运行。

UDP 保留丢失、重复和乱序的语义。这个例子选择“保留最新状态”，不会把每个样本都必须到达作为真实业务承诺。

## 2. 构建与运行

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --target hx_os_udp_dashboard
cmake --build build --config Release --target hx_os_udp_sensor
```

Windows PowerShell，Visual Studio 构建：

```powershell
python examples/run_udp_business.py --bin-dir build/bin/Release
python examples/run_udp_business.py --simulate-gaps --bin-dir build/bin/Release
```

Linux/WSL 使用 `python3` 和 `--bin-dir build/bin`。也可使用已有 `build-shared-msvc/bin/Release` 或 `build-shared-linux/bin`；Windows Ninja 去掉 `Release`。脚本自动选端口，检查两个进程的状态和反馈。

手工运行，Windows **终端一**：

```powershell
.\build\bin\Release\hx_os_udp_dashboard.exe 39002
```

看到 `Listening:` 后，**终端二**：

```powershell
.\build\bin\Release\hx_os_udp_sensor.exe 39002 --simulate-gaps
```

去掉 `--simulate-gaps` 就是普通演示。Linux/WSL 分别运行 `./build/bin/hx_os_udp_dashboard 39002` 和 `./build/bin/hx_os_udp_sensor 39002 --simulate-gaps`。两端都使用 `127.0.0.1`；参数是端口号，不能填 `udp://`。每次重跑重新启动接收端。

## 3. 一个数据报就是一个 JSON 对象

```mermaid
flowchart LR
    TX[传感器发送线程] -->|JSON sample| S[接收端：解析、校验、保留最新值]
    S -->|JSON latest / summary| RX[传感器接收线程]
```

发送的采样正文如下，不添加 TCP 长度前缀：

```json
{"type":"sample","device_id":101,"seq":10,"temperature_centi":2510}
```

`device_id`、`seq`、`temperature_centi` 必须是 JSON 整数。温度单位是百分之一摄氏度，`2510` 表示 `25.10°C`。设备为 101/102，序号为 1～10，温度范围为 -10000～20000。缺失字段、类型错误、语法损坏和重复键都不更新设备状态。

两个设备各发 10 轮数据，每轮约 30 毫秒。接收端每接受三个新的样本就向来源地址反馈：

```json
{"type":"latest","device_id":101,"seq":3}
```

收集窗口结束后反馈统计和最终温度：

```json
{"type":"summary","device_id":101,"accepted":10,"newest":10,"gaps":0,"stale":0,"temperature_centi":2510}
```

设备编号在 JSON 正文里；来源由 `receive_from()` 返回的地址和端口确定。接收端等待首条数据最多 10 秒，之后收集 1 秒；传感器接收线程等待 2 秒，到期正常结束。反馈缺失不会导致无限等待。

## 4. 缺号、重复与晚到

| 情况 | 处理 |
| --- | --- |
| 序号更新 | 更新温度，记录观察到的序号空缺 |
| 序号相同或更旧 | 增加 `stale`，不覆盖新温度 |
| 同设备来自另一个地址 | 本次运行中忽略，保持首次来源绑定 |
| 数据报超出容量被截断 | 明确报错，不尝试把剩余部分当新消息 |

`--simulate-gaps` 主动漏发 3/7、重复 4、在 8 后发送 6。它控制业务输入，不测量真实网络丢包率。正常回环时每设备为 `accepted=10,newest=10,gaps=0,stale=0`；异常演示为 `accepted=8,newest=10,gaps=2,stale=2`，最新温度不被旧数据覆盖。

普通 UDP 仍可能丢失、重复、乱序。C 传感器程序允许没有反馈；自动测试要求这次小批量回环实际收到预期的 JSON 汇总，缺失时报告测试失败，不把普通 UDP 描述成可靠传输。

## 5. 到哪里改字段与业务

| 源码 | 重点 |
| --- | --- |
| [udp_sensor.c](../examples/udp_sensor.c) | `publish_sample()` 构造 JSON；发送线程上报，`receive_feedback()` 解析并校验 JSON 回复 |
| [udp_dashboard.c](../examples/udp_dashboard.c) | `receive_sample()` 读取整数、验证范围和来源、更新状态；窗口结束编码 JSON 汇总 |
| [udp_business.h](../examples/udp_business.h) | 完整数据报收发与有限等待 |
| [run_udp_business.py](../examples/run_udp_business.py) | 使用 `json.loads` 检查两个设备的统计与反馈 |

构造正文的方式是类型化字段数组：

```c
json_field fields[] = {
	json_text("type", "sample"), json_int("device_id", 101),
	json_int("seq", 1), json_int("temperature_centi", 2501)
};
```

将字段交给 `json_encode()`，再用本例 `udp_send_text()` 发送编码结果。完整创建、检查和释放流程都在 `publish_sample()` 中。辅助代码 [business_json.h](../examples/business_json.h) 只处理 JSON 编解码；UDP 业务不经过 IPC。

默认启用的 `CONFIG`、`CONFIG_SERIALIZE`、`SETTINGS` 提供内置解析与安全字符串转义。正文为 UTF-8，最多 1023 字节；解析深度最多 8、节点最多 64、内存限额 64 KiB。报文用完释放解析文档和临时字符串；不能保存已释放文档内的节点/字符串指针。

两个线程的非阻塞 socket 调用按既有契约在短互斥区内串行，重试休眠在锁外。主线程先 join 再释放资源。真实设备重启后如果复用序号，需要在自己的 JSON 协议中加入会话标识；地址绑定不是身份认证。

### 放进自己的工程时要复制什么

复制本页服务端／客户端源码和同目录的 `udp_business.h`、`network_demo.h`、`business_json.h`。两个带 main 的源文件分别建立 executable，不要放进同一个可执行目标。每个目标链接 `hx_c_platform::hx_c_platform`；安装包接入见[最小工程](USAGE.zh-CN.md#installed-project)。`json_field`、`json_encode` 等是这些头文件中的示例辅助代码，不是只包含 hx_os.h 就会出现的公共接口。

Python 运行器仅方便本机启动两端和查看结果。自己的 C 程序无需 Python；先用本页的手动命令在两个终端分别运行，再替换消息字段和业务处理。保留创建、收发线程退出及关闭顺序。

## 6. 运行业务示例

```sh
python examples/run_udp_business.py --bin-dir build/bin/Release
python examples/run_udp_business.py --bin-dir build/bin/Release --simulate-gaps
```

第一条运行正常采样与反馈；第二条加入缺号、重复和迟到数据，演示接收方的处理方式。Linux 的程序目录改为 build/bin。

在 WSL2 镜像模式下，`127.0.0.1` 也可能经过 MTU 1500 的虚拟网络；本机实测超过 1472 字节的 IPv4 UDP 载荷出现分片丢失。此示例的 JSON 上限为 1023 字节，保持小数据报。较大业务消息可参考 [KCP 示例](KCP_BUSINESS.zh-CN.md) 或 [TCP 示例](TCP_BUSINESS.zh-CN.md)；库不会自动拆分普通 UDP 或改变其丢包语义。WSL 内回环与 Windows／WSL 通信需分别验证，详见 [IPC 的平台说明](IPC.zh-CN.md)。
