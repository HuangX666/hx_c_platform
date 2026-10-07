#include "utility_test.h"

#if defined(HX_TEST_TLS_NATIVE)
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

static const char *const ca_file = "tls-ca.pem";
static const char *const cert_file = "tls-server.pem";
static const char *const key_file = "tls-private.pem";

static void certificate_extension(
	X509 *certificate, X509 *issuer, int nid, const char *value)
{
	X509V3_CTX context;
	X509_EXTENSION *extension;

	X509V3_set_ctx(&context, issuer, certificate, NULL, NULL, 0);
	extension = X509V3_EXT_conf_nid(NULL, &context, nid, value);
	CHECK(extension && X509_add_ext(certificate, extension, -1));
	X509_EXTENSION_free(extension);
}

static X509 *certificate(
	EVP_PKEY *key, X509 *issuer, EVP_PKEY *signer, int expired)
{
	X509 *cert = X509_new();
	X509_NAME *name;

	CHECK(cert && X509_set_version(cert, 2));
	CHECK(ASN1_INTEGER_set(X509_get_serialNumber(cert), issuer ? 2 : 1));
	CHECK(X509_gmtime_adj(X509_getm_notBefore(cert), -3600));
	CHECK(X509_gmtime_adj(X509_getm_notAfter(cert), expired ? -60 : 86400));
	CHECK(X509_set_pubkey(cert, key));
	name = X509_get_subject_name(cert);
	CHECK(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
		(const unsigned char *)(issuer ? "localhost" : "HX test CA"),
		-1, -1, 0));
	CHECK(X509_set_issuer_name(
		cert, issuer ? X509_get_subject_name(issuer) : name));
	certificate_extension(cert, issuer ? issuer : cert,
		NID_basic_constraints,
		issuer ? "critical,CA:FALSE" : "critical,CA:TRUE");
	certificate_extension(cert, issuer ? issuer : cert, NID_key_usage,
		issuer ? "critical,digitalSignature"
		       : "critical,keyCertSign,cRLSign");
	certificate_extension(cert, issuer ? issuer : cert,
		NID_subject_key_identifier, "hash");
	certificate_extension(cert, issuer ? issuer : cert,
		NID_authority_key_identifier, "keyid:always");
	if (issuer) {
		certificate_extension(cert, issuer, NID_ext_key_usage,
			"serverAuth,clientAuth");
		certificate_extension(cert, issuer, NID_subject_alt_name,
			"DNS:localhost,IP:127.0.0.1");
	}
	CHECK(X509_sign(cert, signer, EVP_sha256()));

	return cert;
}

static void bio_save(const char *path, BIO *bio)
{
	char *data;
	long bytes = BIO_get_mem_data(bio, &data);

	CHECK(bytes > 0);
	CHECK(hx_os_file_save(path, data, (size_t)bytes, 1, NULL) == HX_OS_OK);
	BIO_free(bio);
}

static void certificate_save(const char *path, X509 *cert)
{
	BIO *bio = BIO_new(BIO_s_mem());

	CHECK(bio && PEM_write_bio_X509(bio, cert));
	bio_save(path, bio);
}

static void fixtures(void)
{
	EVP_PKEY *ca_key = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "prime256v1");
	EVP_PKEY *key = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "prime256v1");
	X509 *ca, *cert, *expired;
	BIO *bio;

	CHECK(ca_key && key);
	ca = certificate(ca_key, NULL, ca_key, 0);
	cert = certificate(key, ca, ca_key, 0);
	expired = certificate(key, ca, ca_key, 1);
	certificate_save(ca_file, ca);
	certificate_save(cert_file, cert);
	certificate_save("tls-expired.pem", expired);
	bio = BIO_new(BIO_s_mem());
	CHECK(bio &&
		PEM_write_bio_PrivateKey(bio, key, NULL, NULL, 0, NULL, NULL));
	bio_save(key_file, bio);
	bio = BIO_new(BIO_s_mem());
	CHECK(bio &&
		PEM_write_bio_PKCS8PrivateKey(bio, key, EVP_aes_256_cbc(),
			"口令🔑", (int)strlen("口令🔑"), NULL, NULL));
	bio_save("tls-locked.pem", bio);
	X509_free(expired);
	X509_free(cert);
	X509_free(ca);
	EVP_PKEY_free(key);
	EVP_PKEY_free(ca_key);
}

typedef struct tls_pair {
	hx_os_tls *server, *client;
	uint64_t peer;
	hx_os_address address;
} tls_pair;

static void pair_open(tls_pair *pair, int mutual)
{
	hx_os_tls_options server = HX_OS_TLS_OPTIONS_INIT;
	hx_os_tls_options client = HX_OS_TLS_OPTIONS_INIT;
	hx_os_tls_peer_info info;

	memset(pair, 0, sizeof(*pair));
	server.certificate_file = cert_file;
	server.private_key_file = key_file;
	server.queue_capacity = 16384;
	server.max_peers = 2;
	if (mutual) {
		server.flags = HX_OS_TLS_REQUIRE_CLIENT_CERTIFICATE;
		server.ca_file = ca_file;
		client.certificate_file = cert_file;
		client.private_key_file = key_file;
	}
	CHECK(hx_os_tls_listen("127.0.0.1", 0, &server, &pair->server, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_tls_local_address(pair->server, &pair->address, NULL) ==
		HX_OS_OK);
	client.ca_file = ca_file;
	client.server_name = "localhost";
	client.queue_capacity = 16384;
	CHECK(hx_os_tls_connect("127.0.0.1", pair->address.port, &client,
		      &pair->client, NULL) == HX_OS_OK);
	CHECK(hx_os_tls_accept_until(pair->server,
		      hx_os_deadline_after_ms(5000), &pair->peer,
		      NULL) == HX_OS_OK);
	CHECK(pair->peer);
	CHECK(hx_os_tls_get_peer_info(
		      pair->client, 0, &info, sizeof(info), NULL) == HX_OS_OK);
	CHECK(info.flags ==
		(HX_OS_TLS_PEER_CERTIFICATE | HX_OS_TLS_PEER_VERIFIED));
	CHECK(!strcmp(info.protocol, "TLSv1.3"));
	if (mutual) {
		CHECK(hx_os_tls_get_peer_info(pair->server, pair->peer, &info,
			      sizeof(info), NULL) == HX_OS_OK);
		CHECK(info.flags & HX_OS_TLS_PEER_VERIFIED);
	}
}

static void pair_close(tls_pair *pair)
{
	CHECK(hx_os_tls_close_until(&pair->client,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(hx_os_tls_close_until(&pair->server,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(!pair->server && !pair->client);
}

typedef struct transfer {
	hx_os_tls *tls;
	uint64_t peer;
	size_t bytes, completed;
	hx_os_status status;
	unsigned seed;
	int receive;
} transfer;

static void *HX_OS_CALL transfer_run(void *user)
{
	transfer *work = user;
	unsigned char *data = malloc(work->bytes);
	size_t i;

	CHECK(data);
	for (i = 0; i < work->bytes; ++i)
		data[i] = (unsigned char)(i * 17 + work->seed);
	if (work->receive) {
		memset(data, 0, work->bytes);
		work->status = hx_os_tls_receive_exact_until(work->tls,
			work->peer, data, work->bytes,
			hx_os_deadline_after_ms(10000), &work->completed, NULL);
		if (work->status == HX_OS_OK)
			for (i = 0; i < work->bytes; ++i)
				CHECK(data[i] ==
					(unsigned char)(i * 17 + work->seed));
	}
	else
		work->status = hx_os_tls_send_until(work->tls, work->peer, data,
			work->bytes, hx_os_deadline_after_ms(10000),
			&work->completed, NULL);
	free(data);

	return NULL;
}

static void duplex(void)
{
	tls_pair pair;
	transfer work[4];
	hx_os_thread *threads[4] = {0};
	size_t i;

	pair_open(&pair, 1);
	memset(work, 0, sizeof(work));
	for (i = 0; i < 4; ++i) {
		work[i].tls = i < 2 ? pair.client : pair.server;
		work[i].peer = i < 2 ? 0 : pair.peer;
		work[i].receive = (int)(i % 2);
		work[i].seed = i == 0 || i == 3 ? 7 : 13;
		work[i].bytes = 1024 * 1024;
		CHECK(hx_os_thread_create(NULL, transfer_run, work + i,
			      threads + i, NULL) == HX_OS_OK);
	}
	for (i = 0; i < 4; ++i) {
		CHECK(hx_os_thread_join_until(threads[i],
			      hx_os_deadline_after_ms(15000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(threads + i, NULL) == HX_OS_OK);
		if (work[i].status != HX_OS_OK)
			fprintf(stderr, "duplex %zu: %s (%zu bytes)\n", i,
				hx_os_status_name(work[i].status),
				work[i].completed);
		CHECK(work[i].status == HX_OS_OK);
		CHECK(work[i].completed == work[i].bytes);
	}
	pair_close(&pair);
}

static void waits(void)
{
	tls_pair pair;
	char buffer[32];
	size_t bytes;
	transfer work;
	hx_os_thread *thread = NULL;

	pair_open(&pair, 0);
	CHECK(hx_os_tls_receive_until(pair.client, 0, buffer, sizeof(buffer),
		      HX_OS_DEADLINE_NOW, &bytes, NULL) == HX_OS_TIMEOUT);
	CHECK(bytes == 0);
	memset(&work, 0, sizeof(work));
	work.tls = pair.client;
	work.receive = 1;
	work.bytes = 32;
	CHECK(hx_os_thread_create(NULL, transfer_run, &work, &thread, NULL) ==
		HX_OS_OK);
	{
		hx_os_deadline started = hx_os_deadline_after_ms(5000);
		hx_os_status status;

		do {
			status = hx_os_tls_receive_until(pair.client, 0, buffer,
				sizeof(buffer), HX_OS_DEADLINE_NOW, &bytes,
				NULL);
			if (status == HX_OS_BUSY)
				break;
			CHECK(status == HX_OS_TIMEOUT);
			(void)hx_os_sleep_until(
				hx_os_deadline_after_ms(1), NULL);
		} while (!hx_os_deadline_expired(started));
		CHECK(status == HX_OS_BUSY);
	}
	CHECK(hx_os_tls_cancel_waits(pair.client, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(5000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(work.status == HX_OS_CANCELLED);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	CHECK(hx_os_tls_send_until(pair.server, pair.peer, "hello", 5,
		      hx_os_deadline_after_ms(5000), &bytes, NULL) == HX_OS_OK);
	CHECK(hx_os_tls_receive_exact_until(pair.client, 0, buffer, 5,
		      hx_os_deadline_after_ms(5000), &bytes, NULL) == HX_OS_OK);
	CHECK(bytes == 5 && !memcmp(buffer, "hello", 5));
	pair_close(&pair);
}

static void verification(void)
{
	tls_pair pair;
	hx_os_tls_peer_info info;
	hx_os_tls_options options = HX_OS_TLS_OPTIONS_INIT;
	hx_os_tls *bad = NULL;
	hx_os_error error;
	hx_os_status status;

	pair_open(&pair, 0);
	options.ca_file = ca_file;
	options.server_name = "wrong.example";
	status = hx_os_tls_connect(
		"127.0.0.1", pair.address.port, &options, &bad, &error);
	CHECK(status == HX_OS_AUTH_FAILED && bad);
	CHECK(error.domain == HX_OS_ERROR_OPENSSL);
	CHECK(error.cause_code == X509_V_ERR_HOSTNAME_MISMATCH);
	CHECK(hx_os_tls_get_peer_info(bad, 0, &info, sizeof(info), NULL) ==
		HX_OS_AUTH_FAILED);
	(void)hx_os_tls_abort_until(&bad, hx_os_deadline_after_ms(5000), NULL);
	CHECK(!bad);
	options.ca_file = NULL;
	options.server_name = "localhost";
	status = hx_os_tls_connect(
		"127.0.0.1", pair.address.port, &options, &bad, &error);
	CHECK(status == HX_OS_AUTH_FAILED && bad);
	(void)hx_os_tls_abort_until(&bad, hx_os_deadline_after_ms(5000), NULL);
	CHECK(!bad);
	pair_close(&pair);
}

static void abrupt(void)
{
	tls_pair pair;
	size_t bytes;
	char buffer[8];

	pair_open(&pair, 0);
	CHECK(hx_os_tls_send_until(pair.server, pair.peer, "123", 3,
		      hx_os_deadline_after_ms(5000), &bytes, NULL) == HX_OS_OK);
	CHECK(hx_os_tls_abort_until(&pair.server, hx_os_deadline_after_ms(5000),
		      NULL) == HX_OS_OK);
	CHECK(hx_os_tls_receive_exact_until(pair.client, 0, buffer, 8,
		      hx_os_deadline_after_ms(5000), &bytes,
		      NULL) == HX_OS_TRUNCATED_INPUT);
	CHECK(bytes == 3 && !memcmp(buffer, "123", 3));
	(void)hx_os_tls_abort_until(
		&pair.client, hx_os_deadline_after_ms(5000), NULL);
	CHECK(!pair.client);
}

typedef struct tls_memory {
	hx_os_atomic calls, live;
	int64_t fail;
} tls_memory;

static void *HX_OS_CALL tls_allocate(size_t bytes, void *user)
{
	tls_memory *state = user;
	void *memory;
	int64_t call =
		hx_os_atomic_fetch_add(&state->calls, 1, HX_OS_RELAXED) + 1;

	if (call == state->fail)
		return NULL;
	memory = malloc(bytes);
	if (memory)
		(void)hx_os_atomic_fetch_add(&state->live, 1, HX_OS_RELAXED);

	return memory;
}

static void HX_OS_CALL tls_deallocate(void *memory, void *user)
{
	tls_memory *state = user;

	if (memory) {
		CHECK(hx_os_atomic_fetch_add(&state->live, -1, HX_OS_RELAXED) >
			0);
		free(memory);
	}
}

static void allocation_failures(void)
{
	int64_t failure;

	for (failure = 1; failure <= 48; ++failure) {
		tls_memory memory = {0};
		hx_os_context_options allocator;
		hx_os_context *context = NULL;
		hx_os_tls_options options = HX_OS_TLS_OPTIONS_INIT;
		hx_os_tls *tls = NULL;

		hx_os_context_options_init(&allocator);
		allocator.allocator.allocate = tls_allocate;
		allocator.allocator.deallocate = tls_deallocate;
		allocator.allocator.user = &memory;
		CHECK(hx_os_context_create(&allocator, &context, NULL) ==
			HX_OS_OK);
		memory.fail = failure +
			hx_os_atomic_load(&memory.calls, HX_OS_RELAXED);
		options.certificate_file = cert_file;
		options.private_key_file = key_file;
		(void)hx_os_tls_listen_until(context, "127.0.0.1", 0, &options,
			hx_os_deadline_after_ms(5000), &tls, NULL);
		(void)hx_os_tls_abort_until(
			&tls, hx_os_deadline_after_ms(5000), NULL);
		CHECK(!tls);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
		CHECK(hx_os_atomic_load(&memory.live, HX_OS_RELAXED) == 0);
	}
}

static void encrypted_identity(void)
{
	hx_os_tls_options options = HX_OS_TLS_OPTIONS_INIT;
	hx_os_tls *tls = NULL;

	options.certificate_file = cert_file;
	options.private_key_file = "tls-locked.pem";
	options.key_password = "口令🔑";
	CHECK(hx_os_tls_listen("127.0.0.1", 0, &options, &tls, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_tls_close_until(
		      &tls, hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	options.key_password = "incorrect password";
	CHECK(hx_os_tls_listen("127.0.0.1", 0, &options, &tls, NULL) ==
		HX_OS_INVALID);
	CHECK(tls);
	(void)hx_os_tls_abort_until(&tls, hx_os_deadline_after_ms(5000), NULL);
	CHECK(!tls);
}

static void peer_lifetime(void)
{
	tls_pair pair;
	hx_os_tls_options options = HX_OS_TLS_OPTIONS_INIT;
	uint64_t old;
	hx_os_tls_peer_info info;

	pair_open(&pair, 0);
	old = pair.peer;
	CHECK(hx_os_tls_peer_close_until(pair.server, old, HX_OS_DEADLINE_NOW,
		      NULL) == HX_OS_TIMEOUT);
	CHECK(hx_os_tls_peer_close_until(pair.server, old,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(hx_os_tls_get_peer_info(pair.server, old, &info, sizeof(info),
		      NULL) == HX_OS_NOT_FOUND);
	CHECK(hx_os_tls_close_until(&pair.client, hx_os_deadline_after_ms(5000),
		      NULL) == HX_OS_OK);
	options.ca_file = ca_file;
	CHECK(hx_os_tls_connect("127.0.0.1", pair.address.port, &options,
		      &pair.client, NULL) == HX_OS_OK);
	CHECK(hx_os_tls_accept_until(pair.server, hx_os_deadline_after_ms(5000),
		      &pair.peer, NULL) == HX_OS_OK);
	CHECK(pair.peer != old);
	pair_close(&pair);
}
#endif

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
#if defined(HX_TEST_TLS_NATIVE)
	CHECK(hx_os_tls_available());
	fixtures();
	if (argc == 2 && !strcmp(argv[1], "--fixtures"))
		return 0;
	duplex();
	waits();
	verification();
	abrupt();
	allocation_failures();
	encrypted_identity();
	peer_lifetime();
	CHECK(hx_os_file_remove(ca_file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(cert_file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(key_file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove("tls-expired.pem", NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove("tls-locked.pem", NULL) == HX_OS_OK);
	hx_os_crypto_thread_cleanup();
#else
	hx_os_tls *tls = NULL;

	if (hx_os_tls_available()) {
		hx_os_tls_options options = HX_OS_TLS_OPTIONS_INIT;

		options.ca_file = "hx-missing-tls-ca.pem";
		CHECK(hx_os_tls_connect("127.0.0.1", 443, &options, &tls,
			      NULL) == HX_OS_NOT_FOUND);
		CHECK(tls);
		(void)hx_os_tls_abort_until(
			&tls, hx_os_deadline_after_ms(5000), NULL);
	}
	else
		CHECK(hx_os_tls_connect("127.0.0.1", 443, NULL, &tls, NULL) ==
			HX_OS_NOT_SUPPORTED);
	CHECK(!tls);
#endif

	return 0;
}
