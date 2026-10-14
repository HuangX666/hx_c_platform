#ifndef HX_C_PLATFORM_CPP_CRYPTO_DIGEST_HPP
#define HX_C_PLATFORM_CPP_CRYPTO_DIGEST_HPP

#include <hx_os_crypto.h>
#include <hx/Values.hpp>
#include <utility>

namespace hx {
class CryptoDigest {
public:
	CryptoDigest() noexcept
	    : m_state_(NULL)
	{
	}

	CryptoDigest(CryptoDigest &&other) noexcept
	    : m_state_(other.m_state_)
	{
		other.m_state_ = NULL;
	}

	CryptoDigest &operator=(CryptoDigest &&other) noexcept
	{
		std::swap(m_state_, other.m_state_);
		return *this;
	}

	CryptoDigest(const CryptoDigest &) = delete;
	CryptoDigest &operator=(const CryptoDigest &) = delete;

	~CryptoDigest() noexcept
	{
		(void)hx_os_crypto_digest_destroy(&m_state_, NULL);
	}

	static Result<CryptoDigest> create(
		unsigned algorithm = HX_OS_CRYPTO_SHA256,
		hx_os_crypto *engine = NULL) noexcept
	{
		CryptoDigest result;
		hx_os_error error = {};
		auto status = hx_os_crypto_digest_create(
			engine, algorithm, &result.m_state_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<CryptoDigest> hmac(hx_os_bytes_view key,
		unsigned algorithm = HX_OS_CRYPTO_SHA256,
		hx_os_crypto *engine = NULL) noexcept
	{
		CryptoDigest result;
		hx_os_error error = {};
		auto status = hx_os_crypto_hmac_create(
			engine, algorithm, key, &result.m_state_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	Result<void> update(const void *data, size_t bytes) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_crypto_digest_update(
			m_state_, hx_os_bytes_view{data, bytes}, &error);

		return detail::Status(status, error);
	}

	Result<void> update(StringView data) noexcept
	{
		return update(data.data(), data.size());
	}

	Result<String> finish(hx_os_context *context = NULL) noexcept
	{
		String output(context);
		hx_os_error error = {};
		auto status = hx_os_crypto_digest_finish(
			m_state_, output.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return output;
	}

	Result<void> reset() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_crypto_digest_reset(m_state_, &error);

		return detail::Status(status, error);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_crypto_digest_destroy(&m_state_, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_crypto_digest_state *m_state_;
};
} // namespace hx
#endif
