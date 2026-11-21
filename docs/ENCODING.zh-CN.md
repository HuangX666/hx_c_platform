# Base64 与十六进制编码

[English](ENCODING.md)

当需要把包含 `\0`（NUL）字节或高位字节的原始二进制数据放进 JSON 字段、配置文件、URL 参数或纯文本协议时，直接当作 C 字符串处理会意外截断，也可能破坏 UTF-8 校验。本模块提供零外部依赖的 Base64、Base64URL 与十六进制（Hex）双向转换接口，帮助你在任意二进制字节流与可打印 ASCII 文本之间安全往返。

你可以先运行 [encoding.c](../examples/encoding.c) 查看完整流程：程序准备了 `00 01 fe ff` 四个原始字节，调用 `hx_os_base64_encode_string()` 编码为文本，再调用 `hx_os_base64_decode()` 还原到调用方字节数组并按实际解码长度读取。按[示例运行方法](../examples/README.zh-CN.md#run)启用示例后，构建并运行 `hx_os_encoding`（无需命令行参数或输入文件）：

```text
Base64: AAH+/w==
Decoded 4 bytes; last byte: 255
```

你也可以按[最小工程配置](USAGE.zh-CN.md#c-project)将该示例源码直接作为 `main.c` 编译。

> [!NOTE]
> Base64 与十六进制仅用于**数据表示（Encoding）**，任何拿到文本的一方都可以直接还原原始字节。若业务需要机密性或完整性保护，请配合加密与消息认证模块使用。

## 1. 不依赖 OpenSSL 的字节编码

开启 CMake 选项 `HX_OS_ENABLE_ENCODING`（默认开启）即可使用 Base64、Base64URL 和十六进制转换。该模块由纯 C99 实现，不需要链接 OpenSSL、不需要初始化网络子系统，也不依赖容器模块。若在构建时关闭该选项，相关函数符号依然保留，调用时明确返回 `HX_OS_NOT_SUPPORTED`。

```c
#include <hx_encoding.h>
#include <stdio.h>

int main(void)
{
	hx_string encoded = HX_STRING_INIT;
	hx_os_status status =
		hx_os_base64_encode_string("hello", 5, 0, &encoded, NULL);

	if (status == HX_OS_OK)
		puts(hx_string_c_str(&encoded));

	hx_string_destroy(&encoded);

	return status == HX_OS_OK ? 0 : 1;
}
```

上述代码输出 `aGVsbG8=`；若改用 `hx_os_hex_encode_string()`，则会输出 `68656c6c6f`。对于包含内嵌 `\0` 和大于 `127` 的字节序列，完整的编码与解码往返写法可参考 [完整编码示例](../examples/encoding.c)。

## 2. 明确选择 Base64 格式

通过 `flags` 参数可以精确控制 Base64 的字母表与末尾填充（Padding）策略：

| `flags` 取值 | 字母表 | `=` 填充规则 |
|---|---|---|
| `0` | 标准字母表 `A-Z a-z 0-9 + /` | 编码时按需补齐 `=`；解码时若最后分组不足 4 字符则必须带 `=`。 |
| `HX_OS_BASE64_URL` | URL 安全字母表（用 `-` 和 `_` 替换 `+` 和 `/`） | 编码时按需补齐 `=`；解码时必须带 `=`。 |
| `HX_OS_BASE64_NO_PADDING` | 标准字母表 `A-Z a-z 0-9 + /` | 编码时不输出 `=`；解码时若出现 `=` 则拒绝并返回错误。 |
| `HX_OS_BASE64_URL \| HX_OS_BASE64_NO_PADDING` | URL 安全字母表 | 编码时不输出 `=`；解码时拒绝 `=`。 |

> [!IMPORTANT]
> **严格解码校验规则：**
> - **Base64 解码**：严格拒绝空格、换行符、混用标准与 URL 字母表、多余或缺失的 `=` 填充、不可能出现的余数长度，以及末尾未使用的低位比特不为 `0` 的非规范编码（例如 `'f'` 的规范编码是带填充的 `Zg==` 或无填充的 `Zg`；若传入末尾废位非零的 `Zh==`，解码会返回 `HX_OS_INVALID`）。
> - **十六进制转换**：编码默认输出小写字母 `0-9a-f`，传入 `HX_OS_HEX_UPPERCASE` 可输出大写 `0-9A-F`。解码时同时接受大小写十六进制字符，但严格拒绝奇数个字符、`0x` 前缀、空白字符或冒号／连字符等分隔符。
> - **显式字节长度**：所有接口均按显式传入的字节数处理输入，切勿把 C 字符串末尾的终止符 `\0` 误计入需要编码的载荷长度（除非你确实想把 `\0` 字节也编码进去）。

## 3. 缓冲区与所有权

底层四个缓冲区接口（`hx_os_base64_encode`、`hx_os_base64_decode`、`hx_os_hex_encode`、`hx_os_hex_decode`）都会先完整校验输入并计算所需输出大小。任何校验失败或容量不足都不会污染输出缓冲区：

- **长度汇报（`required`）**：当输入合法且返回 `HX_OS_OK` 或 `HX_OS_OUTPUT_FULL` 时，`*required` 会被写入所需的输出容量；若输入本身不合法（如返回 `HX_OS_INVALID`），`*required` 保持原值不变。
- **编码与解码的终止符差异**：
  - **编码接口**的 `required` 字节数**包含**末尾自动写入的 `\0` 终止符。
  - **解码接口**输出的是纯二进制字节流，`required` 为实际还原出的二进制字节数，**不包含**末尾 `\0`。
- **预查询长度**：传入 `output = NULL, capacity = 0` 可在不写入数据的情况下查询所需容量。注意：对空输入（长度为 `0`）执行编码仍需要 `1` 字节存放末尾 `\0`；对空输入执行解码则直接返回 `HX_OS_OK`，所需字节数为 `0`。
- **禁止原地重叠转换**：输入缓冲区、输出缓冲区以及 `required` 指针指向的对象必须位于互不重叠的内存区域，不支持原地（in-place）转换。
- **内存分配与上限**：
  - 缓冲区接口本身**不分配任何堆内存**。你可以直接传入栈上数组，或先查询 `required` 大小再通过 `hx_os_memory_alloc()` 分配内存（用完后调用 `hx_os_memory_free()` 释放）。
  - 字符串便利接口（`*_encode_string`）使用目标 `hx_string` 绑定的内存上下文，支持输入视图借用同一个 `hx_string` 的原有内容，并在失败时保持原字符串不变。字符串便利接口的正文上限为 **16 MiB**；若需编码超过 16 MiB 的大块数据，请改用调用方自管内存的缓冲区接口。

## 4. C++11 返回值

在 C++11 中，包含 `<hx/Encoding.hpp>` 即可使用静态方法直接获取 `hx::Result<T>`：

```cpp
#include <hx/Encoding.hpp>

int main()
{
	auto encoded = hx::Encoding::base64("hello", 5);
	if (!encoded)
		return 1;

	auto bytes = hx::Encoding::fromBase64("aGVsbG8=");
	if (!bytes)
		return 1;

	return bytes.value().size() == 5 ? 0 : 1;
}
```

- `hx::Encoding::base64()` 与 `hx::Encoding::hex()` 返回拥有文本的 `hx::Result<hx::String>`。
- `hx::Encoding::fromBase64()` 与 `hx::Encoding::fromHex()` 返回拥有二进制字节数组的 `hx::Result<hx::Buffer>`（`hx::Buffer` 会在析构时自动释放内存）。
- C++11 拥有型结果同样遵循 **16 MiB** 的输出上限，全程不使用 C++ 异常，并通过 `hx::Result` 保留完整的状态码与错误信息。访问 `.value()` 前请先检查结果是否成功。
