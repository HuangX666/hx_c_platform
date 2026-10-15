#ifndef HX_C_PLATFORM_CPP_COROUTINE_NETWORK_HPP
#define HX_C_PLATFORM_CPP_COROUTINE_NETWORK_HPP

#include <hx_os_coro_tls.h>
#include <hx_os_coro_http.h>
#include <hx/Http.hpp>

namespace hx {
namespace coro {
/**
 * \brief Borrowed facade; TLS keeps its explicit endpoint close contract.
 */
class Tls {
public:
	explicit Tls(hx_os_tls *tls) noexcept
	    : m_tls_(tls)
	{
	}

	Result<void> waitReady(hx_os_deadline deadline) const noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_coro_tls_wait_ready(m_tls_, deadline, &error);

		return detail::Status(status, error);
	}

	Result<size_t> send(const void *data, size_t bytes,
		hx_os_deadline deadline, uint64_t peer = 0) const noexcept
	{
		hx_os_error error = {};
		size_t completed = 0;
		auto status = hx_os_coro_tls_send_until(m_tls_, peer, data,
			bytes, deadline, &completed, &error);

		return Result<size_t>::withValue(completed, status, error);
	}

	Result<size_t> receive(void *data, size_t capacity,
		hx_os_deadline deadline, uint64_t peer = 0) const noexcept
	{
		hx_os_error error = {};
		size_t completed = 0;
		auto status = hx_os_coro_tls_receive_until(m_tls_, peer, data,
			capacity, deadline, &completed, &error);

		return Result<size_t>::withValue(completed, status, error);
	}

private:
	hx_os_tls *m_tls_;
};

/**
 * \brief Borrowed HTTP service owner; close explicitly before runtime teardown.
 */
class Http {
public:
	explicit Http(hx_os_coro_http *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	Result<void> create(hx_os_coro_runtime *runtime, size_t threads = 2,
		size_t requests = 16, size_t bytes = 64 * 1024 * 1024) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);
		hx_os_error error = {};
		auto status = hx_os_coro_http_create(
			runtime, threads, requests, bytes, m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<void> useCancellableDns(const char *servers = NULL) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_coro_http_use_cancellable_dns(
			*m_owner_, servers, &error);

		return detail::Status(status, error);
	}

	Result<void> get(const char *url, HttpResponse &response,
		const hx_os_http_options *options = NULL) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_coro_http_get(
			*m_owner_, url, options, response.native(), &error);

		return detail::Status(status, error);
	}

	Result<void> close(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_coro_http_close_until(m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_coro_http **m_owner_;
};
} // namespace coro
} // namespace hx
#endif
