#ifndef HX_C_PLATFORM_CPP_BITSET_HPP
#define HX_C_PLATFORM_CPP_BITSET_HPP

#include <hx_bitset.h>
#include <hx/Status.hpp>

namespace hx {
class Bitset {
public:
	Bitset() noexcept
	    : m_bits_(NULL)
	{
	}

	Bitset(Bitset &&other) noexcept
	    : m_bits_(other.m_bits_)
	{
		other.m_bits_ = NULL;
	}

	Bitset &operator=(Bitset &&other) noexcept
	{
		std::swap(m_bits_, other.m_bits_);

		return *this;
	}

	Bitset(const Bitset &) = delete;
	Bitset &operator=(const Bitset &) = delete;

	~Bitset() noexcept
	{
		(void)hx_os_bitset_destroy(&m_bits_, NULL);
	}

	static Result<Bitset> create(size_t bitCount, size_t maxBits = 0,
		hx_os_context *context = NULL) noexcept
	{
		Bitset bits;
		hx_os_error error = {};
		auto status = hx_os_bitset_create(
			context, bitCount, maxBits, &bits.m_bits_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return bits;
	}

	size_t size() const noexcept
	{
		return hx_os_bitset_size(m_bits_);
	}

	size_t maxSize() const noexcept
	{
		return hx_os_bitset_max_size(m_bits_);
	}

	size_t count() const noexcept
	{
		return hx_os_bitset_count(m_bits_);
	}

	Result<void> resize(size_t bitCount) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_bitset_resize(m_bits_, bitCount, &error);

		return detail::Status(status, error);
	}

	Result<void> set(size_t bit, bool value = true) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_bitset_set(m_bits_, bit, value, &error);

		return detail::Status(status, error);
	}

	Result<void> reset(size_t bit) noexcept
	{
		return set(bit, false);
	}

	Result<bool> test(size_t bit) const noexcept
	{
		int value;
		hx_os_error error = {};
		auto status = hx_os_bitset_test(m_bits_, bit, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value != 0;
	}

	Result<void> setRange(
		size_t first, size_t count, bool value = true) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_bitset_set_range(
			m_bits_, first, count, value, &error);

		return detail::Status(status, error);
	}

	Result<void> fill(bool value = true) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_bitset_fill(m_bits_, value, &error);

		return detail::Status(status, error);
	}

	Result<void> clear() noexcept
	{
		return fill(false);
	}

	Result<size_t> findNext(size_t start, bool value = true) const noexcept
	{
		size_t index;
		hx_os_error error = {};
		auto status = hx_os_bitset_find_next(
			m_bits_, start, value, &index, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return index;
	}

	Result<void> apply(
		const Bitset &other, hx_os_bitset_operation operation) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_bitset_apply(
			m_bits_, other.m_bits_, operation, &error);

		return detail::Status(status, error);
	}

	Result<bool> intersects(const Bitset &other) const noexcept
	{
		int value;
		hx_os_error error = {};
		auto status = hx_os_bitset_intersects(
			m_bits_, other.m_bits_, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value != 0;
	}

	Result<bool> isSubsetOf(const Bitset &other) const noexcept
	{
		int value;
		hx_os_error error = {};
		auto status = hx_os_bitset_is_subset(
			m_bits_, other.m_bits_, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value != 0;
	}

private:
	hx_os_bitset *m_bits_;
};
} // namespace hx
#endif
