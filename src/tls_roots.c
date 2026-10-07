#include "tls_private.h"

#if HX_OS_HAVE_TLS
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <Security/Security.h>
#endif
#endif

#if defined(__ANDROID__)
typedef struct hx_tls_roots {
	hx_os_tls *tls;
	const char *directory;
} hx_tls_roots;

static hx_os_status hx_tls_root_entry(
	hx_string_view name, void *user, hx_os_error *error)
{
	hx_tls_roots *roots = user;
	hx_os_tls *tls = roots->tls;
	hx_string path = HX_STRING_INIT;
	hx_os_status status;

	hx_string_init(&path, tls->context);
	status = hx_string_assign_cstr(&path, roots->directory, error);
	if (status == HX_OS_OK)
		status = hx_string_append_cstr(&path, "/", error);
	if (status == HX_OS_OK)
		status = hx_string_append(&path, name, error);
	if (status == HX_OS_OK)
		status = hx_tls_i_ca_file(tls, hx_string_c_str(&path), error);
	hx_string_destroy(&path);

	return status;
}
#endif

hx_os_status hx_tls_i_roots(hx_os_tls *tls, hx_os_error *error)
{
#if defined(_WIN32)
	if (SSL_CTX_load_verify_store(
		    tls->ssl_context, "org.openssl.winstore:") != 1)
		return hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);

	return HX_OS_OK;
#elif defined(__APPLE__) && TARGET_OS_OSX
	CFArrayRef certificates = NULL;
	OSStatus native = SecTrustCopyAnchorCertificates(&certificates);
	hx_os_status status = HX_OS_OK;
	CFIndex i;
	X509_STORE *store = SSL_CTX_get_cert_store(tls->ssl_context);

	if (native != errSecSuccess || !certificates)
		return hx_os_i_error(error, HX_OS_IO_ERROR, 0, native);
	for (i = 0; status == HX_OS_OK && i < CFArrayGetCount(certificates);
		++i) {
		SecCertificateRef item =
			(SecCertificateRef)CFArrayGetValueAtIndex(
				certificates, i);
		CFDataRef der = SecCertificateCopyData(item);
		X509 *certificate = X509_new_ex(tls->crypto->native, NULL);
		const unsigned char *cursor =
			der ? CFDataGetBytePtr(der) : NULL;

		if (!der || !certificate ||
			!d2i_X509(
				&certificate, &cursor, CFDataGetLength(der)) ||
			!X509_STORE_add_cert(store, certificate))
			status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
		X509_free(certificate);
		if (der)
			CFRelease(der);
	}
	CFRelease(certificates);

	return status;
#elif defined(__APPLE__)
	(void)tls;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#elif defined(__ANDROID__)
	static const char *const roots[] = {
		"/apex/com.android.conscrypt/cacerts",
		"/system/etc/security/cacerts"};
	hx_os_status status = HX_OS_NOT_FOUND;
	size_t i, count;
	hx_tls_roots source = {tls, NULL};

	for (i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
		source.directory = roots[i];
		status = hx_os_directory_visit_ex(tls->context, roots[i], 4096,
			hx_tls_root_entry, &source, &count, error);
		if (status == HX_OS_OK && !count)
			status = hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
		if (status != HX_OS_NOT_FOUND)
			break;
	}

	return status;
#else
	static const char *const roots[] = {
		"/etc/ssl/certs/ca-certificates.crt",
		"/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem",
		"/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem"};
	hx_os_status status = HX_OS_NOT_FOUND;
	size_t i;

	for (i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
		status = hx_tls_i_ca_file(tls, roots[i], error);
		if (status != HX_OS_NOT_FOUND)
			break;
	}

	return status;
#endif
}
#endif
