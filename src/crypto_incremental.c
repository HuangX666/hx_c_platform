#include "crypto_private.h"

#if HX_OS_ENABLE_CRYPTO
#include <openssl/core_names.h>
#include <openssl/params.h>

struct hx_os_crypto_digest_state {
	hx_os_crypto *crypto;
	EVP_MD_CTX *digest, *digest_start;
	EVP_MAC_CTX *mac, *mac_start;
	ERR_STATE *cleanup_errors;
	uint64_t total;
	unsigned char result[64];
	size_t bytes;
	int sealed, poisoned;
};

static hx_os_status hx_os_digest_new(hx_os_crypto *crypto, unsigned algorithm,
	const hx_os_bytes_view *key, hx_os_crypto_digest_state **out,
	hx_os_error *error)
{
	hx_os_crypto_scope scope;
	hx_os_crypto_digest_state *state;
	const char *name = algorithm == HX_OS_CRYPTO_SHA256 ? "SHA256"
		: algorithm == HX_OS_CRYPTO_SHA512	    ? "SHA512"
							    : NULL;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!name || (key && ((!key->data && key->size) || key->size > 65536)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_crypto_enter(crypto, NULL, &scope, error);
	if (status != HX_OS_OK)
		return status;
	state = hx_os_i_zero(scope.crypto->context, sizeof(*state));
	if (!state) {
		hx_os_i_crypto_leave(&scope);
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	state->crypto = scope.crypto;
	hx_os_crypto_retain(state->crypto);
	if (key) {
		EVP_MAC *method = EVP_MAC_fetch(
			scope.crypto->native, "HMAC", "provider=default");
		OSSL_PARAM parameters[2];

		parameters[0] = OSSL_PARAM_construct_utf8_string(
			OSSL_MAC_PARAM_DIGEST, (char *)name, 0);
		parameters[1] = OSSL_PARAM_construct_end();
		state->mac_start = method ? EVP_MAC_CTX_new(method) : NULL;
		EVP_MAC_free(method);
		if (state->mac_start &&
			EVP_MAC_init(state->mac_start,
				key->data ? key->data : "", key->size,
				parameters) == 1)
			state->mac = EVP_MAC_CTX_dup(state->mac_start);
		if (!state->mac)
			status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
	}
	else {
		EVP_MD *method = EVP_MD_fetch(
			scope.crypto->native, name, "provider=default");

		state->digest_start = EVP_MD_CTX_new();
		state->digest = EVP_MD_CTX_new();
		if (!method || !state->digest || !state->digest_start ||
			EVP_DigestInit_ex2(state->digest_start, method, NULL) !=
				1 ||
			EVP_MD_CTX_copy_ex(
				state->digest, state->digest_start) != 1)
			status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
		EVP_MD_free(method);
	}
	if (status == HX_OS_OK) {
		// Reserve error storage so allocation cannot block cleanup.
		state->cleanup_errors = OSSL_ERR_STATE_new();
		if (!state->cleanup_errors)
			status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	if (status != HX_OS_OK) {
		EVP_MD_CTX_free(state->digest);
		EVP_MD_CTX_free(state->digest_start);
		EVP_MAC_CTX_free(state->mac);
		EVP_MAC_CTX_free(state->mac_start);
		hx_os_crypto_release(state->crypto);
		hx_os_memory_free(state);
	}
	else
		*out = state;
	hx_os_i_crypto_leave(&scope);

	return status == HX_OS_OK ? hx_os_i_error(error, status, 0, 0) : status;
}
#endif

hx_os_status hx_os_crypto_digest_create(hx_os_crypto *crypto,
	unsigned algorithm, hx_os_crypto_digest_state **out, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	return hx_os_digest_new(crypto, algorithm, NULL, out, error);
#else
	(void)crypto;
	(void)algorithm;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_hmac_create(hx_os_crypto *crypto, unsigned algorithm,
	hx_os_bytes_view key, hx_os_crypto_digest_state **out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	return hx_os_digest_new(crypto, algorithm, &key, out, error);
#else
	(void)crypto;
	(void)algorithm;
	(void)key;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_digest_update(hx_os_crypto_digest_state *state,
	hx_os_bytes_view input, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	hx_os_crypto_scope scope;
	hx_os_status status;
	int updated = 1;

	if (!state || (!input.data && input.size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (state->sealed || state->poisoned)
		return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);
	if (input.size > 16 * 1024 * 1024 ||
		input.size > UINT64_MAX / 8 - state->total)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	status = hx_os_i_crypto_enter(state->crypto, NULL, &scope, error);
	if (status != HX_OS_OK)
		return status;
	if (input.size)
		updated = state->mac
			? EVP_MAC_update(state->mac, input.data, input.size)
			: EVP_DigestUpdate(
				  state->digest, input.data, input.size);
	if (updated != 1) {
		state->poisoned = 1;
		status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
	}
	else
		state->total += input.size;
	hx_os_i_crypto_leave(&scope);

	return status == HX_OS_OK ? hx_os_i_error(error, status, 0, 0) : status;
#else
	(void)state;
	(void)input;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_digest_finish(
	hx_os_crypto_digest_state *state, hx_string *out, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	static const char hex[] = "0123456789abcdef";
	hx_os_crypto_scope scope;
	unsigned char digest[64];
	char text[129];
	size_t bytes = 0, i;
	hx_os_status status;

	if (!state || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (state->poisoned)
		return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);
	status = hx_os_i_crypto_enter(state->crypto, NULL, &scope, error);
	if (status != HX_OS_OK)
		return status;
	if (state->sealed) {
		bytes = state->bytes;
		memcpy(digest, state->result, bytes);
	}
	else if (state->mac) {
		EVP_MAC_CTX *copy = EVP_MAC_CTX_dup(state->mac);

		if (!copy ||
			EVP_MAC_final(copy, digest, &bytes, sizeof(digest)) !=
				1)
			status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
		EVP_MAC_CTX_free(copy);
	}
	else {
		EVP_MD_CTX *copy = EVP_MD_CTX_new();
		unsigned written = 0;

		if (!copy || EVP_MD_CTX_copy_ex(copy, state->digest) != 1 ||
			EVP_DigestFinal_ex(copy, digest, &written) != 1)
			status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
		bytes = written;
		EVP_MD_CTX_free(copy);
	}
	if (status == HX_OS_OK && bytes > sizeof(digest))
		status = hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
	if (status == HX_OS_OK) {
		for (i = 0; i < bytes; ++i) {
			text[2 * i] = hex[digest[i] >> 4];
			text[2 * i + 1] = hex[digest[i] & 15];
		}
		text[2 * bytes] = 0;
		status = hx_string_assign_cstr(out, text, error);
		if (status == HX_OS_OK) {
			memcpy(state->result, digest, bytes);
			state->bytes = bytes;
			state->sealed = 1;
		}
	}
	hx_os_secure_zero(digest, sizeof(digest));
	hx_os_i_crypto_leave(&scope);

	return status == HX_OS_OK ? hx_os_i_error(error, status, 0, 0) : status;
#else
	(void)state;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_digest_reset(
	hx_os_crypto_digest_state *state, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	hx_os_crypto_scope scope;
	hx_os_status status;

	if (!state)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_crypto_enter(state->crypto, NULL, &scope, error);
	if (status != HX_OS_OK)
		return status;
	if (state->mac_start) {
		EVP_MAC_CTX *copy = EVP_MAC_CTX_dup(state->mac_start);

		if (!copy)
			status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
		else {
			EVP_MAC_CTX_free(state->mac);
			state->mac = copy;
		}
	}
	else {
		EVP_MD_CTX *copy = EVP_MD_CTX_new();

		if (!copy ||
			EVP_MD_CTX_copy_ex(copy, state->digest_start) != 1) {
			EVP_MD_CTX_free(copy);
			status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
		}
		else {
			EVP_MD_CTX_free(state->digest);
			state->digest = copy;
		}
	}
	if (status == HX_OS_OK) {
		state->total = 0;
		state->sealed = state->poisoned = 0;
		hx_os_secure_zero(state->result, sizeof(state->result));
	}
	hx_os_i_crypto_leave(&scope);

	return status == HX_OS_OK ? hx_os_i_error(error, status, 0, 0) : status;
#else
	(void)state;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_digest_destroy(
	hx_os_crypto_digest_state **state, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	hx_os_crypto_scope scope;
	hx_os_crypto_digest_state *value;
	hx_os_status status;

	if (!state)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	value = *state;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	status = hx_os_i_crypto_enter_cleanup(
		value->crypto, value->cleanup_errors, &scope, error);
	if (status != HX_OS_OK)
		return status;
	EVP_MD_CTX_free(value->digest);
	EVP_MD_CTX_free(value->digest_start);
	EVP_MAC_CTX_free(value->mac);
	EVP_MAC_CTX_free(value->mac_start);
	hx_os_crypto_release(value->crypto);
	*state = NULL;
	hx_os_secure_zero(value, sizeof(*value));
	hx_os_memory_free(value);
	hx_os_i_crypto_leave(&scope);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	if (state && !*state)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
