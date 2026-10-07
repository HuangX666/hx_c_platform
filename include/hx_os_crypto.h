#ifndef HX_C_PLATFORM_CRYPTO_H
#define HX_C_PLATFORM_CRYPTO_H

#include "hx_os_params.h"
#include "hx_os_file.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_crypto hx_os_crypto;
typedef struct hx_os_crypto_bytes hx_os_crypto_bytes;
typedef struct hx_os_crypto_key hx_os_crypto_key;

enum { HX_OS_CRYPTO_SHA256 = 1, HX_OS_CRYPTO_SHA512 };

HX_OS_API int HX_OS_CALL hx_os_crypto_available(void);

/* Independent OpenSSL library context and built-in default provider. No
 * global allocator, properties, locale or configuration is replaced. NULL
 * crypto in operations creates a temporary context; retain one for repeated
 * work. Native OpenSSL allocations use its allocator, not hx_os_context.
 * Separate calls can share an engine; outputs remain externally serialized.
 * Keep the caller's reference alive through each call. Children retain theirs.
 * Allocator reentry into an active crypto operation on the same thread is
 * rejected with DEADLOCK. Never enter from native OpenSSL callbacks.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_create(
	hx_os_context *context, hx_os_crypto **out, hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_crypto_retain(hx_os_crypto *crypto);
HX_OS_API void HX_OS_CALL hx_os_crypto_release(hx_os_crypto *crypto);

/* Only call at a native thread's last OpenSSL use, outside all OpenSSL/library
 * callbacks, after native operations have returned. Clears ALL OpenSSL state
 * on that thread, including other native consumers. Required for Windows
 * static linking and for idle threads before unloading the OpenSSL runtime.
 * Dynamic Windows thread exit normally provides native cleanup automatically.
 * It does not cancel work, join threads or permit unloading live objects.
 */
HX_OS_API void HX_OS_CALL hx_os_crypto_thread_cleanup(void);

/* Immutable binary result, independent of engine lifetime. No terminator or
 * character encoding is implied. Release clears owned bytes before freeing.
 * Do not cast away const, free the borrowed data pointer or race release.
 */
HX_OS_API const void *HX_OS_CALL hx_os_crypto_bytes_data(
	const hx_os_crypto_bytes *bytes);
HX_OS_API size_t HX_OS_CALL hx_os_crypto_bytes_size(
	const hx_os_crypto_bytes *bytes);
HX_OS_API void HX_OS_CALL hx_os_crypto_bytes_destroy(
	hx_os_crypto_bytes **bytes);

/* Lowercase hexadecimal SHA-256/SHA-512. out is an initialized hx_string and
 * uses its own allocator. Failure preserves out; input is call-borrowed bytes.
 * The span type is reused without requiring the PARAMETERS module.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_hash(hx_os_crypto *crypto,
	unsigned algorithm, hx_os_bytes_view input, hx_string *out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_sha256(hx_os_crypto *crypto,
	hx_os_bytes_view input, hx_string *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_hmac(hx_os_crypto *crypto,
	unsigned algorithm, hx_os_bytes_view key, hx_os_bytes_view input,
	hx_string *out, hx_os_error *error);

/* Hex SHA256/SHA512 tag (either case), exact length, compared in constant
 * time after format validation. A mismatch returns AUTH_FAILED. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_hmac_verify(hx_os_crypto *crypto,
	unsigned algorithm, hx_os_bytes_view key, hx_os_bytes_view input,
	hx_string_view expected_hex, hx_os_error *error);

/* Regular UTF-8 file, bounded 32 KiB buffer, no hidden worker or hard deadline.
 * Reads the initial file length; premature EOF is TRUNCATED_INPUT. The caller
 * keeps input stable: this is not a filesystem snapshot or mutation detector.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_hash_file(hx_os_crypto *crypto,
	unsigned algorithm, const char *path, hx_string *out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_sha256_file(hx_os_crypto *crypto,
	const char *path, hx_string *out, hx_os_error *error);

/* 32 cryptographically random bytes. Failure preserves out. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_random_key(
	hx_os_crypto *crypto, unsigned char out[32], hx_os_error *error);

enum { HX_OS_CRYPTO_ED25519 = 1, HX_OS_CRYPTO_RSA_3072 = 2 };

/* Immutable keys retain their engine. Separate operations may share a key;
 * destruction cannot race a call. RSA generation can block for entropy/work.
 * Import accepts exactly one PEM key (64 KiB maximum), with whitespace only
 * outside the PEM block: unencrypted PKCS#8 private or SPKI public. Imported
 * RSA keys must be 2048..8192 bits; signatures use PSS/SHA256, MGF1/SHA256,
 * salt length 32. Ed25519 uses the message directly. No password prompt.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_key_generate(
	hx_os_crypto *crypto, unsigned algorithm, hx_os_crypto_key **out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_key_import_private(
	hx_os_crypto *crypto, hx_os_bytes_view pem, hx_os_crypto_key **out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_key_import_public(
	hx_os_crypto *crypto, hx_os_bytes_view pem, hx_os_crypto_key **out,
	hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_crypto_key_destroy(hx_os_crypto_key **key);
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_key_export_public(
	hx_os_crypto_key *key, hx_string *out, hx_os_error *error);

/* Sensitive UNENCRYPTED PKCS#8 PEM, owned and wiped on destroy. Encrypt this
 * buffer with encrypt() before persistence if password protection is needed.
 * Do not log it or keep an unprotected file. Public-only keys return
 * ACCESS_DENIED. The byte count excludes a terminator (none is promised).
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_key_export_private(
	hx_os_crypto_key *key, hx_os_crypto_bytes **out, hx_os_error *error);

/* Message <=16 MiB, signature <=1024 bytes. Failure sets *out=NULL; verify
 * returns AUTH_FAILED for an invalid signature, never an affirmative boolean
 * on failure. Public-only keys verify but cannot sign. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_sign(hx_os_crypto_key *key,
	hx_os_bytes_view message, hx_os_crypto_bytes **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_verify(hx_os_crypto_key *key,
	hx_os_bytes_view message, hx_os_bytes_view signature,
	hx_os_error *error);

enum {
	HX_OS_CRYPTO_KEY_256 = 1,
	HX_OS_CRYPTO_PASSWORD = 2,
	HX_OS_CRYPTO_REPLACE = 1
};

/* Borrowed secret. KEY_256 requires exactly 32 bytes; PASSWORD accepts 1..1024
 * exact bytes, without normalization. Text/file password shortcuts use UTF-8.
 */
typedef struct hx_os_crypto_secret {
	hx_os_bytes_view bytes;
	uint32_t kind;
} hx_os_crypto_secret;

typedef struct hx_os_crypto_options {
	uint32_t size, version;
	uint32_t iterations, max_iterations;
	uint64_t max_file_bytes;
	size_t max_memory_bytes;
	uint32_t flags;
} hx_os_crypto_options;

#define HX_OS_CRYPTO_OPTIONS_INIT                                              \
	{                                                                      \
		sizeof(hx_os_crypto_options), 1, 600000, 2000000,              \
			UINT64_C(4294967296), 16 * 1024 * 1024, 0              \
	}

HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_options_init(
	hx_os_crypto_options *options, size_t capacity);

/* AES-256-GCM HXCRYPT v1 envelope: authenticated header, random 96-bit nonce,
 * ciphertext, 128-bit tag. Password mode derives a key with PBKDF2-SHA256 and
 * random 128-bit salt. Never supply a reused nonce; it is generated internally.
 * Raw-key callers coordinate rotation across instances: fewer than 2^32
 * encryptions with one key. This stateless span API does not count key use.
 * Memory payload defaults to 16 MiB (configurable up to 64 MiB); metadata and
 * bounded temporary/output copies are additional. Password work is bounded
 * by max_iterations (100000..2000000). Errors set *out=NULL, clear any owned
 * plaintext and never publish unverified output. Wrong password/key or altered
 * authenticated contents is AUTH_FAILED; malformed framing has syntax/length
 * errors. Explicit output owner slots must not discard an existing owner.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_encrypt(hx_os_crypto *crypto,
	const hx_os_crypto_secret *secret, hx_os_bytes_view input,
	hx_os_crypto_bytes **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_decrypt(hx_os_crypto *crypto,
	const hx_os_crypto_secret *secret, hx_os_bytes_view input,
	hx_os_crypto_bytes **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_encrypt_with_options(
	hx_os_crypto *crypto, const hx_os_crypto_secret *secret,
	hx_os_bytes_view input, const hx_os_crypto_options *options,
	hx_os_crypto_bytes **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_decrypt_with_options(
	hx_os_crypto *crypto, const hx_os_crypto_secret *secret,
	hx_os_bytes_view input, const hx_os_crypto_options *options,
	hx_os_crypto_bytes **out, hx_os_error *error);

/* One-call UTF-8 text/password shortcuts. Ciphertext is canonical unwrapped
 * Base64 of the same envelope. No implicit plaintext encoding conversion.
 * Initialized output uses its context and stays unchanged on any failure.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_encrypt_text(
	const char *password, hx_string_view input, hx_string *out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_decrypt_text(
	const char *password, hx_string_view input, hx_string *out,
	hx_os_error *error);

/* Streaming regular files, requires FILE_UTILS. UTF-8 paths, 32 KiB chunks,
 * finite input-length limit (default 4 GiB, at most 64 GiB minus 32 bytes).
 * Same-directory temporary + full validation + sync + atomic publication;
 * default never overwrites, REPLACE is explicit. Input must remain stable.
 * Decryption stages with POSIX 0600 or a protected Windows owner-only DACL;
 * Windows filesystems lacking persistent ACLs are NOT_SUPPORTED for this.
 * Output keeps these private permissions. Authentication failure preserves
 * the old destination; cleanup removes the temporary where possible.
 * A cleanup failure may leave a private temporary. File I/O/KDF can block;
 * no background thread, interruptible KDF or hard deadline is promised.
 * Success means publication; result separately reports directory durability.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_encrypt_file(
	const char *input_path, const char *output_path, const char *password,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_decrypt_file(
	const char *input_path, const char *output_path, const char *password,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_encrypt_file_with_options(
	hx_os_crypto *crypto, const char *input_path, const char *output_path,
	const hx_os_crypto_secret *secret, const hx_os_crypto_options *options,
	hx_os_file_save_result *result, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_crypto_decrypt_file_with_options(
	hx_os_crypto *crypto, const char *input_path, const char *output_path,
	const hx_os_crypto_secret *secret, const hx_os_crypto_options *options,
	hx_os_file_save_result *result, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
