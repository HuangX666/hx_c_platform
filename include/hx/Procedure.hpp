#ifndef HX_C_PLATFORM_CPP_PROCEDURE_HPP
#define HX_C_PLATFORM_CPP_PROCEDURE_HPP
#include <hx_os_procedure.h>
#include <hx/Signal.hpp>
namespace hx {

/* Same host ownership and retirement rules as SignalHandler. */
class ProcedureHandler {
public:
	explicit ProcedureHandler(hx_os_proc_handler *&owner,
		hx_os_context *context = NULL) noexcept
	    : m_owner_(&owner)
	    , m_context_(context)
	{
	}
	Result<void> create(hx_os_context *context = NULL,
		const hx_os_proc_options *options = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);
		hx_os_error error = {};
		auto status = hx_os_proc_create_with_options(
			context, options, m_owner_, &error);
		if (status == HX_OS_OK)
			m_context_ = context;
		return detail::Status(status, error);
	}
	template <typename F>
	Result<hx_os_proc_registration> add(
		const char *name, F &&function) noexcept
	{
		typedef typename std::decay<F>::type Function;
		auto callback = detail::CallbackOwner::create(
			m_context_, std::forward<F>(function));
		if (!callback)
			return Failure(callback.error());
		auto *state = callback.value().nativeLease();
		hx_os_proc_registration id = 0;
		hx_os_error error = {};
		auto status = hx_os_proc_add(*m_owner_, name, invoke<Function>,
			state, detail::callback_cleanup, &id, &error);
		if (status != HX_OS_OK) {
			state->release();
			return Failure(status, error);
		}
		return id;
	}
	Result<CallData> call(
		const char *name, const CallData &input = CallData()) noexcept
	{
		CallData result = input;
		hx_os_error error = {};
		auto status = hx_os_proc_call(
			*m_owner_, name, &result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}
	Result<void> removeUntil(
		hx_os_proc_registration id, hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_proc_unregister_until(
			*m_owner_, id, deadline, &error);
		return detail::Status(status, error);
	}
	Result<void> closeUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_proc_close_until(m_owner_, deadline, &error);
		return detail::Status(status, error);
	}

private:
	hx_os_proc_handler **m_owner_;
	hx_os_context *m_context_;
	template <typename F>
	static hx_os_status HX_OS_CALL invoke(
		hx_os_calldata *data, void *user, hx_os_error *error) noexcept
	{
		auto *state = static_cast<detail::CallbackModel<F> *>(
			static_cast<detail::CallbackState *>(user));
		CallData parameters(*data);
		auto result =
			detail::InvokeCallback(state->function(), parameters);
		if (result)
			*data = parameters.native();
		if (error)
			*error = result.error();
		return result.status();
	}
};
} // namespace hx
#endif
