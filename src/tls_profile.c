#include "tls_private.h"

#if HX_OS_HAVE_TLS
void hx_tls_i_profile_capture(hx_tls_peer *peer)
{
	hx_os_tls_profile *profile = peer->owner->profile;
	SSL_SESSION *previous;

	if (!profile || !peer->ready || !peer->pending_session)
		return;
	(void)hx_os_mutex_lock(profile->mutex, NULL);
	previous = profile->session;
	profile->session = peer->pending_session;
	peer->pending_session = NULL;
	(void)hx_os_mutex_unlock(profile->mutex, NULL);
	SSL_SESSION_free(previous);
}

int hx_tls_i_profile_new_session(SSL *ssl, SSL_SESSION *session)
{
	hx_tls_peer *peer = SSL_get_app_data(ssl);

	if (!peer || !peer->owner->profile)
		return 0;
	SSL_SESSION_free(peer->pending_session);
	peer->pending_session = session;
	hx_tls_i_profile_capture(peer);
	// Retain the application reference supplied to the new-session
	// callback.
	return 1;
}

hx_os_status hx_tls_i_profile_ssl(hx_tls_peer *peer, hx_os_error *error)
{
	hx_os_tls_profile *profile = peer->owner->profile;
	SSL_SESSION *session;
	int result = 1;

	if (!profile)
		return HX_OS_OK;
	SSL_set_app_data(peer->ssl, peer);
	(void)hx_os_mutex_lock(profile->mutex, NULL);
	session = profile->session;
	profile->session = NULL;
	(void)hx_os_mutex_unlock(profile->mutex, NULL);
	if (session && SSL_SESSION_is_resumable(session))
		result = SSL_set_session(peer->ssl, session);
	SSL_SESSION_free(session);
	return result ? HX_OS_OK : hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
}

int hx_tls_i_profile_verify(hx_tls_peer *peer)
{
	X509_STORE_CTX *verification;
	X509 *leaf;
	int result;

	if (!peer->owner->profile || !SSL_session_reused(peer->ssl))
		return 1;
	leaf = SSL_get1_peer_certificate(peer->ssl);
	verification = X509_STORE_CTX_new_ex(peer->owner->crypto->native, NULL);
	result = leaf && verification &&
		X509_STORE_CTX_init(verification,
			SSL_CTX_get_cert_store(peer->owner->ssl_context), leaf,
			SSL_get0_verified_chain(peer->ssl));
	if (result)
		result = X509_VERIFY_PARAM_set1(
			X509_STORE_CTX_get0_param(verification),
			SSL_get0_param(peer->ssl));
	if (result)
		result = X509_verify_cert(verification) == 1;
	X509_STORE_CTX_free(verification);
	X509_free(leaf);
	return result;
}
#endif

hx_os_status hx_os_tls_profile_create(hx_os_context *context, const char *host,
	uint16_t port, const hx_os_tls_options_ex *input,
	hx_os_tls_profile **out, hx_os_error *error)
{
#if HX_OS_HAVE_TLS
	const hx_os_tls_options_ex defaults = HX_OS_TLS_OPTIONS_EX_INIT;
	const hx_os_tls_options_ex *options = input ? input : &defaults;
	hx_os_tls_profile *profile;
	hx_os_crypto_scope scope;
	hx_os_status status;
	const long cache_mode =
		SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (options->size < sizeof(*options) || options->version != 1)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	profile = hx_os_i_zero(context, sizeof(*profile));
	if (!profile)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	hx_os_atomic_store(&profile->references, 1, HX_OS_RELAXED);
	status = hx_os_mutex_create(context, 0, &profile->mutex, error);
	if (status == HX_OS_OK)
		status = hx_tls_i_configuration(context, host, port, options,
			&profile->configuration, error);
	if (status == HX_OS_OK)
		status = hx_os_crypto_create(
			context, &profile->configuration->crypto, error);
	if (status == HX_OS_OK) {
		hx_os_tls *config = profile->configuration;

		status = hx_os_i_crypto_enter(
			config->crypto, context, &scope, error);
		if (status == HX_OS_OK) {
			status = hx_tls_i_credentials(config, error);
			if (status == HX_OS_OK) {
				SSL_CTX_clear_options(
					config->ssl_context, SSL_OP_NO_TICKET);
				SSL_CTX_set_session_cache_mode(
					config->ssl_context, cache_mode);
				SSL_CTX_sess_set_new_cb(config->ssl_context,
					hx_tls_i_profile_new_session);
			}
			hx_os_i_crypto_leave(&scope);
		}
	}
	if (status == HX_OS_OK)
		*out = profile;
	else
		hx_os_tls_profile_release(profile);
	return status;
#else
	(void)context;
	(void)host;
	(void)port;
	(void)input;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

void hx_os_tls_profile_retain(hx_os_tls_profile *profile)
{
#if HX_OS_HAVE_TLS
	if (profile)
		(void)hx_os_atomic_fetch_add(
			&profile->references, 1, HX_OS_RELAXED);
#else
	(void)profile;
#endif
}

void hx_os_tls_profile_release(hx_os_tls_profile *profile)
{
#if HX_OS_HAVE_TLS
	if (profile &&
		hx_os_atomic_fetch_sub(
			&profile->references, 1, HX_OS_ACQ_REL) == 1) {
		hx_os_tls *config = profile->configuration;

		SSL_SESSION_free(profile->session);
		if (config) {
			SSL_CTX_free(config->ssl_context);
			hx_os_crypto_release(config->crypto);
			hx_tls_i_dispose(config);
		}
		hx_os_mutex_destroy(profile->mutex);
		hx_os_memory_free(profile);
	}
#else
	(void)profile;
#endif
}

hx_os_status hx_os_tls_profile_connect_until(hx_os_tls_profile *profile,
	hx_os_deadline deadline, hx_os_tls **out, hx_os_error *error)
{
#if HX_OS_HAVE_TLS
	hx_os_status status;

	if (!profile || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_tls_i_profile_start(profile, out, error);
	if (status == HX_OS_OK)
		status = hx_os_tls_wait_ready_until(*out, deadline, error);
	return status;
#else
	(void)profile;
	(void)deadline;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
