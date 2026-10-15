#ifndef HX_C_PLATFORM_CPP_TLS_HPP
#define HX_C_PLATFORM_CPP_TLS_HPP

#include <hx_os_tls.h>
#include <hx/Status.hpp>
#include <hx/Values.hpp>

namespace hx {

/**
 * \brief Borrowed facade over a durable owner.
 *
 * \details Explicit close/abort keeps ownership on timeout; destruction of this
 * facade never joins or abandons a worker. send/receive results retain the byte
 * count even when ok() is false.
 */
class Tls {
public:
	explicit Tls(hx_os_tls *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	Result<void> connect(const char *host, uint16_t port,
		const hx_os_tls_options *options = NULL,
		hx_os_deadline deadline = hx_os_deadline_after_ms(5000),
		hx_os_context *context = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};
		auto status = hx_os_tls_connect_until(context, host, port,
			options, deadline, m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<void> listen(const char *host, uint16_t port,
		const hx_os_tls_options &options,
		hx_os_deadline deadline = hx_os_deadline_after_ms(5000),
		hx_os_context *context = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};
		auto status = hx_os_tls_listen_until(context, host, port,
			&options, deadline, m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<void> connectEx(const char *host, uint16_t port,
		const hx_os_tls_options_ex &options,
		hx_os_deadline deadline = hx_os_deadline_after_ms(5000),
		hx_os_context *context = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};
		auto status = hx_os_tls_connect_ex_until(context, host, port,
			&options, deadline, m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<void> listenEx(const char *host, uint16_t port,
		const hx_os_tls_options_ex &options,
		hx_os_deadline deadline = hx_os_deadline_after_ms(5000),
		hx_os_context *context = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};
		auto status = hx_os_tls_listen_ex_until(context, host, port,
			&options, deadline, m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<String> alpn(
		uint64_t peer = 0, hx_os_context *context = NULL) const noexcept
	{
		String result(context);
		hx_os_error error = {};
		auto status = hx_os_tls_get_alpn(
			*m_owner_, peer, result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	Result<uint64_t> acceptUntil(hx_os_deadline deadline) const noexcept
	{
		uint64_t peer = 0;
		hx_os_error error = {};
		auto status = hx_os_tls_accept_until(
			*m_owner_, deadline, &peer, &error);

		if (status != HX_OS_OK)
			return Failure(status, error);

		return peer;
	}

	Result<void> waitReadyUntil(hx_os_deadline deadline) const noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_tls_wait_ready_until(*m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

	Result<hx_os_address> localAddress() const noexcept
	{
		hx_os_address address = {};
		hx_os_error error = {};
		auto status =
			hx_os_tls_local_address(*m_owner_, &address, &error);

		if (status != HX_OS_OK)
			return Failure(status, error);

		return address;
	}

	Result<hx_os_tls_peer_info> peerInfo(uint64_t peer = 0) const noexcept
	{
		hx_os_tls_peer_info info = {};
		hx_os_error error = {};
		auto status = hx_os_tls_get_peer_info(
			*m_owner_, peer, &info, sizeof(info), &error);

		if (status != HX_OS_OK)
			return Failure(status, error);

		return info;
	}

	Result<size_t> send(const void *data, size_t bytes, uint64_t peer = 0,
		hx_os_deadline deadline = hx_os_deadline_after_ms(
			5000)) const noexcept
	{
		size_t accepted = 0;
		hx_os_error error = {};
		auto status = hx_os_tls_send_until(*m_owner_, peer, data, bytes,
			deadline, &accepted, &error);

		return Result<size_t>::withValue(accepted, status, error);
	}

	Result<size_t> receive(void *data, size_t bytes, uint64_t peer = 0,
		hx_os_deadline deadline = hx_os_deadline_after_ms(
			5000)) const noexcept
	{
		size_t received = 0;
		hx_os_error error = {};
		auto status = hx_os_tls_receive_until(*m_owner_, peer, data,
			bytes, deadline, &received, &error);

		return Result<size_t>::withValue(received, status, error);
	}

	Result<size_t> receiveExact(void *data, size_t bytes, uint64_t peer = 0,
		hx_os_deadline deadline = hx_os_deadline_after_ms(
			5000)) const noexcept
	{
		size_t received = 0;
		hx_os_error error = {};
		auto status = hx_os_tls_receive_exact_until(*m_owner_, peer,
			data, bytes, deadline, &received, &error);

		return Result<size_t>::withValue(received, status, error);
	}

	Result<void> flushUntil(
		hx_os_deadline deadline, uint64_t peer = 0) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_tls_flush_until(
			*m_owner_, peer, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> cancelWaits() const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_tls_cancel_waits(*m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<void> closePeerUntil(uint64_t peer, hx_os_deadline deadline,
		bool abort = false) const noexcept
	{
		hx_os_error error = {};
		auto status = abort ? hx_os_tls_peer_abort_until(
					      *m_owner_, peer, deadline, &error)
				    : hx_os_tls_peer_close_until(*m_owner_,
					      peer, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> closeUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_tls_close_until(m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> abortUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_tls_abort_until(m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

	hx_os_tls *get() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_os_tls **m_owner_;
};
} // namespace hx
#endif
