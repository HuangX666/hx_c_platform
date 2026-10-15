# 分块 SHA 摘要与 HMAC

[English](INCREMENTAL_HASH.md) · [README](../README.zh-CN.md)

**先运行一个完整例子。** 大文件或网络内容是一块一块到达时，可以逐块计算摘要，避免先把全部内容放进内存。

打开 [incremental_hash.c](../examples/incremental_hash.c)。digest_create 选择 SHA-256 → update 输入 ab → update 输入 c → finish 取得十六进制结果 → destroy。HMAC 使用带密钥的创建入口。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 examples，并只构建 `hx_os_incremental_hash`。此例无命令行参数、无需输入文件。

**会得到什么：**得到 abc 的 SHA-256：`ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`。换成文件读取循环时，每次只传实际读到的字节数；需要 CRYPTO 和 OpenSSL SDK。

下面的短片段用于解释接口；复制运行时以完整示例为准。使用[安装包接入](USAGE.zh-CN.md#installed-project)时，也必须保留对应 SDK 模块。

## 1. 分块输入，最后取结果

```c
hx_os_crypto_digest_state *digest = NULL;
hx_string hex = HX_STRING_INIT;

hx_os_status status = hx_os_crypto_digest_create(
	NULL, HX_OS_CRYPTO_SHA256, &digest, NULL);
if (status == HX_OS_OK)
	status = hx_os_crypto_digest_update(
		digest, (hx_os_bytes_view){"a", 1}, NULL);
if (status == HX_OS_OK)
	status = hx_os_crypto_digest_update(
		digest, (hx_os_bytes_view){"bc", 2}, NULL);
if (status == HX_OS_OK)
	status = hx_os_crypto_digest_finish(digest, &hex, NULL);
if (status == HX_OS_OK)
	puts(hx_string_c_str(&hex));

hx_os_crypto_digest_destroy(&digest, NULL);
hx_string_destroy(&hex);
```

包含 hx_os.h、stdio.h。这个例子计算 abc 的 SHA-256。完整程序：[incremental_hash.c](../examples/incremental_hash.c)。C++11 的 [CryptoDigest.hpp](../include/hx/CryptoDigest.hpp) 提供 create、hmac、update、finish、reset、close。

## 2. 状态与资源归属

支持 SHA-256、SHA-512。HMAC 创建时把最多 64 KiB 的密钥复制到原生状态。每次 update 借用最多 16 MiB，累计输入上限为 UINT64_MAX/8。大文件按块输入，或直接使用现有 hash_file。

对象持有 engine，操作与销毁由调用者串行化。finish 输出小写十六进制，仅在输出赋值成功后封存状态；输出分配失败可以重试。重复 finish 返回同一结果，封存后 update 返回 CLOSED。reset 保留算法和密钥，开始新一轮计算。原生 update 失败会让当前计算失效，reset 后才能继续。

遵守现有[加密生命周期](CRYPTO.zh-CN.md)约定。OpenSSL 自身存储不计入本库分配器；仍可通过 [hx_c_platform::openssl](NATIVE_DEPENDENCIES.zh-CN.md) 直接调用 EVP。关闭 CRYPTO 后，这些操作明确返回 NOT_SUPPORTED。
