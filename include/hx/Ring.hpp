#ifndef HX_C_PLATFORM_CPP_RING_HPP
#define HX_C_PLATFORM_CPP_RING_HPP

#include <hx_ring.h>
#include <hx/Status.hpp>

namespace hx {
/**
 * \brief Borrows caller storage.
 *
 * \details Moving transfers pending borrows; use the new object to finish them.
 * Destruction neither frees storage nor commits bytes.
 */
class Ring {
public:
	struct ReadSpans {
		hx_os_ring_read_span parts[2];
	};

	struct WriteSpans {
		hx_os_ring_write_span parts[2];
	};

	Ring() noexcept
	    : m_ring_{}
	{
	}

	Ring(Ring &&other) noexcept
	    : m_ring_(other.m_ring_)
	{
		other.m_ring_ = hx_os_ring{};
	}

	Ring(const Ring &) = delete;
	Ring &operator=(const Ring &) = delete;
	Ring &operator=(Ring &&) = delete;

	static Result<Ring> create(void *storage, size_t capacity) noexcept
	{
		Ring result;
		hx_os_error error = {};
		auto status = hx_os_ring_init(
			&result.m_ring_, storage, capacity, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	size_t size() const noexcept
	{
		return hx_os_ring_size(&m_ring_);
	}

	size_t capacity() const noexcept
	{
		return hx_os_ring_capacity(&m_ring_);
	}

	size_t space() const noexcept
	{
		return hx_os_ring_space(&m_ring_);
	}

	bool empty() const noexcept
	{
		return size() == 0;
	}

	Result<void> clear() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_ring_clear(&m_ring_, &error);

		return detail::Status(status, error);
	}

	Result<void> write(const void *data, size_t bytes) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_ring_write(&m_ring_, data, bytes, &error);

		return detail::Status(status, error);
	}

	Result<void> read(void *data, size_t bytes) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_ring_read(&m_ring_, data, bytes, &error);

		return detail::Status(status, error);
	}

	Result<void> discard(size_t bytes) noexcept
	{
		return read(NULL, bytes);
	}

	Result<ReadSpans> readSpans() noexcept
	{
		ReadSpans spans = {};
		hx_os_error error = {};
		auto status =
			hx_os_ring_read_spans(&m_ring_, spans.parts, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return spans;
	}

	Result<WriteSpans> writeSpans() noexcept
	{
		WriteSpans spans = {};
		hx_os_error error = {};
		auto status =
			hx_os_ring_write_spans(&m_ring_, spans.parts, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return spans;
	}

	Result<void> consume(size_t bytes) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_ring_consume(&m_ring_, bytes, &error);

		return detail::Status(status, error);
	}

	Result<void> commit(size_t bytes) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_ring_commit(&m_ring_, bytes, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_ring m_ring_;
};
} // namespace hx
#endif
