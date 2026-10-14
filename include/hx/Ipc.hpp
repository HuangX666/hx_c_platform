#ifndef HX_C_PLATFORM_CPP_IPC_HPP
#define HX_C_PLATFORM_CPP_IPC_HPP

#include <hx_ipc.h>
#include <hx/Status.hpp>

namespace hx {

class IpcMessage {
public:
	explicit IpcMessage(hx_ipc_message *message = NULL) noexcept
	    : m_message_(message)
	{
	}

	IpcMessage(const IpcMessage &) = delete;

	IpcMessage &operator=(const IpcMessage &) = delete;

	IpcMessage(IpcMessage &&other) noexcept
	    : m_message_(other.m_message_)
	{
		other.m_message_ = NULL;
	}

	IpcMessage &operator=(IpcMessage &&other) noexcept
	{
		std::swap(m_message_, other.m_message_);

		return *this;
	}

	~IpcMessage() noexcept
	{
		hx_ipc_message_release(m_message_);
	}

	const hx_ipc_message *get() const noexcept
	{
		return m_message_;
	}

	const void *data() const noexcept
	{
		return m_message_ ? m_message_->data : NULL;
	}

	size_t size() const noexcept
	{
		return m_message_ ? m_message_->bytes : 0;
	}

	// Storage has a trailing NUL; binary data may contain earlier NULs.
	const char *text() const noexcept
	{
		return data() ? static_cast<const char *>(data()) : "";
	}

	int kind() const noexcept
	{
		return m_message_ ? m_message_->kind : 0;
	}

	hx_ipc_peer peer() const noexcept
	{
		return m_message_ ? m_message_->peer : 0;
	}

	uint64_t channel() const noexcept
	{
		return m_message_ ? m_message_->channel : 0;
	}

private:
	hx_ipc_message *m_message_;
};

/**
 * \brief Borrowed facade over a durable host-owned pointer.
 *
 * \details closeUntil() stops the I/O thread; a timeout keeps owner valid for
 * retry. The facade destructor never waits, detaches or destroys an unfinished
 * endpoint. Concurrent calls share one endpoint; serialize close and prevent
 * new calls during teardown.
 */
class Ipc {
public:
	explicit Ipc(hx_ipc *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	Result<void> listen(const char *address,
		const hx_ipc_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};

		auto status = hx_ipc_listen_with_options(
			context, address, options, m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<void> connect(const char *address,
		hx_os_deadline deadline = hx_os_deadline_after_ms(5000),
		const hx_ipc_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};

		auto status = hx_ipc_connect_until(
			context, address, options, deadline, m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<void> sendText(const char *text, uint64_t channel = 0,
		hx_ipc_peer peer = 0,
		hx_os_deadline deadline = hx_os_deadline_after_ms(
			5000)) const noexcept
	{
		hx_os_error error = {};

		auto status = hx_ipc_send_text_until(
			*m_owner_, peer, channel, text, deadline, &error);

		return detail::Status(status, error);
	}

	Result<hx_ipc_peer> waitConnectedUntil(
		hx_os_deadline deadline) const noexcept
	{
		hx_ipc_peer peer = 0;
		hx_os_error error = {};

		auto status = hx_ipc_wait_connected_until(
			*m_owner_, deadline, &peer, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return peer;
	}

	Result<void> send(const void *data, size_t bytes, uint64_t channel = 0,
		hx_ipc_peer peer = 0,
		hx_os_deadline deadline = hx_os_deadline_after_ms(
			5000)) const noexcept
	{
		hx_os_error error = {};

		auto status = hx_ipc_send_until(*m_owner_, peer, channel, data,
			bytes, deadline, &error);

		return detail::Status(status, error);
	}

	Result<IpcMessage> requestText(const char *text, uint64_t channel = 0,
		hx_ipc_peer peer = 0,
		hx_os_deadline deadline = hx_os_deadline_after_ms(
			5000)) const noexcept
	{
		hx_ipc_message *reply = NULL;
		hx_os_error error = {};

		auto status = hx_ipc_request_text_until(*m_owner_, peer,
			channel, text, deadline, &reply, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return IpcMessage(reply);
	}

	Result<IpcMessage> request(const void *data, size_t bytes,
		uint64_t channel = 0, hx_ipc_peer peer = 0,
		hx_os_deadline deadline = hx_os_deadline_after_ms(
			5000)) const noexcept
	{
		hx_ipc_message *reply = NULL;
		hx_os_error error = {};

		auto status = hx_ipc_request_until(*m_owner_, peer, channel,
			data, bytes, deadline, &reply, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return IpcMessage(reply);
	}

	Result<void> replyText(const IpcMessage &request, const char *text,
		hx_os_deadline deadline = hx_os_deadline_after_ms(
			5000)) const noexcept
	{
		hx_os_error error = {};

		auto status = hx_ipc_reply_text_until(
			*m_owner_, request.get(), text, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> reply(const IpcMessage &request, const void *data,
		size_t bytes,
		hx_os_deadline deadline = hx_os_deadline_after_ms(
			5000)) const noexcept
	{
		hx_os_error error = {};

		auto status = hx_ipc_reply_until(*m_owner_, request.get(), data,
			bytes, deadline, &error);

		return detail::Status(status, error);
	}

	Result<IpcMessage> receiveUntil(hx_os_deadline deadline) const noexcept
	{
		hx_ipc_message *message = NULL;
		hx_os_error error = {};

		auto status = hx_ipc_receive_until(
			*m_owner_, deadline, &message, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return IpcMessage(message);
	}

	Result<void> closeUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};

		auto status = hx_ipc_close_until(m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

	hx_ipc *nativeHandle() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_ipc **m_owner_;
};

} // namespace hx
#endif
