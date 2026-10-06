#ifndef HX_C_PLATFORM_CPP_BYTE_QUEUE_HPP
#define HX_C_PLATFORM_CPP_BYTE_QUEUE_HPP

#include <hx_container.h>
#include <hx/Values.hpp>

namespace hx {
class ByteQueue {
public:
	ByteQueue() noexcept
	    : m_queue_(NULL)
	{
	}

	ByteQueue(ByteQueue &&other) noexcept
	    : m_queue_(other.m_queue_)
	{
		other.m_queue_ = NULL;
	}

	ByteQueue &operator=(ByteQueue &&other) noexcept
	{
		std::swap(m_queue_, other.m_queue_);

		return *this;
	}

	ByteQueue(const ByteQueue &) = delete;
	ByteQueue &operator=(const ByteQueue &) = delete;

	~ByteQueue() noexcept
	{
		(void)hx_os_byte_queue_destroy(&m_queue_, NULL);
	}

	static Result<ByteQueue> create(
		size_t maxBytes = 65536, hx_os_context *context = NULL) noexcept
	{
		ByteQueue queue;
		hx_os_error error = {};
		auto status = hx_os_byte_queue_create(
			context, maxBytes, &queue.m_queue_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return queue;
	}

	size_t size() const noexcept
	{
		return hx_os_byte_queue_size(m_queue_);
	}

	Result<void> write(const void *data, size_t bytes) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_byte_queue_write(m_queue_, data, bytes, &error);

		return detail::Status(status, error);
	}

	Result<void> read(void *data, size_t bytes) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_byte_queue_read(m_queue_, data, bytes, &error);

		return detail::Status(status, error);
	}

	Result<Buffer> read(
		size_t bytes, hx_os_context *context = NULL) noexcept
	{
		if (!m_queue_)
			return Failure(HX_OS_CLOSED);
		if (bytes > size())
			return Failure(HX_OS_NEED_INPUT);

		auto buffer = Buffer::allocate(bytes, context);
		if (!buffer)
			return Failure(buffer.error());

		auto status = read(buffer.value().data(), bytes);
		if (!status)
			return Failure(status.error());

		return buffer;
	}

	Result<void> peek(
		void *data, size_t bytes, size_t offset = 0) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_byte_queue_peek(
			m_queue_, offset, data, bytes, &error);

		return detail::Status(status, error);
	}

	Result<void> discard(size_t bytes) noexcept
	{
		return read(NULL, bytes);
	}

private:
	hx_os_byte_queue *m_queue_;
};
} // namespace hx
#endif
