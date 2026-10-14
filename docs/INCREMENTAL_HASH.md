# Incremental SHA and HMAC

[简体中文](INCREMENTAL_HASH.zh-CN.md) · [README](../README.md)

## 1. Feed chunks, finish once

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

Include hx_os.h and stdio.h. This computes SHA-256 of abc. Complete runnable source: [incremental_hash.c](../examples/incremental_hash.c). C++11 provides CryptoDigest::create, hmac, update, finish, reset and close in [CryptoDigest.hpp](../include/hx/CryptoDigest.hpp).

## 2. State and ownership

SHA-256 and SHA-512 are supported. HMAC creation copies a key of at most 64 KiB into native state. Updates borrow at most 16 MiB per call; accumulated bytes are capped at UINT64_MAX/8. Feed large files in bounded chunks or use the existing hash_file shortcut.

The object retains its engine. Externally serialize operations and destruction. finish computes lowercase hexadecimal, seals only after successful output assignment, and can be retried after an output allocation failure. Repeated finish returns the same result; updating a sealed state returns CLOSED. reset starts a new computation with the same algorithm/key. A native update failure poisons that computation until reset.

Objects and native providers follow the existing [crypto lifetime](CRYPTO.md) rules. Native OpenSSL storage is outside the injected allocator's accounting. Direct EVP calls through [hx_c_platform::openssl](NATIVE_DEPENDENCIES.md) remain available. CRYPTO=OFF returns NOT_SUPPORTED for these operations.
