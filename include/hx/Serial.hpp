#ifndef HX_C_PLATFORM_CPP_SERIAL_HPP
#define HX_C_PLATFORM_CPP_SERIAL_HPP

#include <hx_os_serial.h>
#include <hx/Status.hpp>

namespace hx {
class Serial {
public:
	Serial() noexcept
	    : m_serial_(NULL)
	{
	}

	Serial(Serial &&other) noexcept
	    : m_serial_(other.m_serial_)
	{
		other.m_serial_ = NULL;
	}

	Serial &operator=(Serial &&other) noexcept
	{
		std::swap(m_serial_, other.m_serial_);
		return *this;
	}

	Serial(const Serial &) = delete;
	Serial &operator=(const Serial &) = delete;

	~Serial() noexcept
	{
		(void)hx_os_serial_close(&m_serial_, NULL);
	}

	static Result<Serial> open(const char *device,
		const hx_os_serial_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		Serial result;
		hx_os_error error = {};
		auto status = hx_os_serial_open(
			context, device, options, &result.m_serial_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	Result<size_t> read(void *data, size_t bytes, hx_os_deadline deadline,
		hx_os_cancel_token *cancel = NULL) noexcept
	{
		size_t completed = 0;
		hx_os_error error = {};
		auto status = hx_os_serial_read_until(m_serial_, data, bytes,
			deadline, cancel, &completed, &error);
		return Result<size_t>::withValue(completed, status, error);
	}

	Result<size_t> write(const void *data, size_t bytes,
		hx_os_deadline deadline,
		hx_os_cancel_token *cancel = NULL) noexcept
	{
		size_t completed = 0;
		hx_os_error error = {};
		auto status = hx_os_serial_write_until(m_serial_, data, bytes,
			deadline, cancel, &completed, &error);
		return Result<size_t>::withValue(completed, status, error);
	}

private:
	hx_os_serial *m_serial_;
};
} // namespace hx
#endif
