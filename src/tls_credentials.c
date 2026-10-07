#include "tls_private.h"

#if HX_OS_HAVE_TLS
#include <openssl/pem.h>
#include <openssl/x509v3.h>

static int hx_tls_password(char *buffer, int size, int writing, void *user)
{
	const char *password = user;
	size_t bytes = password ? strlen(password) : 0;
	(void)writing;

	if (!bytes || size <= 0 || bytes > (size_t)size)
		return 0;
	memcpy(buffer, password, bytes);

	return (int)bytes;
}

hx_os_status hx_tls_i_ca_file(
	hx_os_tls *tls, const char *path, hx_os_error *error)
{
	void *data = NULL;
	size_t bytes = 0;
	BIO *bio = NULL;
	STACK_OF(X509_INFO) *certificates = NULL;
	X509_STORE *store = SSL_CTX_get_cert_store(tls->ssl_context);
	hx_os_status status = hx_os_file_load(
		tls->context, path, 4 * 1024 * 1024, &data, &bytes, error);
	int i, added = 0;

	if (status == HX_OS_OK) {
		bio = BIO_new_mem_buf(data, (int)bytes);
		if (bio)
			certificates = PEM_X509_INFO_read_bio_ex(bio, NULL,
				hx_tls_password, NULL, tls->crypto->native,
				NULL);
		if (!certificates)
			status = hx_os_i_crypto_fail(error, HX_OS_INVALID);
	}
	if (certificates && sk_X509_INFO_num(certificates) > 4096)
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	for (i = 0; status == HX_OS_OK && i < sk_X509_INFO_num(certificates);
		++i) {
		X509_INFO *item = sk_X509_INFO_value(certificates, i);

		if (item->x509) {
			if (!X509_STORE_add_cert(store, item->x509))
				status = hx_os_i_crypto_fail(
					error, HX_OS_INVALID);
			++added;
		}
	}
	if (status == HX_OS_OK && !added)
		status = hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	sk_X509_INFO_pop_free(certificates, X509_INFO_free);
	BIO_free(bio);
	hx_os_memory_free(data);

	return status;
}

static hx_os_status hx_tls_key_file(
	hx_os_tls *tls, void **out, size_t *bytes, hx_os_error *error)
{
	hx_os_file *file = NULL;
	hx_os_status status = hx_os_file_open(tls->context,
		tls->options.private_key_file, HX_OS_FILE_READ, &file, error);
	unsigned char *data;
	size_t count;

	*out = NULL;
	*bytes = 0;
	if (status != HX_OS_OK)
		return status;
	data = hx_os_i_alloc(tls->context, 65537, 0);
	*out = data;
	if (!data)
		status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	while (status == HX_OS_OK && *bytes < 65537) {
		status = hx_os_file_read_some(
			file, data + *bytes, 65537 - *bytes, &count, error);
		*bytes += count;
		if (status == HX_OS_EOF) {
			status = hx_os_i_error(error, HX_OS_OK, 0, 0);
			break;
		}
		if (status == HX_OS_OK && !count)
			status = hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
	}
	if (status == HX_OS_OK && *bytes > 65536)
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	{
		hx_os_status closed = hx_os_file_close(
			&file, status == HX_OS_OK ? error : NULL);

		if (status == HX_OS_OK)
			status = closed;
	}

	return status;
}

static hx_os_status hx_tls_identity(hx_os_tls *tls, hx_os_error *error)
{
	void *data = NULL;
	size_t bytes = 0;
	BIO *bio = NULL;
	STACK_OF(X509_INFO) *chain = NULL;
	EVP_PKEY *key = NULL;
	hx_os_status status;
	int i;

	status = hx_os_file_load(tls->context, tls->options.certificate_file,
		1024 * 1024, &data, &bytes, error);
	if (status == HX_OS_OK) {
		bio = BIO_new_mem_buf(data, (int)bytes);
		if (bio)
			chain = PEM_X509_INFO_read_bio_ex(bio, NULL,
				hx_tls_password, NULL, tls->crypto->native,
				NULL);
		if (!chain || sk_X509_INFO_num(chain) < 1)
			status = hx_os_i_crypto_fail(error, HX_OS_INVALID);
	}
	if (chain && sk_X509_INFO_num(chain) > 64)
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	for (i = 0; status == HX_OS_OK && i < sk_X509_INFO_num(chain); ++i) {
		X509 *certificate = sk_X509_INFO_value(chain, i)->x509;
		int ok = certificate &&
			(i ? SSL_CTX_add1_chain_cert(
				     tls->ssl_context, certificate)
			   : SSL_CTX_use_certificate(
				     tls->ssl_context, certificate));

		if (!ok)
			status = hx_os_i_crypto_fail(error, HX_OS_INVALID);
	}
	sk_X509_INFO_pop_free(chain, X509_INFO_free);
	BIO_free(bio);
	hx_os_memory_free(data);
	data = NULL;
	bio = NULL;
	if (status == HX_OS_OK)
		status = hx_tls_key_file(tls, &data, &bytes, error);
	if (status == HX_OS_OK) {
		bio = BIO_new_mem_buf(data, (int)bytes);
		if (bio)
			key = PEM_read_bio_PrivateKey_ex(bio, NULL,
				hx_tls_password,
				(void *)tls->options.key_password,
				tls->crypto->native, NULL);
		if (!key || !SSL_CTX_use_PrivateKey(tls->ssl_context, key) ||
			!SSL_CTX_check_private_key(tls->ssl_context))
			status = hx_os_i_crypto_fail(error, HX_OS_INVALID);
	}
	EVP_PKEY_free(key);
	BIO_free(bio);
	if (data)
		hx_os_secure_zero(data, bytes);
	hx_os_memory_free(data);

	return status;
}

hx_os_status hx_tls_i_credentials(hx_os_tls *tls, hx_os_error *error)
{
	hx_os_status status = HX_OS_OK;
	int verify = !tls->server ||
		(tls->options.flags & HX_OS_TLS_REQUIRE_CLIENT_CERTIFICATE);

	tls->ssl_context =
		SSL_CTX_new_ex(tls->crypto->native, NULL, TLS_method());
	if (!tls->ssl_context)
		return hx_os_i_crypto_fail(error, HX_OS_NO_MEMORY);
	if (!SSL_CTX_set_min_proto_version(tls->ssl_context, TLS1_2_VERSION) ||
		!SSL_CTX_set_max_proto_version(
			tls->ssl_context, TLS1_3_VERSION) ||
		!SSL_CTX_set_cipher_list(
			tls->ssl_context, "ECDHE+AESGCM:ECDHE+CHACHA20"))
		return hx_os_i_crypto_fail(error, HX_OS_NOT_SUPPORTED);
	SSL_CTX_set_security_level(tls->ssl_context, 2);
	SSL_CTX_set_options(tls->ssl_context,
		SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION |
			SSL_OP_NO_TICKET);
	SSL_CTX_set_session_cache_mode(tls->ssl_context, SSL_SESS_CACHE_OFF);
	(void)SSL_CTX_set_num_tickets(tls->ssl_context, 0);
	SSL_CTX_set_max_cert_list(tls->ssl_context, 1024 * 1024);
	SSL_CTX_set_verify_depth(tls->ssl_context, 16);
	SSL_CTX_set_verify(tls->ssl_context,
		verify ? SSL_VERIFY_PEER |
				(tls->server ? SSL_VERIFY_FAIL_IF_NO_PEER_CERT
					     : 0)
		       : SSL_VERIFY_NONE,
		NULL);
	if (verify)
		status = tls->options.ca_file
			? hx_tls_i_ca_file(tls, tls->options.ca_file, error)
			: hx_tls_i_roots(tls, error);
	if (status == HX_OS_OK && tls->options.certificate_file)
		status = hx_tls_identity(tls, error);

	return status;
}

hx_os_status hx_tls_i_ssl(hx_tls_peer *peer, hx_os_error *error)
{
	hx_os_tls *tls = peer->owner;
	BIO *internal = NULL;

	peer->ssl = SSL_new(tls->ssl_context);
	if (!peer->ssl ||
		!BIO_new_bio_pair(&internal, HX_TLS_RECORD * 2, &peer->bio,
			HX_TLS_RECORD * 2))
		return hx_os_i_crypto_fail(error, HX_OS_NO_MEMORY);
	SSL_set_bio(peer->ssl, internal, internal);
	if (tls->server)
		SSL_set_accept_state(peer->ssl);
	else {
		const char *name = tls->options.server_name
			? tls->options.server_name
			: tls->host;
		hx_os_address address;
		X509_VERIFY_PARAM *param = SSL_get0_param(peer->ssl);
		int ok;
		size_t i;

		for (i = 0; name[i]; ++i)
			if ((unsigned char)name[i] <= 32 ||
				(unsigned char)name[i] >= 127 || i >= 1024)
				return hx_os_i_error(
					error, HX_OS_INVALID, 0, 0);
		SSL_set_connect_state(peer->ssl);
		X509_VERIFY_PARAM_set_hostflags(param,
			X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS |
				X509_CHECK_FLAG_NEVER_CHECK_SUBJECT);
		if (hx_os_address_parse(name, 0, &address, NULL) == HX_OS_OK)
			ok = X509_VERIFY_PARAM_set1_ip_asc(param, name);
		else
			ok = SSL_set1_host(peer->ssl, name) &&
				SSL_set_tlsext_host_name(peer->ssl, name);
		if (!ok || !*name)
			return hx_os_i_crypto_fail(error, HX_OS_INVALID);
	}

	return HX_OS_OK;
}
#endif
