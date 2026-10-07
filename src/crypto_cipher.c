#include "crypto_private.h"

hx_os_status hx_os_crypto_options_init(
	hx_os_crypto_options *options, size_t capacity)
{
	const hx_os_crypto_options defaults = HX_OS_CRYPTO_OPTIONS_INIT;

	if (!options)
		return HX_OS_INVALID;
	if (capacity < sizeof(*options))
		return HX_OS_OUTPUT_FULL;
	*options = defaults;

	return HX_OS_OK;
}

hx_os_status hx_os_i_crypto_options(const hx_os_crypto_options *input,
	hx_os_crypto_options *out, hx_os_error *error)
{
	const hx_os_crypto_options defaults = HX_OS_CRYPTO_OPTIONS_INIT;

	if (!input)
		input = &defaults;
	if (input->size < sizeof(*input) || input->version != 1 ||
		input->iterations < 100000 || input->iterations > 2000000 ||
		input->max_iterations < input->iterations ||
		input->max_iterations > 2000000 || !input->max_memory_bytes ||
		input->max_memory_bytes > 64 * 1024 * 1024 ||
		!input->max_file_bytes ||
		input->max_file_bytes > UINT64_C(68719476704) ||
		(input->flags & ~(uint32_t)HX_OS_CRYPTO_REPLACE))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = *input;

	return HX_OS_OK;
}

hx_os_status hx_os_i_crypto_password(
	const char *password, hx_os_crypto_secret *secret, hx_os_error *error)
{
	size_t bytes = 0, invalid;
	hx_os_status status;

	if (!password)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	while (bytes <= 1024 && password[bytes])
		++bytes;
	if (!bytes || bytes > 1024)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_text_validate_utf8(password, bytes, &invalid);
	if (status == HX_OS_OK) {
		secret->bytes.data = password;
		secret->bytes.size = bytes;
		secret->kind = HX_OS_CRYPTO_PASSWORD;
	}

	return hx_os_i_error(error, status, 0, 0);
}

#if HX_OS_ENABLE_CRYPTO
#include <openssl/core_names.h>
#include <openssl/kdf.h>

static hx_os_status hx_os_crypto_derive(hx_os_crypto *crypto,
	const hx_os_crypto_secret *secret, unsigned char salt[16],
	uint32_t rounds, unsigned char key[32], hx_os_error *error)
{
	EVP_KDF *method;
	EVP_KDF_CTX *kdf;
	uint64_t iterations = rounds;
	int pkcs5 = 1;
	char digest[] = "SHA256", properties[] = "provider=default";
	OSSL_PARAM params[7];
	hx_os_status status = HX_OS_OK;

	params[0] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD,
		(void *)secret->bytes.data, secret->bytes.size);
	params[1] = OSSL_PARAM_construct_octet_string(
		OSSL_KDF_PARAM_SALT, salt, 16);
	params[2] =
		OSSL_PARAM_construct_uint64(OSSL_KDF_PARAM_ITER, &iterations);
	params[3] = OSSL_PARAM_construct_utf8_string(
		OSSL_KDF_PARAM_DIGEST, digest, 0);
	params[4] = OSSL_PARAM_construct_utf8_string(
		OSSL_KDF_PARAM_PROPERTIES, properties, 0);
	params[5] = OSSL_PARAM_construct_int(OSSL_KDF_PARAM_PKCS5, &pkcs5);
	params[6] = OSSL_PARAM_construct_end();
	method = EVP_KDF_fetch(crypto->native, "PBKDF2", properties);
	kdf = method ? EVP_KDF_CTX_new(method) : NULL;
	if (!kdf || EVP_KDF_derive(kdf, key, 32, params) != 1)
		status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
	EVP_KDF_CTX_free(kdf);
	EVP_KDF_free(method);

	return status;
}

static hx_os_status hx_os_crypto_header(hx_os_crypto *crypto,
	const hx_os_crypto_secret *secret, const hx_os_crypto_options *options,
	int decrypt, unsigned char *header, uint32_t *rounds,
	hx_os_error *error)
{
	static const unsigned char magic[8] = {
		'H', 'X', 'C', 'R', 'Y', 'P', 'T', 1};
	uint32_t count = 0;
	unsigned i;

	if (!secret || !secret->bytes.data ||
		(secret->kind != HX_OS_CRYPTO_KEY_256 &&
			secret->kind != HX_OS_CRYPTO_PASSWORD) ||
		(secret->kind == HX_OS_CRYPTO_KEY_256 &&
			secret->bytes.size != 32) ||
		(secret->kind == HX_OS_CRYPTO_PASSWORD &&
			(!secret->bytes.size || secret->bytes.size > 1024)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (decrypt) {
		if (memcmp(header, magic, sizeof(magic)) || header[8] != 1 ||
			header[10] || header[11] ||
			(header[9] != 1 && header[9] != 2))
			return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
		if (header[9] != secret->kind)
			return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
		for (i = 12; i < 16; ++i)
			count = (count << 8) | header[i];
		if (secret->kind == HX_OS_CRYPTO_PASSWORD) {
			if (count < 100000 || count > options->max_iterations)
				return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		}
		else {
			if (count)
				return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
			for (i = 16; i < 32; ++i)
				if (header[i])
					return hx_os_i_error(
						error, HX_OS_SYNTAX, 0, 0);
		}
	}
	else {
		memset(header, 0, HX_OS_I_CRYPTO_HEADER);
		memcpy(header, magic, sizeof(magic));
		header[8] = 1;
		header[9] = (unsigned char)secret->kind;
		if (secret->kind == HX_OS_CRYPTO_PASSWORD) {
			count = options->iterations;
			if (RAND_bytes_ex(
				    crypto->native, header + 16, 16, 128) != 1)
				return hx_os_i_crypto_fail(
					error, HX_OS_IO_ERROR);
		}
		for (i = 0; i < 4; ++i)
			header[12 + i] = (unsigned char)(count >> (24 - 8 * i));
		if (RAND_bytes_ex(crypto->native, header + 32, 12, 128) != 1)
			return hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
	}
	*rounds = count;

	return HX_OS_OK;
}

hx_os_status hx_os_i_crypto_cipher(hx_os_crypto *crypto,
	const hx_os_crypto_secret *secret, const hx_os_crypto_options *options,
	int decrypt, unsigned char header[HX_OS_I_CRYPTO_HEADER],
	EVP_CIPHER_CTX **out, hx_os_error *error)
{
	unsigned char key[32] = {0};
	uint32_t rounds;
	EVP_CIPHER *method = NULL;
	EVP_CIPHER_CTX *cipher = NULL;
	int count;
	hx_os_status status = hx_os_crypto_header(
		crypto, secret, options, decrypt, header, &rounds, error);

	*out = NULL;
	if (status != HX_OS_OK)
		return status;
	if (secret->kind == HX_OS_CRYPTO_KEY_256)
		memcpy(key, secret->bytes.data, sizeof(key));
	else
		status = hx_os_crypto_derive(
			crypto, secret, header + 16, rounds, key, error);
	if (status == HX_OS_OK) {
		method = EVP_CIPHER_fetch(
			crypto->native, "AES-256-GCM", "provider=default");
		cipher = EVP_CIPHER_CTX_new();
		if (!method || !cipher ||
			EVP_CipherInit_ex2(cipher, method, key, header + 32,
				!decrypt, NULL) != 1 ||
			EVP_CipherUpdate(cipher, NULL, &count, header,
				HX_OS_I_CRYPTO_HEADER) != 1)
			status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
	}
	hx_os_secure_zero(key, sizeof(key));
	EVP_CIPHER_free(method);
	if (status == HX_OS_OK)
		*out = cipher;
	else
		EVP_CIPHER_CTX_free(cipher);

	return status;
}

static hx_os_status hx_os_crypto_transform(hx_os_crypto *crypto,
	const hx_os_crypto_secret *secret, hx_os_bytes_view input,
	const hx_os_crypto_options *settings, int decrypt,
	hx_os_crypto_bytes **out, hx_os_error *error)
{
	hx_os_crypto_scope scope;
	hx_os_crypto_options options;
	hx_os_crypto_bytes *result = NULL;
	EVP_CIPHER_CTX *cipher = NULL;
	unsigned char header[HX_OS_I_CRYPTO_HEADER], *output;
	unsigned char final[EVP_MAX_BLOCK_LENGTH] = {0};
	const unsigned char *data = input.data;
	size_t bytes;
	int count = 0, tail = 0;
	hx_os_error local;
	hx_os_status status;

	if (!error)
		error = &local;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!data && input.size)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_crypto_options(settings, &options, error);
	if (status != HX_OS_OK)
		return status;
	if (decrypt && input.size < HX_OS_I_CRYPTO_OVERHEAD)
		return hx_os_i_error(error, HX_OS_TRUNCATED_INPUT, 0, 0);
	bytes = input.size - (decrypt ? HX_OS_I_CRYPTO_OVERHEAD : 0);
	if (bytes > options.max_memory_bytes)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if (decrypt)
		memcpy(header, data, sizeof(header));
	status = hx_os_i_crypto_enter(crypto, NULL, &scope, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_i_crypto_cipher(scope.crypto, secret, &options, decrypt,
		header, &cipher, error);
	if (status == HX_OS_OK) {
		result = hx_os_i_crypto_bytes(scope.crypto->context,
			bytes + (decrypt ? 0 : HX_OS_I_CRYPTO_OVERHEAD), error);
		if (!result)
			status = error->status;
	}
	if (status == HX_OS_OK) {
		output = result->data + (decrypt ? 0 : sizeof(header));
		if (!decrypt)
			memcpy(result->data, header, sizeof(header));
		if (bytes &&
			EVP_CipherUpdate(cipher, output, &count,
				data + (decrypt ? sizeof(header) : 0),
				(int)bytes) != 1)
			status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
		if (status == HX_OS_OK && (size_t)count != bytes)
			status = hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		if (status == HX_OS_OK && decrypt &&
			EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_AEAD_SET_TAG,
				HX_OS_I_CRYPTO_TAG,
				(void *)(data + sizeof(header) + bytes)) != 1)
			status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
		if (status == HX_OS_OK &&
			EVP_CipherFinal_ex(cipher, final, &tail) != 1)
			status = hx_os_i_crypto_fail(error,
				decrypt ? HX_OS_AUTH_FAILED : HX_OS_IO_ERROR);
		if (status == HX_OS_OK && tail)
			status = hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		if (status == HX_OS_OK && !decrypt &&
			EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_AEAD_GET_TAG,
				HX_OS_I_CRYPTO_TAG, output + bytes) != 1)
			status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
	}
	EVP_CIPHER_CTX_free(cipher);
	hx_os_secure_zero(final, sizeof(final));
	if (status == HX_OS_OK) {
		*out = result;
		hx_os_error_clear(error);
	}
	else
		hx_os_crypto_bytes_destroy(&result);
	hx_os_i_crypto_leave(&scope);

	return status;
}
#endif

hx_os_status hx_os_crypto_encrypt_with_options(hx_os_crypto *crypto,
	const hx_os_crypto_secret *secret, hx_os_bytes_view input,
	const hx_os_crypto_options *options, hx_os_crypto_bytes **out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	return hx_os_crypto_transform(
		crypto, secret, input, options, 0, out, error);
#else
	(void)crypto;
	(void)secret;
	(void)input;
	(void)options;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_decrypt_with_options(hx_os_crypto *crypto,
	const hx_os_crypto_secret *secret, hx_os_bytes_view input,
	const hx_os_crypto_options *options, hx_os_crypto_bytes **out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	return hx_os_crypto_transform(
		crypto, secret, input, options, 1, out, error);
#else
	(void)crypto;
	(void)secret;
	(void)input;
	(void)options;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_encrypt(hx_os_crypto *crypto,
	const hx_os_crypto_secret *secret, hx_os_bytes_view input,
	hx_os_crypto_bytes **out, hx_os_error *error)
{
	return hx_os_crypto_encrypt_with_options(
		crypto, secret, input, NULL, out, error);
}

hx_os_status hx_os_crypto_decrypt(hx_os_crypto *crypto,
	const hx_os_crypto_secret *secret, hx_os_bytes_view input,
	hx_os_crypto_bytes **out, hx_os_error *error)
{
	return hx_os_crypto_decrypt_with_options(
		crypto, secret, input, NULL, out, error);
}
