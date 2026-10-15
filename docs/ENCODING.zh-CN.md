# Base64 与十六进制编码

[English](ENCODING.md)

**先运行一个完整例子。** 需要把包含 NUL 的二进制数据放进 JSON 字符串或纯文本协议时，用 Base64／十六进制表示这些字节。

打开 [encoding.c](../examples/encoding.c)。准备 00 01 fe ff → base64_encode_string → base64_decode 到调用者数组 → 按实际字节数读取 → destroy 字符串。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 examples，并只构建 `hx_os_encoding`。此例无命令行参数、无需输入文件。

**会得到什么：**输出 `Base64: AAH+/w==` 和 `Decoded 4 bytes; last byte: 255`。这只是表示方式，任何人都可以解码；需要保密时使用加密模块。

下面的短片段用于解释接口；复制运行时以完整示例为准。也可按[最小工程配置](USAGE.zh-CN.md#c-project)把完整源码作为自己的 main。

## 1. 不依赖 OpenSSL 的字节编码

开启 `HX_OS_ENABLE_ENCODING` 即可使用 Base64、Base64URL 和十六进制转换。这些是字节的文本表示，不是加密，不要求 OpenSSL、网络初始化或容器模块。关闭模块后函数仍存在，返回 `HX_OS_NOT_SUPPORTED`。

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

输出为 `aGVsbG8=`。改用 `hx_os_hex_encode_string` 则得到 `68656c6c6f`。[完整编码示例](../examples/encoding.c) 演示包含 NUL 和大于 127 的字节进行编码与解码。复用已经开启 examples 的构建目录，编译 `hx_os_encoding` 目标。

## 2. 明确选择 Base64 格式

| flags | 字母表 | 填充 |
|---|---|---|
| `0` | 标准 `A-Z a-z 0-9 + /` | 最后分组需要时必须带 `=`。 |
| `HX_OS_BASE64_URL` | URL 安全格式，用 `- _` 替换 `+ /` | 需要时必须带 `=`。 |
| `HX_OS_BASE64_NO_PADDING` | 标准格式 | 编码不写 `=`，解码拒绝 `=`。 |
| 两个标志按位或 | URL 安全格式 | 编码不写 `=`，解码拒绝 `=`。 |

解码拒绝空格、换行、混用字母表、多余或缺少的填充、不可能的长度，以及末尾未使用位不为零的输入。例如带填充的 `Zg==` 和不带填充的 `Zg` 在各自模式下都表示 `f`；`Zh==` 的未使用位非零，因此拒绝。接口接收明确长度，不把 C 字符串末尾的 NUL 计入长度。

十六进制默认输出小写，`HX_OS_HEX_UPPERCASE` 选择大写。解码同时接受大小写，但不接受奇数字符、`0x` 前缀、空白或分隔符。

## 3. 缓冲区与所有权

四个缓冲区接口都先完整验证输入并计算所需长度。任何失败都保持输出缓冲区不变。输入合法时，在 `HX_OS_OK` 或 `HX_OS_OUTPUT_FULL` 下写出 `required`；输入不合法时保持该值不变。

- 编码的 `required` 包含末尾 NUL。
- 解码的 `required` 是二进制字节数，不包含终止符。
- 传 `output=NULL, capacity=0` 查询长度。编码空输入仍需要一个 NUL；解码空输入直接成功，长度为零。
- 输入、输出和 `required` 对象必须位于不同的存储区域，不提供原地转换。
- 缓冲区接口不分配内存。可以根据所需长度调用 `hx_os_memory_alloc`，解码后用 `hx_os_memory_free` 释放，也可以直接传入已有数组。
- 字符串便利接口使用输出字符串自己的 context，允许输入借用同一字符串，并在失败时保留原值。编码结果正文上限为 16 MiB；更大的结果使用调用者管理内存的缓冲区接口。

## 4. C++11 返回值

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

`Encoding::hex` 返回 `Result<String>`；`fromHex` 和 `fromBase64` 返回 `Result<Buffer>`。`Buffer` 可以保存任意字节并自动释放。C++ 拥有型结果的上限为 16 MiB，不使用异常，继续使用状态码与原生错误信息。先检查成功，再访问 `.value()`。
