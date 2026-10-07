#include "crypto_private.h"

#if HX_OS_ENABLE_CRYPTO
#include <openssl/pem.h>
#include <openssl/rsa.h>

static hx_os_status hx_os_crypto_key_take(hx_os_crypto *crypto,
	EVP_PKEY *native, int private_key, hx_os_crypto_key **out,
	hx_os_error *error)
{
	hx_os_crypto_key *key = hx_os_i_zero(crypto->context, sizeof(*key));

	if (!key)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	hx_os_crypto_retain(crypto);
	key->crypto = crypto;
	key->native = native;
	key->private_key = private_key;
	*out = key;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static int hx_os_crypto_key_whitespace(unsigned char ch)
{
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static hx_os_status hx_os_crypto_key_pem(
	hx_os_bytes_view *pem, int private_key, hx_os_error *error)
{
	const char *begin = private_key ? "-----BEGIN PRIVATE KEY-----"
					: "-----BEGIN PUBLIC KEY-----";
	const char *end = private_key ? "-----END PRIVATE KEY-----"
				      : "-----END PUBLIC KEY-----";
	const unsigned char *data = pem->data;
	size_t offset = 0, bytes = pem->size, first = strlen(begin);
	size_t last = strlen(end), i;

	if (!data || !bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (bytes > 65536)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	while (offset < bytes && hx_os_crypto_key_whitespace(data[offset]))
		++offset;
	while (bytes > offset && hx_os_crypto_key_whitespace(data[bytes - 1]))
		--bytes;
	if (bytes - offset <= first + last ||
		memcmp(data + offset, begin, first) ||
		memcmp(data + bytes - last, end, last))
		return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
	for (i = offset + first; i < bytes - last; ++i)
		if (!(data[i] >= 'A' && data[i] <= 'Z') &&
			!(data[i] >= 'a' && data[i] <= 'z') &&
			!(data[i] >= '0' && data[i] <= '9') && data[i] != '+' &&
			data[i] != '/' && data[i] != '=' &&
			!hx_os_crypto_key_whitespace(data[i]))
			return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
	pem->data = data + offset;
	pem->size = bytes - offset;

	return HX_OS_OK;
}

static int hx_os_crypto_no_password(
	char *buffer, int size, int writing, void *user)
{
	(void)buffer;
	(void)size;
	(void)writing;
	(void)user;

	return 0;
}

static hx_os_status hx_os_crypto_key_import(hx_os_crypto *crypto,
	hx_os_bytes_view pem, int private_key, hx_os_crypto_key **out,
	hx_os_error *error)
{
	hx_os_crypto_scope scope;
	EVP_PKEY *native = NULL;
	EVP_PKEY_CTX *check = NULL;
	BIO *bio;
	int bits;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	status = hx_os_crypto_key_pem(&pem, private_key, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_i_crypto_enter(crypto, NULL, &scope, error);
	if (status != HX_OS_OK)
		return status;
	bio = BIO_new_mem_buf(pem.data, (int)pem.size);
	if (bio) {
		if (private_key)
			native = PEM_read_bio_PrivateKey_ex(bio, NULL,
				hx_os_crypto_no_password, NULL,
				scope.crypto->native, "provider=default");
		else
			native = PEM_read_bio_PUBKEY_ex(bio, NULL,
				hx_os_crypto_no_password, NULL,
				scope.crypto->native, "provider=default");
	}
	if (!native)
		status = hx_os_i_crypto_fail(error, HX_OS_SYNTAX);
	else {
		bits = EVP_PKEY_get_bits(native);
		if (!EVP_PKEY_is_a(native, "ED25519") &&
			(!EVP_PKEY_is_a(native, "RSA") || bits < 2048 ||
				bits > 8192))
			status =
				hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	}
	if (status == HX_OS_OK) {
		check = EVP_PKEY_CTX_new_from_pkey(
			scope.crypto->native, native, "provider=default");
		if (!check ||
			(private_key ? EVP_PKEY_pairwise_check(check)
				     : EVP_PKEY_public_check(check)) != 1)
			status = hx_os_i_crypto_fail(error, HX_OS_SYNTAX);
	}
	if (status == HX_OS_OK)
		status = hx_os_crypto_key_take(
			scope.crypto, native, private_key, out, error);
	if (status != HX_OS_OK)
		EVP_PKEY_free(native);
	EVP_PKEY_CTX_free(check);
	BIO_free(bio);
	hx_os_i_crypto_leave(&scope);

	return status;
}

static hx_os_status hx_os_crypto_key_export(hx_os_crypto_key *key,
	hx_os_crypto_bytes **bytes, hx_string *text, hx_os_error *error)
{
	hx_os_crypto_scope scope;
	BIO *bio;
	char *data = NULL;
	long size;
	int written;
	hx_os_status status;

	if (bytes)
		*bytes = NULL;
	if (!key || (!bytes && !text))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (bytes && !key->private_key)
		return hx_os_i_error(error, HX_OS_ACCESS_DENIED, 0, 0);
	status = hx_os_i_crypto_enter(key->crypto, NULL, &scope, error);
	if (status != HX_OS_OK)
		return status;
	bio = BIO_new(BIO_s_mem());
	written = bio
		? (bytes ? PEM_write_bio_PKCS8PrivateKey(bio, key->native, NULL,
				   NULL, 0, hx_os_crypto_no_password, NULL)
			 : PEM_write_bio_PUBKEY(bio, key->native))
		: 0;
	if (written != 1)
		status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
	size = bio ? BIO_get_mem_data(bio, &data) : 0;
	if (status == HX_OS_OK && (size <= 0 || size > 65536))
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if (status == HX_OS_OK && bytes) {
		*bytes = hx_os_i_crypto_bytes(
			key->crypto->context, (size_t)size, error);
		if (!*bytes)
			status = HX_OS_NO_MEMORY;
		else
			memcpy((*bytes)->data, data, (size_t)size);
	}
	else if (status == HX_OS_OK) {
		hx_string_view view = {data, (size_t)size};

		status = hx_string_assign(text, view, error);
	}
	if (bytes && size > 0)
		hx_os_secure_zero(data, (size_t)size);
	BIO_free(bio);
	hx_os_i_crypto_leave(&scope);
	if (status == HX_OS_OK)
		hx_os_error_clear(error);

	return status;
}

static hx_os_status hx_os_crypto_signature(hx_os_crypto_key *key,
	hx_os_bytes_view message, hx_os_bytes_view signature,
	hx_os_crypto_bytes **out, hx_os_error *error)
{
	hx_os_crypto_scope scope;
	EVP_MD_CTX *md;
	EVP_PKEY_CTX *pkey = NULL;
	const char *digest;
	hx_os_crypto_bytes *result = NULL;
	size_t size = 0;
	int result_code;
	hx_os_status status;

	if (out)
		*out = NULL;
	if (!key || (!message.data && message.size) ||
		(!out && !signature.data))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (message.size > 16 * 1024 * 1024 || (!out && signature.size > 1024))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if (out && !key->private_key)
		return hx_os_i_error(error, HX_OS_ACCESS_DENIED, 0, 0);
	status = hx_os_i_crypto_enter(key->crypto, NULL, &scope, error);
	if (status != HX_OS_OK)
		return status;
	digest = EVP_PKEY_is_a(key->native, "RSA") ? "SHA256" : NULL;
	md = EVP_MD_CTX_new();
	result_code = md
		? (out ? EVP_DigestSignInit_ex(md, &pkey, digest,
				 scope.crypto->native, "provider=default",
				 key->native, NULL)
		       : EVP_DigestVerifyInit_ex(md, &pkey, digest,
				 scope.crypto->native, "provider=default",
				 key->native, NULL))
		: 0;
	if (result_code != 1 ||
		(digest &&
			(EVP_PKEY_CTX_set_rsa_padding(
				 pkey, RSA_PKCS1_PSS_PADDING) <= 0 ||
				EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey, 32) <=
					0 ||
				EVP_PKEY_CTX_set_rsa_mgf1_md_name(
					pkey, "SHA256", NULL) <= 0)))
		status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
	if (status == HX_OS_OK && out) {
		if (EVP_DigestSign(
			    md, NULL, &size, message.data, message.size) != 1)
			status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
		else if (!size || size > 1024)
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		else {
			result = hx_os_i_crypto_bytes(
				key->crypto->context, size, error);
			if (!result)
				status = HX_OS_NO_MEMORY;
			else if (EVP_DigestSign(md, result->data, &size,
					 message.data, message.size) != 1)
				status = hx_os_i_crypto_fail(
					error, HX_OS_IO_ERROR);
		}
		if (status == HX_OS_OK) {
			result->size = size;
			*out = result;
		}
		else
			hx_os_crypto_bytes_destroy(&result);
	}
	else if (status == HX_OS_OK &&
		EVP_DigestVerify(md, signature.data, signature.size,
			message.data, message.size) != 1)
		status = hx_os_i_crypto_fail(error, HX_OS_AUTH_FAILED);
	EVP_MD_CTX_free(md);
	hx_os_i_crypto_leave(&scope);
	if (status == HX_OS_OK)
		hx_os_error_clear(error);

	return status;
}
#endif

hx_os_status hx_os_crypto_key_generate(hx_os_crypto *crypto, unsigned algorithm,
	hx_os_crypto_key **out, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	hx_os_crypto_scope scope;
	EVP_PKEY *native;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (algorithm != HX_OS_CRYPTO_ED25519 &&
		algorithm != HX_OS_CRYPTO_RSA_3072)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_crypto_enter(crypto, NULL, &scope, error);
	if (status != HX_OS_OK)
		return status;
	native = algorithm == HX_OS_CRYPTO_ED25519
		? EVP_PKEY_Q_keygen(
			  scope.crypto->native, "provider=default", "ED25519")
		: EVP_PKEY_Q_keygen(scope.crypto->native, "provider=default",
			  "RSA", (size_t)3072);
	status = native
		? hx_os_crypto_key_take(scope.crypto, native, 1, out, error)
		: hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
	if (status != HX_OS_OK)
		EVP_PKEY_free(native);
	hx_os_i_crypto_leave(&scope);

	return status;
#else
	(void)crypto;
	(void)algorithm;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_key_import_private(hx_os_crypto *crypto,
	hx_os_bytes_view pem, hx_os_crypto_key **out, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	return hx_os_crypto_key_import(crypto, pem, 1, out, error);
#else
	(void)crypto;
	(void)pem;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_key_import_public(hx_os_crypto *crypto,
	hx_os_bytes_view pem, hx_os_crypto_key **out, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	return hx_os_crypto_key_import(crypto, pem, 0, out, error);
#else
	(void)crypto;
	(void)pem;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

void hx_os_crypto_key_destroy(hx_os_crypto_key **owner)
{
#if HX_OS_ENABLE_CRYPTO
	if (owner && *owner) {
		hx_os_crypto_key *key = *owner;

		EVP_PKEY_free(key->native);
		hx_os_crypto_release(key->crypto);
		hx_os_memory_free(key);
		*owner = NULL;
	}
#else
	(void)owner;
#endif
}

hx_os_status hx_os_crypto_key_export_public(
	hx_os_crypto_key *key, hx_string *out, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	return hx_os_crypto_key_export(key, NULL, out, error);
#else
	(void)key;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_key_export_private(
	hx_os_crypto_key *key, hx_os_crypto_bytes **out, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_crypto_key_export(key, out, NULL, error);
#else
	(void)key;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_sign(hx_os_crypto_key *key, hx_os_bytes_view message,
	hx_os_crypto_bytes **out, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	const hx_os_bytes_view empty = {NULL, 0};

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_crypto_signature(key, message, empty, out, error);
#else
	(void)key;
	(void)message;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_verify(hx_os_crypto_key *key,
	hx_os_bytes_view message, hx_os_bytes_view signature,
	hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	return hx_os_crypto_signature(key, message, signature, NULL, error);
#else
	(void)key;
	(void)message;
	(void)signature;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
