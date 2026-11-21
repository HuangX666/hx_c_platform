# Incremental SHA and HMAC

[简体中文](INCREMENTAL_HASH.zh-CN.md) · [README](../README.md)

When computing a SHA-256 / SHA-512 digest or an HMAC authentication tag over a multi-gigabyte file, a streaming HTTP download, or network frames that arrive in chunks, buffering the entire payload in memory first is unnecessary. The `hx_os_crypto_digest_*` incremental hashing APIs let you feed arbitrary chunks via repeated `update` calls and extract the final lowercase hexadecimal digest with a single `finish` call.

Build and run the standalone [incremental_hash.c](../examples/incremental_hash.c) example to see chunked hashing in action (enable `HX_OS_BUILD_EXAMPLES=ON` per the [example run instructions](../examples/README.md#run) and build `hx_os_incremental_hash`; no arguments or input files are required):
- The example feeds `"ab"` followed by `"c"` and prints the SHA-256 digest of `"abc"`: `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`.
- This module requires `HX_OS_ENABLE_CRYPTO` and the OpenSSL dependency SDK (keep the corresponding SDK module enabled when linking an [installed package](USAGE.md#installed-project)).

## 1. Feed chunks, finish once

The C snippet below initializes a SHA-256 digest state, feeds `"a"` and `"bc"` across two updates, and prints the resulting hexadecimal digest:

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

Include `<hx_os.h>` and `<stdio.h>`. For keyed **HMAC** computation, create the state with `hx_os_crypto_hmac_create()` and use the exact same `update`, `finish`, `reset`, and `destroy` calls. Full source code is in [incremental_hash.c](../examples/incremental_hash.c); in C++11, include [CryptoDigest.hpp](../include/hx/CryptoDigest.hpp) for `hx::CryptoDigest::create`, `hmac`, `update`, `finish`, `reset`, and `close`.

## 2. State and ownership

- **Supported Algorithms & Chunk Limits**:
  - Supports `HX_OS_CRYPTO_SHA256` and `HX_OS_CRYPTO_SHA512`.
  - HMAC creation copies keys of up to `64 KiB` into internal state so the caller's key buffer can be wiped immediately after creation returns.
  - Each `hx_os_crypto_digest_update()` call borrows up to `16 MiB` (with total accumulated input bounded at `UINT64_MAX / 8` bytes). When hashing a file on disk, pass each chunk read from your loop or use the one-step `hx_os_crypto_sha256_file()` / `hx_os_crypto_hash_file()` helpers.
- **Sealing, Retrying `finish`, and `reset`**:
  - `hx_os_crypto_digest_finish()` writes lowercase hexadecimal output and **seals the digest state only after the output string assignment succeeds**—if output string allocation fails, you can safely retry `finish`.
  - Calling `finish` again on an already sealed state returns the same hex digest, whereas calling `update` on a sealed state returns `HX_OS_CLOSED`.
  - Calling `hx_os_crypto_digest_reset()` starts a fresh computation using the same algorithm and HMAC key (if a native `update` call fails, the state is poisoned until `reset` is called).
- **Thread Safety & infallible Cleanup Reservation**:
  - Serialize calls on a single `hx_os_crypto_digest_state` instance externally.
  - `create` preallocates the error-stack preservation storage needed by `destroy` (and the C++ `CryptoDigest` destructor), ensuring that **cleanup never fails to release the digest or its engine reference even under out-of-memory conditions**.
  - Do not destroy a digest state from inside an active crypto allocator or native OpenSSL callback (a rejected re-entrant C destroy preserves the owner pointer so you can retry once the callback returns). When `HX_OS_ENABLE_CRYPTO=OFF`, these functions return `HX_OS_NOT_SUPPORTED` (see the [Cryptography Lifetime Rules](CRYPTO.md)).
