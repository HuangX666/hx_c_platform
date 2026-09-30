#ifndef HX_C_PLATFORM_CPP_LOCAL_HPP
#define HX_C_PLATFORM_CPP_LOCAL_HPP
#include <hx_os_local.h>
#include <hx/Files.hpp>
namespace hx {
class DuplexPipe {
public:
	static std::pair<File, File> pair(hx_os_context *context = NULL)
	{
		hx_os_file *first = NULL, *second = NULL;
		hx_os_error error;
		detail::CheckStatus(hx_os_pipe_pair_socket(
					    context, &first, &second, &error),
			error);
		return std::make_pair(File::adopt(first), File::adopt(second));
	}
	static void shutdown(File &file, int direction = HX_OS_SHUT_WRITE)
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_pipe_socket_shutdown(
					    file.get(), direction, &error),
			error);
	}
};
/* The host owns both network and socket pointer variables. Explicit close
 * retains a socket with pending async I/O. Facade destruction does not lose
 * the host's handle or silently cancel another owner's request.
 */
class LocalSocket {
public:
	explicit LocalSocket(hx_os_socket *&owner) noexcept
	    : m_owner_(&owner)
	{
	}
	void listen(hx_os_network *network, const char *path, int backlog = 16)
	{
		if (*m_owner_)
			throw StatusError(HX_OS_BUSY);
		hx_os_error error;
		detail::CheckStatus(hx_os_local_listen(network, path, backlog,
					    m_owner_, &error),
			error);
	}
	hx_os_status connect(hx_os_network *network, const char *path,
		hx_os_deadline deadline, hx_os_error *error = NULL)
	{
		if (*m_owner_)
			throw StatusError(HX_OS_BUSY);
		return hx_os_local_connect(
			network, path, deadline, m_owner_, error);
	}
	hx_os_status finishConnect(
		hx_os_deadline deadline, hx_os_error *error = NULL) noexcept
	{
		return hx_os_local_socket_finish_connect(
			*m_owner_, deadline, error);
	}
	hx_os_status acceptUntil(LocalSocket &destination,
		hx_os_deadline deadline, hx_os_error *error = NULL)
	{
		if (*destination.m_owner_)
			throw StatusError(HX_OS_BUSY);
		return hx_os_local_socket_accept_until(
			*m_owner_, deadline, destination.m_owner_, error);
	}
	File::ReadResult readUntil(
		void *data, size_t bytes, hx_os_deadline deadline)
	{
		size_t completed = 0;
		hx_os_error error;
		hx_os_status status = hx_os_socket_receive_until(
			*m_owner_, data, bytes, deadline, &completed, &error);
		if (status != HX_OS_OK && status != HX_OS_EOF)
			throw IoError(error, completed);
		return {completed, status == HX_OS_EOF};
	}
	void writeUntil(const void *data, size_t bytes, hx_os_deadline deadline)
	{
		size_t completed = 0;
		hx_os_error error;
		if (hx_os_socket_send_until(*m_owner_, data, bytes, deadline,
			    &completed, &error) != HX_OS_OK)
			throw IoError(error, completed);
	}
	void shutdown(int direction = HX_OS_SHUT_WRITE)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_socket_shutdown(*m_owner_, direction, &error),
			error);
	}
	std::string address(bool peer = false) const
	{
		hx_os_local_address value;
		hx_os_error error;
		detail::CheckStatus(hx_os_local_socket_address(*m_owner_,
					    peer ? 1 : 0, &value, &error),
			error);
		return value.path;
	}
	hx_os_status close(hx_os_error *error = NULL) noexcept
	{
		return hx_os_socket_close(m_owner_, error);
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
