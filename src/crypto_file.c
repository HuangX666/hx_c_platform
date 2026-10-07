#include "crypto_private.h"
#include "file_utils_private.h"

#if HX_OS_ENABLE_CRYPTO && HX_OS_ENABLE_FILE_UTILS
typedef struct hx_os_crypto_file {
	hx_os_crypto *crypto;
	hx_os_file *input;
	EVP_CIPHER_CTX *cipher;
	uint64_t remaining;
	unsigned char header[HX_OS_I_CRYPTO_HEADER];
	int decrypt;
} hx_os_crypto_file;

static hx_os_status hx_os_crypto_file_read(
	hx_os_file *file, unsigned char *data, size_t bytes, hx_os_error *error)
{
	size_t done = 0, count;
	hx_os_status status;

	while (done < bytes) {
		status = hx_os_file_read_some(
			file, data + done, bytes - done, &count, error);
		if (status == HX_OS_EOF)
			status = hx_os_i_error(
				error, HX_OS_TRUNCATED_INPUT, 0, 0);
		if (status != HX_OS_OK)
			return status;
		if (!count)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		done += count;
	}

	return HX_OS_OK;
}

static hx_os_status hx_os_crypto_file_write(
	hx_os_file *output, void *user, hx_os_error *error)
{
	hx_os_crypto_file *state = user;
	unsigned char tag[HX_OS_I_CRYPTO_TAG], tail[16];
	unsigned char *buffer = hx_os_i_alloc(state->crypto->context, 65536, 0);
	size_t bytes, done;
	int count = 0;
	hx_os_status status = HX_OS_OK;

	if (!buffer)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	if (!state->decrypt)
		status = hx_os_file_write_all(output, state->header,
			sizeof(state->header), &done, error);
	while (status == HX_OS_OK && state->remaining) {
		bytes = state->remaining > 32768 ? 32768
						 : (size_t)state->remaining;
		status = hx_os_crypto_file_read(
			state->input, buffer, bytes, error);
		if (status == HX_OS_OK &&
			EVP_CipherUpdate(state->cipher, buffer + 32768, &count,
				buffer, (int)bytes) != 1)
			status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
		if (status == HX_OS_OK && (size_t)count != bytes)
			status = hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		if (status == HX_OS_OK) {
			status = hx_os_file_write_all(
				output, buffer + 32768, bytes, &done, error);
			state->remaining -= bytes;
		}
	}
	if (status == HX_OS_OK && state->decrypt) {
		status = hx_os_crypto_file_read(
			state->input, tag, sizeof(tag), error);
		if (status == HX_OS_OK &&
			EVP_CIPHER_CTX_ctrl(state->cipher,
				EVP_CTRL_AEAD_SET_TAG, sizeof(tag), tag) != 1)
			status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
	}
	if (status == HX_OS_OK &&
		EVP_CipherFinal_ex(state->cipher, tail, &count) != 1)
		status = hx_os_i_crypto_fail(error,
			state->decrypt ? HX_OS_AUTH_FAILED : HX_OS_IO_ERROR);
	if (status == HX_OS_OK && count)
		status = hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
	if (status == HX_OS_OK && !state->decrypt) {
		if (EVP_CIPHER_CTX_ctrl(state->cipher, EVP_CTRL_AEAD_GET_TAG,
			    sizeof(tag), tag) != 1)
			status = hx_os_i_crypto_fail(error, HX_OS_IO_ERROR);
		else
			status = hx_os_file_write_all(
				output, tag, sizeof(tag), &done, error);
	}
	hx_os_secure_zero(buffer, 65536);
	hx_os_secure_zero(tail, sizeof(tail));
	hx_os_memory_free(buffer);
	{
		hx_os_status closed = hx_os_file_close(
			&state->input, status == HX_OS_OK ? error : NULL);

		if (status == HX_OS_OK)
			status = closed;
	}

	return status;
}

static hx_os_status hx_os_crypto_file_transform(hx_os_crypto *crypto,
	const char *input_path, const char *output_path,
	const hx_os_crypto_secret *secret, const hx_os_crypto_options *settings,
	int decrypt, hx_os_file_save_result *result, hx_os_error *error)
{
	hx_os_crypto_options options;
	hx_os_crypto_scope scope;
	hx_os_crypto_file state;
	hx_os_file_info info;
	hx_os_status status;

	memset(&state, 0, sizeof(state));
	if (result)
		memset(result, 0, sizeof(*result));
	status = hx_os_i_crypto_options(settings, &options, error);
	if (status != HX_OS_OK)
		return status;
	if (!(options.flags & HX_OS_CRYPTO_REPLACE)) {
		status = hx_os_path_stat(output_path, 0, &info, error);
		if (status == HX_OS_OK)
			return hx_os_i_error(error, HX_OS_EXISTS, 0, 0);
		if (status != HX_OS_NOT_FOUND)
			return status;
	}
	status = hx_os_i_crypto_enter(crypto, NULL, &scope, error);
	if (status != HX_OS_OK)
		return status;
	state.crypto = scope.crypto;
	state.decrypt = decrypt;
	status = hx_os_file_open(scope.crypto->context, input_path,
		HX_OS_FILE_READ, &state.input, error);
	if (status == HX_OS_OK)
		status = hx_os_file_size(state.input, &state.remaining, error);
	if (status == HX_OS_OK && decrypt) {
		if (state.remaining < HX_OS_I_CRYPTO_OVERHEAD)
			status = hx_os_i_error(
				error, HX_OS_TRUNCATED_INPUT, 0, 0);
		else {
			state.remaining -= HX_OS_I_CRYPTO_OVERHEAD;
			status = hx_os_crypto_file_read(state.input,
				state.header, sizeof(state.header), error);
		}
	}
	if (status == HX_OS_OK && state.remaining > options.max_file_bytes)
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if (status == HX_OS_OK)
		status = hx_os_i_crypto_cipher(scope.crypto, secret, &options,
			decrypt, state.header, &state.cipher, error);
	if (status == HX_OS_OK)
		status = hx_os_i_file_save_callback(scope.crypto->context,
			output_path,
			(options.flags & HX_OS_CRYPTO_REPLACE) != 0, decrypt,
			hx_os_crypto_file_write, &state, result, error);
	if (state.input)
		(void)hx_os_file_close(&state.input, NULL);
	EVP_CIPHER_CTX_free(state.cipher);
	hx_os_i_crypto_leave(&scope);

	return status;
}
#endif

hx_os_status hx_os_crypto_encrypt_file_with_options(hx_os_crypto *crypto,
	const char *input_path, const char *output_path,
	const hx_os_crypto_secret *secret, const hx_os_crypto_options *options,
	hx_os_file_save_result *result, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO && HX_OS_ENABLE_FILE_UTILS
	return hx_os_crypto_file_transform(crypto, input_path, output_path,
		secret, options, 0, result, error);
#else
	(void)crypto;
	(void)input_path;
	(void)output_path;
	(void)secret;
	(void)options;
	if (result)
		memset(result, 0, sizeof(*result));

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_decrypt_file_with_options(hx_os_crypto *crypto,
	const char *input_path, const char *output_path,
	const hx_os_crypto_secret *secret, const hx_os_crypto_options *options,
	hx_os_file_save_result *result, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO && HX_OS_ENABLE_FILE_UTILS
	return hx_os_crypto_file_transform(crypto, input_path, output_path,
		secret, options, 1, result, error);
#else
	(void)crypto;
	(void)input_path;
	(void)output_path;
	(void)secret;
	(void)options;
	if (result)
		memset(result, 0, sizeof(*result));

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_encrypt_file(const char *input_path,
	const char *output_path, const char *password, hx_os_error *error)
{
	hx_os_crypto_secret secret;
	hx_os_status status;

	if (!HX_OS_ENABLE_CRYPTO || !HX_OS_ENABLE_FILE_UTILS)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	status = hx_os_i_crypto_password(password, &secret, error);

	return status == HX_OS_OK
		? hx_os_crypto_encrypt_file_with_options(NULL, input_path,
			  output_path, &secret, NULL, NULL, error)
		: status;
}

hx_os_status hx_os_crypto_decrypt_file(const char *input_path,
	const char *output_path, const char *password, hx_os_error *error)
{
	hx_os_crypto_secret secret;
	hx_os_status status;

	if (!HX_OS_ENABLE_CRYPTO || !HX_OS_ENABLE_FILE_UTILS)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	status = hx_os_i_crypto_password(password, &secret, error);

	return status == HX_OS_OK
		? hx_os_crypto_decrypt_file_with_options(NULL, input_path,
			  output_path, &secret, NULL, NULL, error)
		: status;
}
