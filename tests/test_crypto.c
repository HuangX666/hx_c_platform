#include "utility_test.h"

static hx_os_bytes_view span(const void *data, size_t size)
{
	hx_os_bytes_view result = {data, size};

	return result;
}

static hx_os_bytes_view owned(const hx_os_crypto_bytes *bytes)
{
	return span(
		hx_os_crypto_bytes_data(bytes), hx_os_crypto_bytes_size(bytes));
}

static void check_hash(void)
{
	hx_string output = HX_STRING_INIT;
	unsigned char key[20];

	memset(key, 0x0b, sizeof(key));
	CHECK(hx_os_crypto_sha256(NULL, span("abc", 3), &output, NULL) ==
		HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&output),
		"ba7816bf8f01cfea414140de5dae2223b0"
		"0361a396177a9cb410ff61f20015ad"));
	CHECK(hx_os_crypto_hash(NULL, HX_OS_CRYPTO_SHA512, span("", 0), &output,
		      NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&output),
		"cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9"
		"ce"
		"47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da"
		"3e"));
	CHECK(hx_os_crypto_hmac(NULL, HX_OS_CRYPTO_SHA256,
		      span(key, sizeof(key)), span("Hi There", 8), &output,
		      NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&output),
		"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cf"
		"f7"));
	CHECK(hx_os_crypto_hmac_verify(NULL, HX_OS_CRYPTO_SHA256,
		      span(key, sizeof(key)), span("Hi There", 8),
		      hx_string_view_of(&output), NULL) == HX_OS_OK);
	hx_string_upper_ascii_in_place(&output);
	CHECK(hx_os_crypto_hmac_verify(NULL, HX_OS_CRYPTO_SHA256,
		      span(key, sizeof(key)), span("Hi There", 8),
		      hx_string_view_of(&output), NULL) == HX_OS_OK);
	CHECK(hx_os_crypto_hmac_verify(NULL, HX_OS_CRYPTO_SHA256,
		      span(key, sizeof(key)), span("changed", 7),
		      hx_string_view_of(&output), NULL) == HX_OS_AUTH_FAILED);
	CHECK(hx_os_crypto_hash(NULL, 0, span(NULL, 0), &output, NULL) ==
		HX_OS_INVALID);
	CHECK(hx_os_crypto_hmac(NULL, HX_OS_CRYPTO_SHA256, span(NULL, 0),
		      span(NULL, 0), &output, NULL) == HX_OS_OK);
	hx_string_destroy(&output);
}

static void check_cipher(void)
{
	unsigned char key[32] = {0}, other[32], message[] = {0, 0xff, 7, 0x80};
	unsigned char changed[64];
	hx_os_crypto_secret secret = {
		span(key, sizeof(key)), HX_OS_CRYPTO_KEY_256};
	hx_os_crypto_bytes *cipher = NULL, *plain = NULL;
	hx_os_crypto_options options = HX_OS_CRYPTO_OPTIONS_INIT;
	hx_os_crypto *crypto = NULL;
	size_t i;

	CHECK(hx_os_crypto_create(NULL, &crypto, NULL) == HX_OS_OK);
	CHECK(hx_os_crypto_random_key(crypto, key, NULL) == HX_OS_OK);
	CHECK(hx_os_crypto_random_key(crypto, other, NULL) == HX_OS_OK);
	CHECK(memcmp(key, other, sizeof(key)));
	CHECK(hx_os_crypto_encrypt(crypto, &secret,
		      span(message, sizeof(message)), &cipher,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_crypto_bytes_size(cipher) == sizeof(changed));
	CHECK(hx_os_crypto_decrypt(crypto, &secret, owned(cipher), &plain,
		      NULL) == HX_OS_OK);
	CHECK(owned(plain).size == sizeof(message));
	CHECK(!memcmp(owned(plain).data, message, sizeof(message)));
	hx_os_crypto_bytes_destroy(&plain);

	for (i = 0; i < sizeof(changed); ++i) {
		memcpy(changed, owned(cipher).data, sizeof(changed));
		changed[i] ^= 1;
		CHECK(hx_os_crypto_decrypt(crypto, &secret,
			      span(changed, sizeof(changed)), &plain,
			      NULL) != HX_OS_OK &&
			!plain);
	}
	options.max_memory_bytes = 3;
	CHECK(hx_os_crypto_decrypt_with_options(crypto, &secret, owned(cipher),
		      &options, &plain, NULL) == HX_OS_LIMIT &&
		!plain);
	secret.bytes = span(other, sizeof(other));
	CHECK(hx_os_crypto_decrypt(crypto, &secret, owned(cipher), &plain,
		      NULL) == HX_OS_AUTH_FAILED &&
		!plain);
	hx_os_crypto_bytes_destroy(&cipher);
	CHECK(hx_os_crypto_encrypt(crypto, &secret, span(NULL, 0), &cipher,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_crypto_decrypt(crypto, &secret, owned(cipher), &plain,
		      NULL) == HX_OS_OK &&
		owned(plain).size == 0);
	hx_os_crypto_bytes_destroy(&plain);
	hx_os_crypto_bytes_destroy(&cipher);
	hx_os_crypto_release(crypto);
}

static void check_text(void)
{
	hx_string encrypted = HX_STRING_INIT, plain = HX_STRING_INIT;
	hx_string_view message = {"\xe4\xb8\xad\xe6\x96\x87\0x", 8};
	hx_string_view bad = {"AAA=A===", 8};

	CHECK(hx_string_assign_cstr(&plain, "unchanged", NULL) == HX_OS_OK);
	CHECK(hx_os_crypto_encrypt_text(
		      "test password", message, &encrypted, NULL) == HX_OS_OK);
	CHECK(hx_os_crypto_decrypt_text("wrong password",
		      hx_string_view_of(&encrypted), &plain,
		      NULL) == HX_OS_AUTH_FAILED);
	CHECK(!strcmp(hx_string_c_str(&plain), "unchanged"));
	CHECK(hx_os_crypto_decrypt_text("test password",
		      hx_string_view_of(&encrypted), &plain, NULL) == HX_OS_OK);
	CHECK(plain.size == message.size &&
		!memcmp(hx_string_c_str(&plain), message.data, message.size));
	CHECK(hx_os_crypto_decrypt_text("test password", bad, &plain, NULL) ==
		HX_OS_INVALID_ENCODING);
	bad.data = "AA=A";
	bad.size = 4;
	CHECK(hx_os_crypto_decrypt_text("test password", bad, &plain, NULL) ==
		HX_OS_INVALID_ENCODING);
	hx_string_destroy(&plain);
	hx_string_destroy(&encrypted);
}

static void check_keys(void)
{
	hx_os_crypto_key *key = NULL, *public_key = NULL, *copy = NULL;
	hx_os_crypto_bytes *private_pem = NULL, *signature = NULL;
	hx_string public_pem = HX_STRING_INIT;
	unsigned algorithms[] = {HX_OS_CRYPTO_ED25519, HX_OS_CRYPTO_RSA_3072};
	size_t i;

	for (i = 0; i < sizeof(algorithms) / sizeof(*algorithms); ++i) {
		CHECK(hx_os_crypto_key_generate(
			      NULL, algorithms[i], &key, NULL) == HX_OS_OK);
		CHECK(hx_os_crypto_key_export_public(key, &public_pem, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_crypto_key_import_public(NULL,
			      span(hx_string_c_str(&public_pem),
				      public_pem.size),
			      &public_key, NULL) == HX_OS_OK);
		CHECK(hx_os_crypto_key_export_private(
			      key, &private_pem, NULL) == HX_OS_OK);
		CHECK(hx_os_crypto_key_import_private(NULL, owned(private_pem),
			      &copy, NULL) == HX_OS_OK);
		hx_os_crypto_bytes_destroy(&private_pem);
		hx_os_crypto_key_destroy(&key);
		CHECK(hx_os_crypto_sign(copy, span("payload", 7), &signature,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_crypto_verify(public_key, span("payload", 7),
			      owned(signature), NULL) == HX_OS_OK);
		CHECK(hx_os_crypto_verify(public_key, span("tampered", 8),
			      owned(signature), NULL) == HX_OS_AUTH_FAILED);
		hx_os_crypto_bytes_destroy(&signature);
		CHECK(hx_os_crypto_sign(public_key, span("", 0), &signature,
			      NULL) == HX_OS_ACCESS_DENIED &&
			!signature);
		CHECK(hx_os_crypto_key_export_private(public_key, &private_pem,
			      NULL) == HX_OS_ACCESS_DENIED &&
			!private_pem);
		hx_os_crypto_key_destroy(&public_key);
		hx_os_crypto_key_destroy(&copy);
	}
	hx_string_destroy(&public_pem);
}

static void check_files(void)
{
	char input[128], output[128], decoded[128];
	unsigned char data[70000], key[32] = {7}, changed;
	hx_os_crypto_secret secret = {
		span(key, sizeof(key)), HX_OS_CRYPTO_KEY_256};
	hx_os_crypto_options options = HX_OS_CRYPTO_OPTIONS_INIT;
	hx_os_file_save_result published;
	hx_string before = HX_STRING_INIT, after = HX_STRING_INIT;
	hx_os_file *file = NULL;
	size_t done;
	uint64_t id = hx_os_monotonic_ns();
	hx_os_status status;

	if (!(hx_os_capabilities() & HX_OS_CAP_FILE_UTILS))
		return;
	memset(data, 0xad, sizeof(data));
	(void)snprintf(input, sizeof(input), "crypto-%llu-%s.bin",
		(unsigned long long)id, "\xe4\xb8\xad");
	(void)snprintf(output, sizeof(output), "crypto-%llu.enc",
		(unsigned long long)id);
	(void)snprintf(decoded, sizeof(decoded), "crypto-%llu.dec",
		(unsigned long long)id);
	CHECK(hx_os_file_save(input, data, sizeof(data), 0, NULL) == HX_OS_OK);
	CHECK(hx_os_crypto_sha256_file(NULL, input, &before, NULL) == HX_OS_OK);
	CHECK(hx_os_crypto_encrypt_file_with_options(NULL, input, output,
		      &secret, &options, &published, NULL) == HX_OS_OK &&
		published.published);
	status = hx_os_crypto_decrypt_file_with_options(
		NULL, output, decoded, &secret, &options, &published, NULL);
	CHECK(status == HX_OS_OK || status == HX_OS_NOT_SUPPORTED);
	if (status == HX_OS_NOT_SUPPORTED)
		puts("SKIP: private-file permissions unavailable on this "
		     "filesystem");
	else {
		CHECK(published.published);
		CHECK(hx_os_crypto_sha256_file(NULL, decoded, &after, NULL) ==
			HX_OS_OK);
		CHECK(!strcmp(
			hx_string_c_str(&before), hx_string_c_str(&after)));
		CHECK(hx_os_crypto_decrypt_file_with_options(NULL, output,
			      decoded, &secret, NULL, NULL,
			      NULL) == HX_OS_EXISTS);
		CHECK(hx_os_file_open(NULL, output,
			      HX_OS_FILE_READ | HX_OS_FILE_WRITE, &file,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_file_read_at(file, 44, &changed, 1, &done, NULL) ==
			HX_OS_OK);
		changed ^= 1;
		CHECK(hx_os_file_write_at(file, 44, &changed, 1, &done, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
		options.flags = HX_OS_CRYPTO_REPLACE;
		CHECK(hx_os_crypto_decrypt_file_with_options(NULL, output,
			      decoded, &secret, &options, &published,
			      NULL) == HX_OS_AUTH_FAILED);
		CHECK(!published.published);
		CHECK(hx_os_crypto_sha256_file(NULL, decoded, &after, NULL) ==
			HX_OS_OK);
		CHECK(!strcmp(
			hx_string_c_str(&before), hx_string_c_str(&after)));
		CHECK(hx_os_file_remove(decoded, NULL) == HX_OS_OK);
	}
	CHECK(hx_os_file_remove(input, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(output, NULL) == HX_OS_OK);
	hx_string_destroy(&before);
	hx_string_destroy(&after);
}

static void check_failure(void)
{
	test_memory memory = {0};
	hx_os_context *context = test_context(&memory);
	hx_os_crypto *crypto = NULL;
	hx_os_crypto_bytes *result = NULL;
	unsigned char key[32] = {0};
	hx_os_crypto_secret secret = {
		span(key, sizeof(key)), HX_OS_CRYPTO_KEY_256};

	memory.fail = memory.calls + 1;
	CHECK(hx_os_crypto_create(context, &crypto, NULL) == HX_OS_NO_MEMORY &&
		!crypto);
	memory.fail = 0;
	CHECK(hx_os_crypto_create(context, &crypto, NULL) == HX_OS_OK);
	memory.fail = memory.calls + 1;
	CHECK(hx_os_crypto_encrypt(crypto, &secret, span("abc", 3), &result,
		      NULL) == HX_OS_NO_MEMORY &&
		!result);
	hx_os_crypto_release(crypto);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(memory.live == 0);
}

typedef struct reentry_memory {
	test_memory memory;
	int probing, calls;
} reentry_memory;

static void *HX_OS_CALL reentry_allocate(size_t bytes, void *user)
{
	reentry_memory *state = user;
	hx_os_crypto *nested = NULL;

	if (state->probing) {
		CHECK(hx_os_crypto_create(NULL, &nested, NULL) ==
			HX_OS_DEADLOCK);
		CHECK(!nested);
		++state->calls;
	}

	return test_allocate(bytes, &state->memory);
}

static void check_reentry(void)
{
	reentry_memory state = {0};
	hx_os_context_options options;
	hx_os_context *context = NULL;
	hx_os_crypto *crypto = NULL;
	hx_os_crypto_bytes *output = NULL;
	unsigned char key[32] = {0};
	hx_os_crypto_secret secret = {{key, sizeof(key)}, HX_OS_CRYPTO_KEY_256};

	hx_os_context_options_init(&options);
	options.allocator.allocate = reentry_allocate;
	options.allocator.deallocate = test_deallocate;
	options.allocator.user = &state;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);
	state.probing = 1;
	CHECK(hx_os_crypto_create(context, &crypto, NULL) == HX_OS_OK);
	CHECK(hx_os_crypto_encrypt(crypto, &secret, span("test", 4), &output,
		      NULL) == HX_OS_OK);
	CHECK(state.calls >= 2);
	state.probing = 0;
	hx_os_crypto_bytes_destroy(&output);
	hx_os_crypto_release(crypto);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(state.memory.live == 0);
}

int main(void)
{
	if (!hx_os_crypto_available()) {
		hx_os_crypto *crypto = NULL;
		hx_string text = HX_STRING_INIT;

		CHECK(hx_os_crypto_create(NULL, &crypto, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_crypto_sha256(NULL, span("", 0), &text, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(!crypto && !text.size);

		return 0;
	}
	check_hash();
	check_cipher();
	check_text();
	check_keys();
	check_files();
	check_failure();
	check_reentry();
	hx_os_crypto_thread_cleanup();

	return 0;
}
