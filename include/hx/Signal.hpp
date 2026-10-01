#ifndef HX_C_PLATFORM_CPP_SIGNAL_HPP
#define HX_C_PLATFORM_CPP_SIGNAL_HPP
#include <hx_os_signal.h>
#include <hx/Status.hpp>
#include <functional>
#include <memory>
#include <utility>
namespace hx {
class CallDataView {
public:
	explicit CallDataView(const hx_os_calldata &data) noexcept
	    : m_data_(&data)
	{
	}
	int64_t integer(const char *name) const
	{
		int64_t value;
		hx_os_error error;
		detail::CheckStatus(
			hx_os_calldata_get_i64(m_data_, name, &value, &error),
			error);
		return value;
	}
	bool boolean(const char *name) const
	{
		int value;
		hx_os_error error;
		detail::CheckStatus(
			hx_os_calldata_get_bool(m_data_, name, &value, &error),
			error);
		return value != 0;
	}
	double real(const char *name) const
	{
		double value;
		hx_os_error error;
		detail::CheckStatus(hx_os_calldata_get_double(
					    m_data_, name, &value, &error),
			error);
		return value;
	}
	std::string string(const char *name) const
	{
		hx_string_view value;
		hx_os_error error;
		detail::CheckStatus(hx_os_calldata_get_string(
					    m_data_, name, &value, &error),
			error);
		return std::string(value.data, value.size);
	}
	void *pointer(const char *name) const
	{
		void *value;
		hx_os_error error;
		detail::CheckStatus(hx_os_calldata_get_pointer(
					    m_data_, name, &value, &error),
			error);
		return value;
	}

private:
	const hx_os_calldata *m_data_;
};
class CallData {
public:
	CallData() noexcept
	    : m_data_{}
	{
	}
	CallData &setInt(const char *name, int64_t value)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_calldata_set_i64(&m_data_, name, value, &error),
			error);
		return *this;
	}
	CallData &setBool(const char *name, bool value)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_calldata_set_bool(&m_data_, name, value, &error),
			error);
		return *this;
	}
	CallData &setDouble(const char *name, double value)
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_calldata_set_double(
					    &m_data_, name, value, &error),
			error);
		return *this;
	}
	CallData &setString(const char *name, const std::string &value)
	{
		hx_string_view text = {value.data(), value.size()};
		hx_os_error error;
		detail::CheckStatus(
			hx_os_calldata_set_string(&m_data_, name, text, &error),
			error);
		return *this;
	}
	CallData &setPointer(const char *name, void *value)
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_calldata_set_pointer(
					    &m_data_, name, value, &error),
			error);
		return *this;
	}
	CallDataView view() const noexcept
	{
		return CallDataView(m_data_);
	}
	explicit CallData(const hx_os_calldata &data) noexcept
	    : m_data_(data)
	{
	}
	hx_os_calldata &native() noexcept
	{
		return m_data_;
	}
	const hx_os_calldata &native() const noexcept
	{
		return m_data_;
	}

private:
	hx_os_calldata m_data_;
};
/* Host-owned handle remains reachable after a timeout/exception. Explicit
 * close retires callbacks and copied closures. Destruction of this facade
 * does not wait, detach or drop the host's handle. Captured references remain
 * the caller's responsibility through disconnectUntil/closeUntil success.
 */
class SignalHandler {
public:
	explicit SignalHandler(hx_os_signal_handler *&owner) noexcept
	    : m_owner_(&owner)
	{
	}
	void create(hx_os_context *context = NULL,
		const hx_os_signal_options *options = NULL)
	{
		if (*m_owner_)
			throw StatusError(HX_OS_BUSY);
		hx_os_error error;
		detail::CheckStatus(hx_os_signal_handler_create_with_options(
					    context, options, m_owner_, &error),
			error);
	}
	hx_os_signal_connection connect(
		const char *signal, std::function<void(CallDataView)> callback)
	{
		if (!callback)
			throw StatusError(HX_OS_INVALID);
		std::unique_ptr<Callback> owned(
			new Callback(std::move(callback)));
		hx_os_signal_connection connection;
		hx_os_error error;
		auto status = hx_os_signal_connect(*m_owner_, signal, invoke,
			owned.get(), cleanup, &connection, &error);
		if (status == HX_OS_OK)
			owned.release();
		detail::CheckStatus(status, error);
		return connection;
	}
	size_t emit(const char *signal, const CallData &data = CallData())
	{
		size_t called;
		hx_os_error error;
		detail::CheckStatus(hx_os_signal_emit(*m_owner_, signal,
					    &data.native(), &called, &error),
			error);
		return called;
	}
	hx_os_status disconnectUntil(hx_os_signal_connection connection,
		hx_os_deadline deadline, hx_os_error *error = NULL) noexcept
	{
		return hx_os_signal_disconnect_until(
			*m_owner_, connection, deadline, error);
	}
	void stop() noexcept
	{
		hx_os_signal_handler_stop(*m_owner_);
	}
	hx_os_status closeUntil(
		hx_os_deadline deadline, hx_os_error *error = NULL) noexcept
	{
		return hx_os_signal_handler_close_until(
			m_owner_, deadline, error);
	}

private:
	typedef std::function<void(CallDataView)> Callback;
	static hx_os_status HX_OS_CALL invoke(
		const hx_os_calldata *data, void *user, hx_os_error *error)
	{
		try {
			(*static_cast<Callback *>(user))(CallDataView(*data));
		} catch (const StatusError &failure) {
			*error = failure.error();
			return failure.status();
		} catch (...) {
			return HX_OS_IO_ERROR;
		}
		return HX_OS_OK;
	}
	static void HX_OS_CALL cleanup(void *user)
	{
		delete static_cast<Callback *>(user);
	}
	hx_os_signal_handler **m_owner_;
};
} // namespace hx
#endif
