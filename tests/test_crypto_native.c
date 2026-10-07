#include "utility_test.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/err.h>

static void check_rfc8032(void)
{
	static const char pem[] = "-----BEGIN PRIVATE KEY-----\n"
				  "MC4CAQAwBQYDK2VwBCIEIJ1hsZ3v/"
				  "VpguoRK9JLsLMREScVpezJpGXA7rAMcrn9g\n"
				  "-----END PRIVATE KEY-----\n";
	static const char expected[] = "e5564300c360ac729086e2cc806e828a84877f1"
				       "eb8e5d974d873e065224901555f"
				       "b8821590a33bacc61e39701cf9b46bd25bf5f05"
				       "95bbe24655141438e7a100b";
	const hx_os_bytes_view input = {pem, sizeof(pem) - 1},
			       empty = {NULL, 0};
	hx_os_crypto_key *key = NULL;
	hx_os_crypto_bytes *signature = NULL;
	char encoded[129];
	const unsigned char *data;
	size_t i;

	CHECK(hx_os_crypto_key_import_private(NULL, input, &key, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_crypto_sign(key, empty, &signature, NULL) == HX_OS_OK);
	CHECK(hx_os_crypto_bytes_size(signature) == 64);
	data = hx_os_crypto_bytes_data(signature);
	for (i = 0; i < 64; ++i)
		(void)snprintf(encoded + 2 * i, 3, "%02x", data[i]);
	CHECK(!strcmp(encoded, expected));
	hx_os_crypto_bytes_destroy(&signature);
	hx_os_crypto_key_destroy(&key);
}

static void check_envelope(void)
{
	unsigned char key[32] = {0}, plaintext[16] = {0};
	unsigned char header[44] = {'H', 'X', 'C', 'R', 'Y', 'P', 'T', 1, 1, 1};
	unsigned char envelope[76], decoded[32], tail[16];
	hx_os_crypto_secret secret = {{key, sizeof(key)}, HX_OS_CRYPTO_KEY_256};
	hx_os_bytes_view input = {envelope, sizeof(envelope)};
	hx_os_crypto_bytes *result = NULL, *encrypted = NULL;
	EVP_CIPHER_CTX *native = EVP_CIPHER_CTX_new();
	int count;
	const unsigned char *generated;

	CHECK(native);
	CHECK(EVP_EncryptInit_ex(
		      native, EVP_aes_256_gcm(), NULL, key, header + 32) == 1);
	CHECK(EVP_EncryptUpdate(native, NULL, &count, header, sizeof(header)) ==
		1);
	memcpy(envelope, header, sizeof(header));
	CHECK(EVP_EncryptUpdate(native, envelope + 44, &count, plaintext, 16) ==
			1 &&
		count == 16);
	CHECK(EVP_EncryptFinal_ex(native, tail, &count) == 1 && count == 0);
	CHECK(EVP_CIPHER_CTX_ctrl(
		      native, EVP_CTRL_AEAD_GET_TAG, 16, envelope + 60) == 1);
	CHECK(hx_os_crypto_decrypt(NULL, &secret, input, &result, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_crypto_bytes_size(result) == 16);
	CHECK(!memcmp(hx_os_crypto_bytes_data(result), plaintext, 16));
	hx_os_crypto_bytes_destroy(&result);

	input.data = plaintext;
	input.size = sizeof(plaintext);
	CHECK(hx_os_crypto_encrypt(NULL, &secret, input, &encrypted, NULL) ==
		HX_OS_OK);
	generated = hx_os_crypto_bytes_data(encrypted);
	CHECK(EVP_DecryptInit_ex(native, EVP_aes_256_gcm(), NULL, key,
		      generated + 32) == 1);
	CHECK(EVP_DecryptUpdate(native, NULL, &count, generated, 44) == 1);
	CHECK(EVP_DecryptUpdate(native, decoded, &count, generated + 44, 16) ==
			1 &&
		count == 16);
	CHECK(EVP_CIPHER_CTX_ctrl(native, EVP_CTRL_AEAD_SET_TAG, 16,
		      (void *)(generated + 60)) == 1);
	CHECK(EVP_DecryptFinal_ex(native, tail, &count) == 1 && count == 0);
	CHECK(!memcmp(decoded, plaintext, 16));
	hx_os_crypto_bytes_destroy(&encrypted);
	EVP_CIPHER_CTX_free(native);
}

static void check_native_state(void)
{
	static const char bad_pem[] = "-----BEGIN PRIVATE KEY-----\nAAAA\n"
				      "-----END PRIVATE KEY-----\n";
	hx_string output = HX_STRING_INIT;
	hx_os_bytes_view input = {"abc", 3};
	hx_os_bytes_view bad = {bad_pem, sizeof(bad_pem) - 1};
	hx_os_crypto_key *key = NULL;
	hx_os_error error;
	char diagnostic[512];
	size_t required;
	unsigned long first, last;
	const char *data;
	int flags;

	CHECK(EVP_set_default_properties(NULL, "provider=hx_nonexistent") == 1);
	ERR_raise_data(ERR_LIB_USER, 101, "caller %d", 1);
	first = ERR_peek_error();
	ERR_raise(ERR_LIB_USER, 102);
	last = ERR_peek_last_error();
	CHECK(hx_os_crypto_sha256(NULL, input, &output, NULL) == HX_OS_OK);
	CHECK(hx_os_crypto_key_import_private(NULL, bad, &key, &error) ==
		HX_OS_SYNTAX);
	CHECK(!key && error.domain == HX_OS_ERROR_OPENSSL && error.native_code);
	CHECK(hx_os_error_message(&error, diagnostic, sizeof(diagnostic),
		      &required) == HX_OS_OK);
	CHECK(strstr(diagnostic, "error:") != NULL);
	CHECK(ERR_peek_error() == first && ERR_peek_last_error() == last);
	CHECK(ERR_get_error_all(NULL, NULL, NULL, &data, &flags) == first);
	CHECK((flags & ERR_TXT_STRING) && !strcmp(data, "caller 1"));
	CHECK(ERR_get_error() == last && ERR_get_error() == 0);
	CHECK(EVP_MD_fetch(NULL, "SHA256", NULL) == NULL);
	ERR_clear_error();
	CHECK(EVP_set_default_properties(NULL, "") == 1);
	hx_string_destroy(&output);
}

typedef struct crypto_worker {
	hx_os_crypto *crypto;
	hx_os_crypto_key *key;
} crypto_worker;

static void *HX_OS_CALL worker(void *user)
{
	crypto_worker *state = user;
	hx_string hash = HX_STRING_INIT;
	hx_os_crypto_bytes *signature = NULL;
	hx_os_bytes_view input = {"message", 7}, signed_bytes;
	int i;

	for (i = 0; i < 50; ++i) {
		CHECK(hx_os_crypto_sha256(state->crypto, input, &hash, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_crypto_sign(state->key, input, &signature, NULL) ==
			HX_OS_OK);
		signed_bytes.data = hx_os_crypto_bytes_data(signature);
		signed_bytes.size = hx_os_crypto_bytes_size(signature);
		CHECK(hx_os_crypto_verify(state->key, input, signed_bytes,
			      NULL) == HX_OS_OK);
		hx_os_crypto_bytes_destroy(&signature);
	}
	hx_string_destroy(&hash);
	/* Static OpenSSL on Windows has no DLL thread-exit notification.
	 * Other configurations exercise the native automatic cleanup path. */
#if defined(_WIN32) && defined(HX_TEST_STATIC_OPENSSL)
	hx_os_crypto_thread_cleanup();
#endif

	return NULL;
}

static void check_threads(void)
{
	crypto_worker state;
	hx_os_thread *threads[4];
	unsigned i;

	CHECK(hx_os_crypto_create(NULL, &state.crypto, NULL) == HX_OS_OK);
	CHECK(hx_os_crypto_key_generate(state.crypto, HX_OS_CRYPTO_ED25519,
		      &state.key, NULL) == HX_OS_OK);
	for (i = 0; i < 4; ++i)
		CHECK(hx_os_thread_create(NULL, worker, &state, &threads[i],
			      NULL) == HX_OS_OK);
	for (i = 0; i < 4; ++i) {
		CHECK(hx_os_thread_join_until(threads[i],
			      hx_os_deadline_after_ms(10000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&threads[i], NULL) == HX_OS_OK);
	}
	hx_os_crypto_key_destroy(&state.key);
	hx_os_crypto_release(state.crypto);
}

int main(void)
{
	check_rfc8032();
	check_envelope();
	check_native_state();
	check_threads();
	hx_os_crypto_thread_cleanup();

	return 0;
}
