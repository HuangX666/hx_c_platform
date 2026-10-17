#ifndef HX_C_PLATFORM_CRYPTO_PRIVATE_H
#define HX_C_PLATFORM_CRYPTO_PRIVATE_H

#include "internal.h"
#include "hx_os_crypto.h"
#include "hx_os_util.h"

struct hx_os_crypto_bytes {
	size_t size;
	unsigned char data[];
};

hx_os_crypto_bytes *hx_os_i_crypto_bytes(
	hx_os_context *context, size_t size, hx_os_error *error);

hx_os_status hx_os_i_crypto_options(const hx_os_crypto_options *input,
	hx_os_crypto_options *out, hx_os_error *error);
hx_os_status hx_os_i_crypto_password(
	const char *password, hx_os_crypto_secret *secret, hx_os_error *error);

#define HX_OS_I_CRYPTO_HEADER 44
#define HX_OS_I_CRYPTO_TAG 16
#define HX_OS_I_CRYPTO_OVERHEAD (HX_OS_I_CRYPTO_HEADER + HX_OS_I_CRYPTO_TAG)

#if HX_OS_ENABLE_CRYPTO
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/provider.h>
#include <openssl/rand.h>

struct hx_os_crypto {
	hx_os_context *context;
	OSSL_LIB_CTX *native;
	OSSL_PROVIDER *provider;
	hx_os_atomic references;
};

struct hx_os_crypto_key {
	hx_os_crypto *crypto;
	EVP_PKEY *native;
	int private_key;
};

typedef struct hx_os_crypto_scope {
	hx_os_crypto *crypto;
	ERR_STATE *saved_errors;
} hx_os_crypto_scope;

hx_os_status hx_os_i_crypto_enter(hx_os_crypto *crypto, hx_os_context *context,
	hx_os_crypto_scope *scope, hx_os_error *error);
// On success, leave consumes the reserved error state.
hx_os_status hx_os_i_crypto_enter_cleanup(hx_os_crypto *crypto,
	ERR_STATE *reserved, hx_os_crypto_scope *scope, hx_os_error *error);
void hx_os_i_crypto_leave(hx_os_crypto_scope *scope);
hx_os_status hx_os_i_crypto_fail(hx_os_error *error, hx_os_status status);

hx_os_status hx_os_i_crypto_cipher(hx_os_crypto *crypto,
	const hx_os_crypto_secret *secret, const hx_os_crypto_options *options,
	int decrypt, unsigned char header[HX_OS_I_CRYPTO_HEADER],
	EVP_CIPHER_CTX **out, hx_os_error *error);
#endif
#endif
