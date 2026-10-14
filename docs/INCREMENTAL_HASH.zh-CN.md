# 分块 SHA 摘要与 HMAC

[English](INCREMENTAL_HASH.md) · [README](../README.zh-CN.md)

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
