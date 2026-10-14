#ifndef HX_C_PLATFORM_CPP_FRAME_HPP
#define HX_C_PLATFORM_CPP_FRAME_HPP

#include <hx_os_frame.h>
#include <hx/Bytes.hpp>
#include <hx/Values.hpp>

namespace hx {
struct FrameSend {
	bool accepted;
};

/**
 * \brief Borrowed facade: the durable owner retains incomplete framing.
 *
 * \details Explicit flush/receive/abort/close controls that state; destruction
 * never discards a message or blocks. Owner and backend outlive this facade and
 * its calls.
 */
class Frame {
public:
	explicit Frame(hx_os_frame *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	Result<void> fromStream(hx_os_stream *stream, size_t maxBytes = 0,
		hx_os_context *context = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);
		hx_os_error error = {};
		auto status = hx_os_frame_from_stream(
			context, stream, maxBytes, m_owner_, &error);
		return detail::Status(status, error);
	}

	Result<void> fromStreams(hx_os_stream *reader, hx_os_stream *writer,
		size_t maxBytes = 0, hx_os_context *context = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);
		hx_os_error error = {};
		auto status = hx_os_frame_from_streams(
			context, reader, writer, maxBytes, m_owner_, &error);
		return detail::Status(status, error);
	}

	Result<void> fromSocket(hx_os_socket *socket, size_t maxBytes = 0,
		hx_os_context *context = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);
		hx_os_error error = {};
		auto status = hx_os_frame_from_socket(
			context, socket, maxBytes, m_owner_, &error);
		return detail::Status(status, error);
	}

	Result<void> fromTls(hx_os_tls *tls, uint64_t peer = 0,
		size_t maxBytes = 0, hx_os_context *context = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);
		hx_os_error error = {};
		auto status = hx_os_frame_from_tls(
			context, tls, peer, maxBytes, m_owner_, &error);
		return detail::Status(status, error);
	}

	/**
	 * \brief Result always carries accepted, including timeout/errors.
	 */
	Result<FrameSend> send(hx_os_bytes_view input,
		hx_os_deadline deadline =
			HX_OS_DEADLINE_INFINITE) const noexcept
	{
		int accepted = 0;
		hx_os_error error = {};
		auto status = hx_os_frame_send_until(
			*m_owner_, input, deadline, &accepted, &error);
		return Result<FrameSend>::withValue(
			FrameSend{accepted != 0}, status, error);
	}

	Result<void> flushUntil(hx_os_deadline deadline) const noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_frame_flush_until(*m_owner_, deadline, &error);
		return detail::Status(status, error);
	}

	Result<Bytes> receive(hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE,
		hx_os_context *context = NULL) const noexcept
	{
		Bytes result(context);
		hx_os_error error = {};
		auto status = hx_os_frame_receive_until(
			*m_owner_, deadline, result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	Result<String> receiveText(
		hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE,
		hx_os_context *context = NULL) const noexcept
	{
		String result(context);
		hx_os_error error = {};
		auto status = hx_os_frame_receive_text_until(
			*m_owner_, deadline, result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	Result<void> abort() const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_frame_abort(*m_owner_, &error);
		return detail::Status(status, error);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_frame_destroy(m_owner_, &error);
		return detail::Status(status, error);
	}

private:
	hx_os_frame **m_owner_;
};
} // namespace hx
#endif
