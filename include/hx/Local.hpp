#ifndef HX_C_PLATFORM_CPP_LOCAL_HPP
#define HX_C_PLATFORM_CPP_LOCAL_HPP

#include <hx_os_local.h>
#include <hx/Files.hpp>

namespace hx {

class DuplexPipe {
public:
	static Result<std::pair<File, File>> pair(
		hx_os_context *context = NULL) noexcept
	{
		hx_os_file *first = NULL, *second = NULL;
		hx_os_error error = {};

		auto status = hx_os_pipe_pair_socket(
			context, &first, &second, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return std::make_pair(File::adopt(first), File::adopt(second));
	}

	static Result<void> shutdown(
		File &file, int direction = HX_OS_SHUT_WRITE) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_pipe_socket_shutdown(
			file.get(), direction, &error);

		return detail::Status(status, error);
	}
};

/* Host owns the network and socket. Failed close preserves pending I/O. */
class LocalSocket {
public:
	explicit LocalSocket(hx_os_socket *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	Result<void> listen(hx_os_network *network, const char *path,
		int backlog = 16) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};

		auto status = hx_os_local_listen(
			network, path, backlog, m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<void> connect(hx_os_network *network, const char *path,
		hx_os_deadline deadline) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};

		auto status = hx_os_local_connect(
			network, path, deadline, m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<void> finishConnect(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_local_socket_finish_connect(
			*m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> acceptUntil(
		LocalSocket &destination, hx_os_deadline deadline) noexcept
	{
		if (*destination.m_owner_)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};

		auto status = hx_os_local_socket_accept_until(
			*m_owner_, deadline, destination.m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<File::ReadResult> readUntil(
		void *data, size_t bytes, hx_os_deadline deadline) noexcept
	{
		size_t completed = 0;
		hx_os_error error = {};

		auto status = hx_os_socket_receive_until(
			*m_owner_, data, bytes, deadline, &completed, &error);

		return Result<File::ReadResult>::withValue(
			File::ReadResult{completed, status == HX_OS_EOF},
			status, error);
	}

	Result<size_t> writeUntil(const void *data, size_t bytes,
		hx_os_deadline deadline) noexcept
	{
		size_t completed = 0;
		hx_os_error error = {};

		auto status = hx_os_socket_send_until(
			*m_owner_, data, bytes, deadline, &completed, &error);

		return Result<size_t>::withValue(completed, status, error);
	}

	Result<void> shutdown(int direction = HX_OS_SHUT_WRITE) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_socket_shutdown(*m_owner_, direction, &error);

		return detail::Status(status, error);
	}

	Result<String> address(
		bool peer = false, hx_os_context *context = NULL) const noexcept
	{
		hx_os_local_address value = {};
		hx_os_error error = {};

		auto status = hx_os_local_socket_address(
			*m_owner_, peer ? 1 : 0, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return String::copy(value.path, context);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_socket_close(m_owner_, &error);

		return detail::Status(status, error);
	}

	hx_os_socket *nativeHandle() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_os_socket **m_owner_;
};
} // namespace hx
#endif
