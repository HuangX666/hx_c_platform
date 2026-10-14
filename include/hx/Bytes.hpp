#ifndef HX_C_PLATFORM_CPP_BYTES_HPP
#define HX_C_PLATFORM_CPP_BYTES_HPP

#include <hx_bytes.h>
#include <hx/Status.hpp>

namespace hx {
class Bytes {
public:
	explicit Bytes(hx_os_context *context = NULL) noexcept
	{
		hx_bytes_init(&m_bytes_, context);
	}

	Bytes(Bytes &&other) noexcept
	    : m_bytes_(HX_BYTES_INIT)
	{
		hx_bytes_move(&m_bytes_, &other.m_bytes_);
	}

	Bytes &operator=(Bytes &&other) noexcept
	{
		std::swap(m_bytes_, other.m_bytes_);
		return *this;
	}

	Bytes(const Bytes &) = delete;
	Bytes &operator=(const Bytes &) = delete;

	~Bytes() noexcept
	{
		hx_bytes_destroy(&m_bytes_);
	}

	const void *data() const noexcept
	{
		return m_bytes_.data;
	}

	size_t size() const noexcept
	{
		return m_bytes_.size;
	}

	hx_os_bytes_view view() const noexcept
	{
		return hx_bytes_view(&m_bytes_);
	}

	hx_bytes *native() noexcept
	{
		return &m_bytes_;
	}

	Result<void> assign(const void *data, size_t size) noexcept
	{
		hx_os_error error = {};
		auto status = hx_bytes_assign(
			&m_bytes_, hx_os_bytes_view{data, size}, &error);
		return detail::Status(status, error);
	}

	Result<void> append(const void *data, size_t size) noexcept
	{
		hx_os_error error = {};
		auto status = hx_bytes_append(
			&m_bytes_, hx_os_bytes_view{data, size}, &error);
		return detail::Status(status, error);
	}

	Result<void> resize(size_t size) noexcept
	{
		hx_os_error error = {};
		auto status = hx_bytes_resize(&m_bytes_, size, &error);
		return detail::Status(status, error);
	}

private:
	hx_bytes m_bytes_;
};
} // namespace hx
#endif
