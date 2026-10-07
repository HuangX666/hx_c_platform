#include "crypto_private.h"

#if HX_OS_ENABLE_CRYPTO
#include <openssl/core_names.h>
#include <openssl/params.h>

static const char *hx_os_crypto_digest_name(unsigned algorithm)
{
	switch (algorithm) {
	case HX_OS_CRYPTO_SHA256:
		return "SHA256";
	case HX_OS_CRYPTO_SHA512:
		return "SHA512";
	default:
		return NULL;
	}
}

static hx_os_status hx_os_crypto_hex(const unsigned char *data, size_t bytes,
	hx_string *out, hx_os_error *error)
{
	static const char digits[] = "0123456789abcdef";
	char text[129];
	size_t i;

	if (bytes > 64)
		return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
	for (i = 0; i < bytes; ++i) {
		text[2 * i] = digits[data[i] >> 4];
		text[2 * i + 1] = digits[data[i] & 15];
	}
	text[2 * bytes] = 0;

	return hx_string_assign_cstr(out, text, error);
}

static hx_os_status hx_os_crypto_feed_file(hx_os_crypto *crypto,
	EVP_MD_CTX *digest, const char *path, hx_os_error *error)
{
	hx_os_file *file = NULL;
	unsigned char *buffer = NULL;
	uint64_t remaining = 0;
	size_t completed, request;
	hx_os_status status = hx_os_file_open(
		crypto->context, path, HX_OS_FILE_READ, &file, error);

	if (status == HX_OS_OK)
		status = hx_os_file_size(file, &remaining, error);
	if (status == HX_OS_OK) {
		buffer = hx_os_i_alloc(crypto->context, 32768, 0);
		if (!buffer)
			status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	while (status == HX_OS_OK && remaining) {
		request = remaining > 32768 ? 32768 : (size_t)remaining;
		status = hx_os_file_read_some(
			file, buffer, request, &completed, error);
		if (status == HX_OS_EOF)
			status = hx_os_i_error(
				error, HX_OS_TRUNCATED_INPUT, 0, 0);
		if (status == HX_OS_OK && !completed)
			status = hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		if (status == HX_OS_OK) {
			remaining -= completed;
			if (EVP_DigestUpdate(digest, buffer, completed) != 1)
				status = hx_os_i_crypto_fail(
					error, HX_OS_IO_ERROR);
		}
	}
	if (buffer) {
		hx_os_secure_zero(buffer, 32768);
		hx_os_memory_free(buffer);
	}
	if (file) {
		hx_os_status closed = hx_os_file_close(
			&file, status == HX_OS_OK ? error : NULL);

		if (status == HX_OS_OK)
			status = closed;
	}

	return status;
}

static hx_os_status hx_os_crypto_digest(hx_os_crypto *crypto,
	unsigned algorithm, hx_os_bytes_view input, const char *path,
	hx_string *out, hx_os_error *error)
{
	hx_os_crypto_scope scope;
	EVP_MD *method;
	EVP_MD_CTX *digest;
	unsigned char result[64];
	unsigned bytes = 0;
	const char *name = hx_os_crypto_digest_name(algorithm);
	hx_os_status status;

	if (!out || !name || (!input.data && input.size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_crypto_enter(crypto, out->context, &scope, error);
	if (status != HX_OS_OK)
		return status;
	method = EVP_MD_fetch(scope.crypto->native, name, "provider=default");
	digest = EVP_MD_CTX_new();
	if (!method || !digest || EVP_DigestInit_ex2(digest, method, NULL) != 1)
		status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
	if (status == HX_OS_OK) {
		if (path)
			status = hx_os_crypto_feed_file(
				scope.crypto, digest, path, error);
		else if (input.size &&
			EVP_DigestUpdate(digest, input.data, input.size) != 1)
			status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
	}
	if (status == HX_OS_OK &&
		EVP_DigestFinal_ex(digest, result, &bytes) != 1)
		status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
	if (status == HX_OS_OK)
		status = hx_os_crypto_hex(result, bytes, out, error);
	hx_os_secure_zero(result, sizeof(result));
	EVP_MD_CTX_free(digest);
	EVP_MD_free(method);
	hx_os_i_crypto_leave(&scope);

	return status;
}
#endif

hx_os_status hx_os_crypto_hash(hx_os_crypto *crypto, unsigned algorithm,
	hx_os_bytes_view input, hx_string *out, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	return hx_os_crypto_digest(crypto, algorithm, input, NULL, out, error);
#else
	(void)crypto;
	(void)algorithm;
	(void)input;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_sha256(hx_os_crypto *crypto, hx_os_bytes_view input,
	hx_string *out, hx_os_error *error)
{
	return hx_os_crypto_hash(
		crypto, HX_OS_CRYPTO_SHA256, input, out, error);
}

hx_os_status hx_os_crypto_hash_file(hx_os_crypto *crypto, unsigned algorithm,
	const char *path, hx_string *out, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	const hx_os_bytes_view empty = {NULL, 0};

	if (!path)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_crypto_digest(crypto, algorithm, empty, path, out, error);
#else
	(void)crypto;
	(void)algorithm;
	(void)path;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_sha256_file(hx_os_crypto *crypto, const char *path,
	hx_string *out, hx_os_error *error)
{
	return hx_os_crypto_hash_file(
		crypto, HX_OS_CRYPTO_SHA256, path, out, error);
}

hx_os_status hx_os_crypto_hmac(hx_os_crypto *crypto, unsigned algorithm,
	hx_os_bytes_view key, hx_os_bytes_view input, hx_string *out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	hx_os_crypto_scope scope;
	EVP_MAC *method;
	EVP_MAC_CTX *mac;
	OSSL_PARAM parameters[2];
	unsigned char result[64];
	size_t bytes = 0;
	const char *name = hx_os_crypto_digest_name(algorithm);
	hx_os_status status;

	if (!name || !out || (!key.data && key.size) ||
		(!input.data && input.size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_crypto_enter(crypto, out->context, &scope, error);
	if (status != HX_OS_OK)
		return status;
	parameters[0] = OSSL_PARAM_construct_utf8_string(
		OSSL_MAC_PARAM_DIGEST, (char *)name, 0);
	parameters[1] = OSSL_PARAM_construct_end();
	method =
		EVP_MAC_fetch(scope.crypto->native, "HMAC", "provider=default");
	mac = method ? EVP_MAC_CTX_new(method) : NULL;
	if (!mac ||
		EVP_MAC_init(mac, key.data ? key.data : "", key.size,
			parameters) != 1 ||
		(input.size &&
			EVP_MAC_update(mac, input.data, input.size) != 1) ||
		EVP_MAC_final(mac, result, &bytes, sizeof(result)) != 1)
		status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
	if (status == HX_OS_OK)
		status = hx_os_crypto_hex(result, bytes, out, error);
	hx_os_secure_zero(result, sizeof(result));
	EVP_MAC_CTX_free(mac);
	EVP_MAC_free(method);
	hx_os_i_crypto_leave(&scope);

	return status;
#else
	(void)crypto;
	(void)algorithm;
	(void)key;
	(void)input;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_hmac_verify(hx_os_crypto *crypto, unsigned algorithm,
	hx_os_bytes_view key, hx_os_bytes_view input,
	hx_string_view expected_hex, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	hx_string calculated = HX_STRING_INIT;
	char expected[128];
	size_t i, bytes;
	unsigned char value;
	hx_os_status status;

	if (!expected_hex.data || !hx_os_crypto_digest_name(algorithm))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	bytes = algorithm == HX_OS_CRYPTO_SHA256 ? 64 : 128;
	if (expected_hex.size != bytes)
		return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
	for (i = 0; i < bytes; ++i) {
		value = (unsigned char)expected_hex.data[i];
		if (value >= 'A' && value <= 'F')
			value = (unsigned char)(value + ('a' - 'A'));
		if (!(value >= '0' && value <= '9') &&
			!(value >= 'a' && value <= 'f'))
			return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
		expected[i] = (char)value;
	}
	hx_string_init(&calculated, crypto ? crypto->context : NULL);
	status = hx_os_crypto_hmac(
		crypto, algorithm, key, input, &calculated, error);
	if (status == HX_OS_OK &&
		CRYPTO_memcmp(hx_string_c_str(&calculated), expected, bytes))
		status = hx_os_i_error(error, HX_OS_AUTH_FAILED, 0, 0);
	hx_string_destroy(&calculated);

	return status;
#else
	(void)crypto;
	(void)algorithm;
	(void)key;
	(void)input;
	(void)expected_hex;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_random_key(
	hx_os_crypto *crypto, unsigned char out[32], hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	hx_os_crypto_scope scope;
	unsigned char key[32];
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_crypto_enter(crypto, NULL, &scope, error);
	if (status != HX_OS_OK)
		return status;
	if (RAND_priv_bytes_ex(scope.crypto->native, key, sizeof(key), 256) !=
		1)
		status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
	if (status == HX_OS_OK) {
		memcpy(out, key, sizeof(key));
		hx_os_error_clear(error);
	}
	hx_os_secure_zero(key, sizeof(key));
	hx_os_i_crypto_leave(&scope);

	return status;
#else
	(void)crypto;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
