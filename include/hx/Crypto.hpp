#ifndef HX_C_PLATFORM_CPP_CRYPTO_HPP
#define HX_C_PLATFORM_CPP_CRYPTO_HPP

#include <hx_os_crypto.h>
#include <hx/Values.hpp>

namespace hx {

class CryptoBytes {
public:
	CryptoBytes() noexcept
	    : m_bytes_(NULL)
	{
	}

	static CryptoBytes adopt(hx_os_crypto_bytes *&owner) noexcept
	{
		CryptoBytes result;

		result.m_bytes_ = owner;
		owner = NULL;

		return result;
	}

	CryptoBytes(CryptoBytes &&other) noexcept
	    : m_bytes_(other.m_bytes_)
	{
		other.m_bytes_ = NULL;
	}

	CryptoBytes &operator=(CryptoBytes &&other) noexcept
	{
		if (this != &other) {
			hx_os_crypto_bytes_destroy(&m_bytes_);
			m_bytes_ = other.m_bytes_;
			other.m_bytes_ = NULL;
		}

		return *this;
	}

	CryptoBytes(const CryptoBytes &) = delete;
	CryptoBytes &operator=(const CryptoBytes &) = delete;

	~CryptoBytes() noexcept
	{
		hx_os_crypto_bytes_destroy(&m_bytes_);
	}

	const void *data() const noexcept
	{
		return hx_os_crypto_bytes_data(m_bytes_);
	}

	size_t size() const noexcept
	{
		return hx_os_crypto_bytes_size(m_bytes_);
	}

	hx_os_bytes_view view() const noexcept
	{
		return {data(), size()};
	}

private:
	hx_os_crypto_bytes *m_bytes_;
};

class CryptoKey {
public:
	CryptoKey() noexcept
	    : m_key_(NULL)
	{
	}

	CryptoKey(CryptoKey &&other) noexcept
	    : m_key_(other.m_key_)
	{
		other.m_key_ = NULL;
	}

	CryptoKey &operator=(CryptoKey &&other) noexcept
	{
		if (this != &other) {
			hx_os_crypto_key_destroy(&m_key_);
			m_key_ = other.m_key_;
			other.m_key_ = NULL;
		}

		return *this;
	}

	CryptoKey(const CryptoKey &) = delete;
	CryptoKey &operator=(const CryptoKey &) = delete;

	~CryptoKey() noexcept
	{
		hx_os_crypto_key_destroy(&m_key_);
	}

	static Result<CryptoKey> generate(
		unsigned algorithm = HX_OS_CRYPTO_ED25519,
		hx_os_crypto *engine = NULL) noexcept
	{
		CryptoKey result;
		hx_os_error error = {};

		auto status = hx_os_crypto_key_generate(
			engine, algorithm, &result.m_key_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<CryptoKey> importPublic(
		StringView pem, hx_os_crypto *engine = NULL) noexcept
	{
		CryptoKey result;
		hx_os_error error = {};

		auto status = hx_os_crypto_key_import_public(engine,
			{pem.data(), pem.size()}, &result.m_key_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<CryptoKey> importPrivate(
		hx_os_bytes_view pem, hx_os_crypto *engine = NULL) noexcept
	{
		CryptoKey result;
		hx_os_error error = {};

		auto status = hx_os_crypto_key_import_private(
			engine, pem, &result.m_key_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	Result<String> exportPublic(
		hx_os_context *context = NULL) const noexcept
	{
		String result(context);
		hx_os_error error = {};

		auto status = hx_os_crypto_key_export_public(
			m_key_, result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	/* Sensitive unencrypted PEM. Encrypt before storing private material.
	 */
	Result<CryptoBytes> exportPrivate() const noexcept
	{
		hx_os_crypto_bytes *result = NULL;
		hx_os_error error = {};

		auto status = hx_os_crypto_key_export_private(
			m_key_, &result, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return CryptoBytes::adopt(result);
	}

	Result<CryptoBytes> sign(hx_os_bytes_view message) const noexcept
	{
		hx_os_crypto_bytes *result = NULL;
		hx_os_error error = {};

		auto status =
			hx_os_crypto_sign(m_key_, message, &result, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return CryptoBytes::adopt(result);
	}

	Result<void> verify(hx_os_bytes_view message,
		hx_os_bytes_view signature) const noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_crypto_verify(m_key_, message, signature, &error);

		return detail::Status(status, error);
	}

	hx_os_crypto_key *native() const noexcept
	{
		return m_key_;
	}

private:
	hx_os_crypto_key *m_key_;
};

/* Common operations need no owned engine. For repeated native C calls, use
 * create() and native(); children retain the reusable private engine. */
class Crypto {
public:
	Crypto() noexcept
	    : m_crypto_(NULL)
	{
	}

	Crypto(Crypto &&other) noexcept
	    : m_crypto_(other.m_crypto_)
	{
		other.m_crypto_ = NULL;
	}

	Crypto &operator=(Crypto &&other) noexcept
	{
		if (this != &other) {
			hx_os_crypto_release(m_crypto_);
			m_crypto_ = other.m_crypto_;
			other.m_crypto_ = NULL;
		}

		return *this;
	}

	Crypto(const Crypto &) = delete;
	Crypto &operator=(const Crypto &) = delete;

	~Crypto() noexcept
	{
		hx_os_crypto_release(m_crypto_);
	}

	static Result<Crypto> create(hx_os_context *context = NULL) noexcept
	{
		Crypto result;
		hx_os_error error = {};

		auto status =
			hx_os_crypto_create(context, &result.m_crypto_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<String> sha256(
		hx_os_bytes_view input, hx_os_crypto *engine = NULL) noexcept
	{
		String result;
		hx_os_error error = {};

		auto status = hx_os_crypto_sha256(
			engine, input, result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<String> sha256(StringView input) noexcept
	{
		return sha256(hx_os_bytes_view{input.data(), input.size()});
	}

	static Result<String> sha256File(
		const char *path, hx_os_crypto *engine = NULL) noexcept
	{
		String result;
		hx_os_error error = {};

		auto status = hx_os_crypto_sha256_file(
			engine, path, result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<String> hmac(hx_os_bytes_view key, hx_os_bytes_view input,
		unsigned algorithm = HX_OS_CRYPTO_SHA256,
		hx_os_crypto *engine = NULL) noexcept
	{
		String result;
		hx_os_error error = {};

		auto status = hx_os_crypto_hmac(
			engine, algorithm, key, input, result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<void> verifyHmac(hx_os_bytes_view key,
		hx_os_bytes_view input, StringView expected,
		unsigned algorithm = HX_OS_CRYPTO_SHA256,
		hx_os_crypto *engine = NULL) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_crypto_hmac_verify(engine, algorithm, key,
			input, expected.native(), &error);

		return detail::Status(status, error);
	}

	static Result<String> encryptText(
		const char *password, StringView input) noexcept
	{
		return text(password, input, false);
	}

	static Result<String> decryptText(
		const char *password, StringView input) noexcept
	{
		return text(password, input, true);
	}

	static Result<CryptoBytes> encrypt(const hx_os_crypto_secret &secret,
		hx_os_bytes_view input,
		const hx_os_crypto_options *options = NULL,
		hx_os_crypto *engine = NULL) noexcept
	{
		hx_os_crypto_bytes *result = NULL;
		hx_os_error error = {};

		auto status = hx_os_crypto_encrypt_with_options(
			engine, &secret, input, options, &result, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return CryptoBytes::adopt(result);
	}

	static Result<CryptoBytes> decrypt(const hx_os_crypto_secret &secret,
		hx_os_bytes_view input,
		const hx_os_crypto_options *options = NULL,
		hx_os_crypto *engine = NULL) noexcept
	{
		hx_os_crypto_bytes *result = NULL;
		hx_os_error error = {};

		auto status = hx_os_crypto_decrypt_with_options(
			engine, &secret, input, options, &result, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return CryptoBytes::adopt(result);
	}

	static Result<void> encryptFile(const char *input, const char *output,
		const char *password) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_crypto_encrypt_file(
			input, output, password, &error);

		return detail::Status(status, error);
	}

	static Result<void> decryptFile(const char *input, const char *output,
		const char *password) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_crypto_decrypt_file(
			input, output, password, &error);

		return detail::Status(status, error);
	}

	hx_os_crypto *native() const noexcept
	{
		return m_crypto_;
	}

private:
	hx_os_crypto *m_crypto_;

	static Result<String> text(
		const char *password, StringView input, bool decrypt) noexcept
	{
		String result;
		hx_os_error error = {};
		auto status = decrypt
			? hx_os_crypto_decrypt_text(password, input.native(),
				  result.native(), &error)
			: hx_os_crypto_encrypt_text(password, input.native(),
				  result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}
};
} // namespace hx
#endif
