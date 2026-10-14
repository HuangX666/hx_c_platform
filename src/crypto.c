#include "crypto_private.h"
#if HX_OS_HAVE_OPENSSL
#include <openssl/crypto.h>
#include <openssl/err.h>
#endif

#if HX_OS_ENABLE_CRYPTO
#if defined(_MSC_VER)
static __declspec(thread) int hx_os_crypto_active;
#else
static __thread int hx_os_crypto_active;
#endif

static hx_os_status hx_os_crypto_save_errors(
	ERR_STATE **saved, hx_os_error *error)
{
	*saved = NULL;
	if (!ERR_peek_error())
		return HX_OS_OK;
	*saved = OSSL_ERR_STATE_new();
	if (!*saved)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	OSSL_ERR_STATE_save(*saved);

	return HX_OS_OK;
}

static void hx_os_crypto_restore_errors(ERR_STATE *saved)
{
	ERR_clear_error();
	if (saved) {
		OSSL_ERR_STATE_restore(saved);
		OSSL_ERR_STATE_free(saved);
	}
}

hx_os_status hx_os_i_crypto_fail(hx_os_error *error, hx_os_status status)
{
	unsigned long code = ERR_peek_last_error();

	if (code && ERR_GET_REASON(code) == ERR_R_MALLOC_FAILURE)
		status = HX_OS_NO_MEMORY;

	return hx_os_i_error(
		error, status, code ? HX_OS_ERROR_OPENSSL : 0, (int64_t)code);
}

static hx_os_status hx_os_crypto_new(
	hx_os_context *context, hx_os_crypto **out, hx_os_error *error)
{
	hx_os_crypto *crypto = hx_os_i_zero(context, sizeof(*crypto));
	hx_os_status status;

	if (!crypto)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	crypto->context = context;
	crypto->native = OSSL_LIB_CTX_new();
	if (crypto->native)
		crypto->provider =
			OSSL_PROVIDER_load(crypto->native, "default");
	if (!crypto->native || !crypto->provider) {
		status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
		OSSL_LIB_CTX_free(crypto->native);
		hx_os_memory_free(crypto);

		return status;
	}
	hx_os_atomic_store(&crypto->references, 1, HX_OS_RELAXED);
	*out = crypto;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_crypto_enter(hx_os_crypto *crypto, hx_os_context *context,
	hx_os_crypto_scope *scope, hx_os_error *error)
{
	hx_os_status status;

	memset(scope, 0, sizeof(*scope));
	if (hx_os_crypto_active)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	hx_os_crypto_active = 1;
	status = hx_os_crypto_save_errors(&scope->saved_errors, error);
	if (status != HX_OS_OK) {
		hx_os_crypto_active = 0;

		return status;
	}
	if (crypto) {
		hx_os_crypto_retain(crypto);
		scope->crypto = crypto;
	}
	else
		status = hx_os_crypto_new(context, &scope->crypto, error);
	if (status != HX_OS_OK) {
		hx_os_crypto_restore_errors(scope->saved_errors);
		hx_os_crypto_active = 0;
	}

	return status;
}

void hx_os_i_crypto_leave(hx_os_crypto_scope *scope)
{
	// Native per-context TLS must retire on each participating thread
	// before the last reference can free this context on another thread.
	OPENSSL_thread_stop_ex(scope->crypto->native);
	hx_os_crypto_release(scope->crypto);
	hx_os_crypto_restore_errors(scope->saved_errors);
	hx_os_crypto_active = 0;
}
#endif

int hx_os_crypto_available(void)
{
	return HX_OS_ENABLE_CRYPTO;
}

hx_os_status hx_os_crypto_create(
	hx_os_context *context, hx_os_crypto **out, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	hx_os_crypto_scope scope;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	status = hx_os_i_crypto_enter(NULL, context, &scope, error);
	if (status != HX_OS_OK)
		return status;
	*out = scope.crypto;
	hx_os_crypto_retain(*out);
	hx_os_i_crypto_leave(&scope);

	return status;
#else
	(void)context;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

void hx_os_crypto_retain(hx_os_crypto *crypto)
{
#if HX_OS_ENABLE_CRYPTO
	if (crypto)
		(void)hx_os_atomic_fetch_add(
			&crypto->references, 1, HX_OS_RELAXED);
#else
	(void)crypto;
#endif
}

void hx_os_crypto_release(hx_os_crypto *crypto)
{
#if HX_OS_ENABLE_CRYPTO
	if (crypto &&
		hx_os_atomic_fetch_add(
			&crypto->references, -1, HX_OS_ACQ_REL) == 1) {
		OPENSSL_thread_stop_ex(crypto->native);
		(void)OSSL_PROVIDER_unload(crypto->provider);
		OSSL_LIB_CTX_free(crypto->native);
		hx_os_memory_free(crypto);
	}
#else
	(void)crypto;
#endif
}

void hx_os_crypto_thread_cleanup(void)
{
#if HX_OS_HAVE_OPENSSL
	OPENSSL_thread_stop();
#endif
}

hx_os_crypto_bytes *hx_os_i_crypto_bytes(
	hx_os_context *context, size_t size, hx_os_error *error)
{
	hx_os_crypto_bytes *bytes;
	size_t capacity;

	if (!hx_os_i_add_size(sizeof(*bytes), size, &capacity)) {
		(void)hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		return NULL;
	}
	bytes = hx_os_i_zero(context, capacity);
	if (!bytes) {
		(void)hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

		return NULL;
	}
	bytes->size = size;

	return bytes;
}

const void *hx_os_crypto_bytes_data(const hx_os_crypto_bytes *bytes)
{
	return bytes ? bytes->data : NULL;
}

size_t hx_os_crypto_bytes_size(const hx_os_crypto_bytes *bytes)
{
	return bytes ? bytes->size : 0;
}

void hx_os_crypto_bytes_destroy(hx_os_crypto_bytes **owner)
{
	if (owner && *owner) {
		hx_os_secure_zero((*owner)->data, (*owner)->size);
		hx_os_memory_free(*owner);
		*owner = NULL;
	}
}

void hx_os_i_crypto_error_text(uint64_t code, char *out, size_t capacity)
{
#if HX_OS_HAVE_OPENSSL
	if (code <= ULONG_MAX)
		ERR_error_string_n((unsigned long)code, out, capacity);
#else
	(void)code;
	(void)out;
	(void)capacity;
#endif
}
