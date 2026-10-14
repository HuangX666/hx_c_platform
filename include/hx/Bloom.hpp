#ifndef HX_C_PLATFORM_CPP_BLOOM_HPP
#define HX_C_PLATFORM_CPP_BLOOM_HPP

#include <hx_bloom.h>
#include <hx/Status.hpp>

namespace hx {
class Bloom {
public:
	Bloom() noexcept
	    : m_bloom_(NULL)
	{
	}

	Bloom(Bloom &&other) noexcept
	    : m_bloom_(other.m_bloom_)
	{
		other.m_bloom_ = NULL;
	}

	Bloom &operator=(Bloom &&other) noexcept
	{
		std::swap(m_bloom_, other.m_bloom_);
		return *this;
	}

	Bloom(const Bloom &) = delete;
	Bloom &operator=(const Bloom &) = delete;

	~Bloom() noexcept
	{
		hx_os_bloom_destroy(&m_bloom_);
	}

	static Result<Bloom> create(size_t expectedItems, double rate = 0.01,
		size_t maxBytes = 0, hx_os_context *context = NULL) noexcept
	{
		Bloom result;
		hx_os_error error = {};
		auto status = hx_os_bloom_create(context, expectedItems, rate,
			maxBytes, &result.m_bloom_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	Result<void> insert(hx_os_bytes_view value) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_bloom_insert(m_bloom_, value, &error);
		return detail::Status(status, error);
	}

	Result<bool> contains(hx_os_bytes_view value) const noexcept
	{
		int maybe = 0;
		hx_os_error error = {};
		auto status =
			hx_os_bloom_contains(m_bloom_, value, &maybe, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return maybe != 0;
	}

	void clear() noexcept
	{
		if (m_bloom_)
			hx_os_bloom_clear(m_bloom_);
	}

	size_t size() const noexcept
	{
		return m_bloom_ ? hx_os_bloom_size_bytes(m_bloom_) : 0;
	}

private:
	hx_os_bloom *m_bloom_;
};
} // namespace hx
#endif
