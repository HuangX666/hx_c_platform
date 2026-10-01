#ifndef HX_C_PLATFORM_CPP_PROCEDURE_HPP
#define HX_C_PLATFORM_CPP_PROCEDURE_HPP
#include <hx_os_procedure.h>
#include <hx/Signal.hpp>
namespace hx {
/* The host retains owner through close timeout/exception. Destruction of this
 * facade does not wait or release owner. Same lifetime contract as
 * SignalHandler.
 */
class ProcedureHandler {
public:
	explicit ProcedureHandler(hx_os_proc_handler *&owner) noexcept
	    : m_owner_(&owner)
	{
	}
	void create(hx_os_context *context = NULL,
		const hx_os_proc_options *options = NULL)
	{
		if (*m_owner_)
			throw StatusError(HX_OS_BUSY);
		hx_os_error error;
		detail::CheckStatus(hx_os_proc_create_with_options(
					    context, options, m_owner_, &error),
			error);
	}
	hx_os_proc_registration add(
		const char *name, std::function<void(CallData &)> callback)
	{
		if (!callback)
			throw StatusError(HX_OS_INVALID);
		std::unique_ptr<Callback> owned(
			new Callback(std::move(callback)));
		hx_os_proc_registration id;
		hx_os_error error;
		auto status = hx_os_proc_add(*m_owner_, name, invoke,
			owned.get(), cleanup, &id, &error);
		if (status == HX_OS_OK)
			owned.release();
		detail::CheckStatus(status, error);
		return id;
	}
	CallData call(const char *name, const CallData &input = CallData())
	{
		CallData result = input;
		hx_os_error error;
		detail::CheckStatus(hx_os_proc_call(*m_owner_, name,
					    &result.native(), &error),
			error);
		return result;
	}
	hx_os_status removeUntil(hx_os_proc_registration id,
		hx_os_deadline deadline, hx_os_error *error = NULL) noexcept
	{
		return hx_os_proc_unregister_until(
			*m_owner_, id, deadline, error);
	}
	hx_os_status closeUntil(
		hx_os_deadline deadline, hx_os_error *error = NULL) noexcept
	{
		return hx_os_proc_close_until(m_owner_, deadline, error);
	}

private:
	typedef std::function<void(CallData &)> Callback;
	static hx_os_status HX_OS_CALL invoke(
		hx_os_calldata *data, void *user, hx_os_error *error)
	{
		try {
			CallData parameters(*data);
			(*static_cast<Callback *>(user))(parameters);
			*data = parameters.native();
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
	hx_os_proc_handler **m_owner_;
};
} // namespace hx
#endif
