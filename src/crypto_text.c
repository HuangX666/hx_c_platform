#include "crypto_private.h"

#if HX_OS_ENABLE_CRYPTO
static int hx_os_crypto_base64_value(unsigned char value)
{
	if (value >= 'A' && value <= 'Z')
		return value - 'A';
	if (value >= 'a' && value <= 'z')
		return value - 'a' + 26;
	if (value >= '0' && value <= '9')
		return value - '0' + 52;

	return value == '+' ? 62 : value == '/' ? 63 : -1;
}

static hx_os_status hx_os_crypto_base64_read(hx_os_context *context,
	hx_string_view input, hx_os_crypto_bytes **out, hx_os_error *error)
{
	const unsigned char *data = (const unsigned char *)input.data;
	size_t i, padding = 0;
	hx_os_crypto_bytes *bytes;
	int decoded, tail;

	*out = NULL;
	if (!input.size)
		return hx_os_i_error(error, HX_OS_TRUNCATED_INPUT, 0, 0);
	if (input.size % 4)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	if (data[input.size - 1] == '=')
		++padding;
	if (data[input.size - 2] == '=')
		++padding;
	if (data[input.size - 2] == '=' && data[input.size - 1] != '=')
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	for (i = 0; i < input.size - padding; ++i)
		if (hx_os_crypto_base64_value(data[i]) < 0)
			return hx_os_i_error(
				error, HX_OS_INVALID_ENCODING, 0, 0);
	tail = hx_os_crypto_base64_value(data[input.size - padding - 1]);
	if ((padding == 1 && (tail & 3)) || (padding == 2 && (tail & 15)))
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	bytes = hx_os_i_crypto_bytes(context, input.size / 4 * 3, error);
	if (!bytes)
		return HX_OS_NO_MEMORY;
	decoded = EVP_DecodeBlock(bytes->data, data, (int)input.size);
	if (decoded < 0 || (size_t)decoded < padding) {
		hx_os_crypto_bytes_destroy(&bytes);

		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	}
	bytes->size = (size_t)decoded - padding;
	*out = bytes;

	return HX_OS_OK;
}

static hx_os_status hx_os_crypto_text(const char *password,
	hx_string_view input, int decrypt, hx_string *out, hx_os_error *error)
{
	const hx_os_crypto_options limits = HX_OS_CRYPTO_OPTIONS_INIT;
	hx_os_crypto *crypto = NULL;
	hx_os_crypto_secret secret;
	hx_os_crypto_bytes *data = NULL, *result = NULL, *encoded = NULL;
	hx_os_bytes_view view = {input.data, input.size};
	hx_string_view text;
	size_t maximum, invalid, capacity;
	int count;
	hx_os_status status;

	if (!out || (!input.data && input.size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	maximum = decrypt
		? ((limits.max_memory_bytes + HX_OS_I_CRYPTO_OVERHEAD + 2) / 3 *
			  4)
		: limits.max_memory_bytes;
	if (input.size > maximum)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	status = hx_os_i_crypto_password(password, &secret, error);
	if (status == HX_OS_OK && !decrypt) {
		status = hx_os_text_validate_utf8(
			input.data, input.size, &invalid);
		if (status != HX_OS_OK)
			return hx_os_i_error(error, status, 0, 0);
	}
	if (status == HX_OS_OK)
		status = hx_os_crypto_create(out->context, &crypto, error);
	if (status == HX_OS_OK && decrypt) {
		status = hx_os_crypto_base64_read(
			out->context, input, &data, error);
		if (status == HX_OS_OK) {
			view.data = data->data;
			view.size = data->size;
		}
	}
	if (status == HX_OS_OK)
		status = decrypt ? hx_os_crypto_decrypt(crypto, &secret, view,
					   &result, error)
				 : hx_os_crypto_encrypt(crypto, &secret, view,
					   &result, error);
	if (status == HX_OS_OK && !decrypt) {
		capacity = (result->size + 2) / 3 * 4;
		encoded =
			hx_os_i_crypto_bytes(out->context, capacity + 1, error);
		if (!encoded)
			status = HX_OS_NO_MEMORY;
		else {
			count = EVP_EncodeBlock(
				encoded->data, result->data, (int)result->size);
			if (count < 0 || (size_t)count != capacity)
				status = hx_os_i_error(
					error, HX_OS_IO_ERROR, 0, 0);
			else {
				text.data = (const char *)encoded->data;
				text.size = capacity;
				status = hx_string_assign(out, text, error);
			}
		}
	}
	else if (status == HX_OS_OK) {
		text.data = (const char *)result->data;
		text.size = result->size;
		status = hx_string_assign(out, text, error);
	}
	hx_os_crypto_bytes_destroy(&data);
	hx_os_crypto_bytes_destroy(&result);
	hx_os_crypto_bytes_destroy(&encoded);
	hx_os_crypto_release(crypto);

	return status;
}
#endif

hx_os_status hx_os_crypto_encrypt_text(const char *password,
	hx_string_view input, hx_string *out, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	return hx_os_crypto_text(password, input, 0, out, error);
#else
	(void)password;
	(void)input;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_crypto_decrypt_text(const char *password,
	hx_string_view input, hx_string *out, hx_os_error *error)
{
#if HX_OS_ENABLE_CRYPTO
	return hx_os_crypto_text(password, input, 1, out, error);
#else
	(void)password;
	(void)input;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
