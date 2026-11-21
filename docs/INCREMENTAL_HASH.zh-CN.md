# 分块 SHA 摘要与 HMAC

[English](INCREMENTAL_HASH.md) · [README](../README.zh-CN.md)

当你需要对超大文件、HTTP 流式下载内容或分批到达的网络数据块计算 SHA-256 / SHA-512 摘要或 HMAC 消息认证码时，如果先把所有数据拼接到一整块内存中再计算，会消耗大量内存。使用 `hx_os_crypto_digest_*` 增量哈希接口，你可以按任意分块大小反复调用 `update` 喂入数据，最后调用一次 `finish` 取出小写十六进制结果。

你可以直接编译并运行公开示例 [incremental_hash.c](../examples/incremental_hash.c) 来验证分块摘要计算（按[示例构建说明](../examples/README.zh-CN.md#run)启用 `HX_OS_BUILD_EXAMPLES=ON` 并构建 `hx_os_incremental_hash` 即可，无需命令行参数或外部文件）：
- 示例分两次喂入 `"ab"` 和 `"c"`，最终输出 `"abc"` 的标准 SHA-256 值：`ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`。
- 该模块依赖 `HX_OS_ENABLE_CRYPTO` 与 OpenSSL SDK；通过[已安装包接入工程](USAGE.zh-CN.md#installed-project)时请同样保留对应 SDK 模块。

## 1. 分块输入，最后取结果

下面的 C 代码创建一个 SHA-256 增量状态机，分两次喂入 `"a"` 与 `"bc"`，最后输出完整的十六进制摘要：

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

使用时包含 `<hx_os.h>` 与 `<stdio.h>`。如果你需要计算分块 **HMAC**，只需将创建函数换为带密钥的 `hx_os_crypto_hmac_create()`，后续的 `update`、`finish`、`reset` 与 `destroy` 完全相同。完整可运行源码见 [incremental_hash.c](../examples/incremental_hash.c)；在 C++11 中，包含 [CryptoDigest.hpp](../include/hx/CryptoDigest.hpp) 即可使用提供 `create`、`hmac`、`update`、`finish`、`reset` 和 `close` 的 `hx::CryptoDigest` 包装类。

## 2. 状态与资源归属

- **支持的算法与单次输入上限**：
  - 支持 `HX_OS_CRYPTO_SHA256` 与 `HX_OS_CRYPTO_SHA512`。
  - 创建 HMAC 状态机时，库会将传入的密钥（上限 `64 KiB`）拷贝到内部原生状态中，函数返回后调用方即可清零或释放原密钥缓冲区。
  - 每次调用 `hx_os_crypto_digest_update()` 最多借用 `16 MiB` 数据块（累计输入总上限为 `UINT64_MAX / 8` 字节）。读取本地大文件时，可在循环中每次传入实际读到的字节数，或者直接调用 `CRYPTO` 模块现成的 `hx_os_crypto_sha256_file()` / `hx_os_crypto_hash_file()` 快捷函数。
- **封存（Sealing）、重试与重置（`reset`）规则**：
  - `hx_os_crypto_digest_finish()` 输出小写十六进制字符串，并且**只有在输出字符串内存分配并赋值成功后才会封存（Seal）内部状态**——如果因输出字符串扩容失败而报错，你可以修复内存后安全重试 `finish`。
  - 状态封存后，重复调用 `finish` 会直接返回相同的摘要结果；而对已封存的状态再次调用 `update` 则会返回 `HX_OS_CLOSED`。
  - 调用 `hx_os_crypto_digest_reset()` 可以保留已配置的算法与 HMAC 密钥，立即开始新一轮摘要计算（如果某次底层 `update` 调用报错，当前计算状态会被标记为失效，必须先调用 `reset` 才能继续使用）。
- **并发与销毁安全保证**：
  - 同一个 `hx_os_crypto_digest_state` 实例上的操作与销毁需由调用方在外部串行化。
  - 在 `create` 阶段，库已经预先分配好了 `destroy`（以及 C++ `CryptoDigest` 析构函数）保存和恢复线程 OpenSSL 错误栈所需的内存空间，因此**即使在内存耗尽的极端情况下，销毁摘要对象及其持有的 Engine 引用也绝不会因为分配失败而泄漏**。
  - 销毁操作必须在活跃的加密分配器回调或 OpenSSL 原生回调之外执行（若在回调内发生非法的同线程重入销毁，C 销毁函数会拒绝操作并保留原指针供退出回调后重试）。当构建时关闭了 `HX_OS_ENABLE_CRYPTO` 时，所有增量摘要入口统一返回 `HX_OS_NOT_SUPPORTED`（遵循[密码学模块生命周期契约](CRYPTO.zh-CN.md)）。
