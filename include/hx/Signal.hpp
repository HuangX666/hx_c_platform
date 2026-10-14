#ifndef HX_C_PLATFORM_CPP_SIGNAL_HPP
#define HX_C_PLATFORM_CPP_SIGNAL_HPP

#include <hx_os_signal.h>
#include <hx/Values.hpp>
#include <hx/Callback.hpp>
#include <hx/SignalArguments.hpp>

namespace hx {
class CallDataEdit {
public:
	explicit CallDataEdit(hx_os_calldata &data) noexcept
	    : m_edit_(hx_os_calldata_edit_begin(&data))
	{
	}

	CallDataEdit &setInt(const char *name, int64_t value) noexcept
	{
		hx_os_calldata_edit_set_i64(&m_edit_, name, value);

		return *this;
	}

	CallDataEdit &setBool(const char *name, bool value) noexcept
	{
		hx_os_calldata_edit_set_bool(&m_edit_, name, value);

		return *this;
	}

	CallDataEdit &setDouble(const char *name, double value) noexcept
	{
		hx_os_calldata_edit_set_double(&m_edit_, name, value);

		return *this;
	}

	CallDataEdit &setString(const char *name, StringView value) noexcept
	{
		hx_os_calldata_edit_set_string(&m_edit_, name, value.native());

		return *this;
	}

	CallDataEdit &setPointer(const char *name, void *value) noexcept
	{
		hx_os_calldata_edit_set_pointer(&m_edit_, name, value);

		return *this;
	}

	Result<void> finish() const noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_calldata_edit_finish(&m_edit_, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_calldata_edit m_edit_;
};

class CallDataView {
public:
	explicit CallDataView(const hx_os_calldata &data) noexcept
	    : m_data_(&data)
	{
	}

	Result<int64_t> integer(const char *name) const noexcept
	{
		int64_t value{};
		hx_os_error error = {};
		auto status =
			hx_os_calldata_get_i64(m_data_, name, &value, &error);

		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<bool> boolean(const char *name) const noexcept
	{
		int value{};
		hx_os_error error = {};
		auto status =
			hx_os_calldata_get_bool(m_data_, name, &value, &error);

		if (status != HX_OS_OK)
			return Failure(status, error);

		return value != 0;
	}

	Result<double> real(const char *name) const noexcept
	{
		double value{};
		hx_os_error error = {};

		auto status = hx_os_calldata_get_double(
			m_data_, name, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<void *> pointer(const char *name) const noexcept
	{
		void *value{};
		hx_os_error error = {};

		auto status = hx_os_calldata_get_pointer(
			m_data_, name, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<String> string(
		const char *name, hx_os_context *context = NULL) const noexcept
	{
		auto value = stringView(name);
		if (!value)
			return Failure(value.error());

		return String::copy(value.value(), context);
	}

	/**
	 * \brief Borrows the pack until mutation/destruction; allocates no
	 * storage.
	 */
	Result<StringView> stringView(const char *name) const noexcept
	{
		hx_string_view value = {};
		hx_os_error error = {};

		auto status = hx_os_calldata_get_string(
			m_data_, name, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return StringView(value);
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

	explicit CallData(const hx_os_calldata &data) noexcept
	    : m_data_(data)
	{
	}

	CallDataEdit edit() & noexcept
	{
		return CallDataEdit(m_data_);
	}

	Result<void> setInt(const char *name, int64_t value) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_calldata_set_i64(&m_data_, name, value, &error);

		return detail::Status(status, error);
	}

	Result<void> setBool(const char *name, bool value) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_calldata_set_bool(&m_data_, name, value, &error);

		return detail::Status(status, error);
	}

	Result<void> setDouble(const char *name, double value) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_calldata_set_double(
			&m_data_, name, value, &error);

		return detail::Status(status, error);
	}

	Result<void> setString(const char *name, StringView value) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_calldata_set_string(
			&m_data_, name, value.native(), &error);

		return detail::Status(status, error);
	}

	Result<void> setPointer(const char *name, void *value) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_calldata_set_pointer(
			&m_data_, name, value, &error);

		return detail::Status(status, error);
	}

	CallDataView view() const noexcept
	{
		return CallDataView(m_data_);
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

/**
 * \brief Host-owned pointer survives close failure.
 *
 * \details Callbacks return void, hx_os_status or Result<void>; noexcept
 * captures retire outside native locks.
 */
class SignalHandler {
public:
	explicit SignalHandler(hx_os_signal_handler *&owner,
		hx_os_context *context = NULL) noexcept
	    : m_owner_(&owner)
	    , m_context_(context)
	{
	}

	Result<void> create(hx_os_context *context = NULL,
		const hx_os_signal_options *options = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};

		auto status = hx_os_signal_handler_create_with_options(
			context, options, m_owner_, &error);
		if (status == HX_OS_OK)
			m_context_ = context;

		return detail::Status(status, error);
	}

	template <typename F>
	Result<hx_os_signal_connection> connect(
		const char *signal, F &&function) noexcept
	{
		typedef typename std::decay<F>::type Function;

		auto callback = detail::CallbackOwner::create(
			m_context_, std::forward<F>(function));
		if (!callback)
			return Failure(callback.error());

		auto *state = callback.value().nativeLease();

		hx_os_signal_connection id = 0;
		hx_os_error error = {};

		auto status = hx_os_signal_connect(*m_owner_, signal,
			invoke<Function>, state, detail::callback_cleanup, &id,
			&error);
		if (status != HX_OS_OK) {
			state->release();

			return Failure(status, error);
		}

		return id;
	}

	template <typename F>
	Result<hx_os_signal_connection> connectAll(F &&function) noexcept
	{
		typedef typename std::decay<F>::type Function;

		auto callback = detail::CallbackOwner::create(
			m_context_, std::forward<F>(function));
		if (!callback)
			return Failure(callback.error());
		auto *state = callback.value().nativeLease();
		hx_os_signal_connection id = 0;
		hx_os_error error = {};

		auto status = hx_os_signal_connect_all(*m_owner_,
			invoke_all<Function>, state, detail::callback_cleanup,
			&id, &error);
		if (status != HX_OS_OK) {
			state->release();

			return Failure(status, error);
		}

		return id;
	}

	template <typename F>
	Result<hx_os_signal_connection> observe(
		const char *signal, F &&function) noexcept
	{
		typedef typename std::decay<F>::type Function;
		auto callback = detail::CallbackOwner::create(
			m_context_, std::forward<F>(function));
		if (!callback)
			return Failure(callback.error());
		auto *state = callback.value().nativeLease();
		hx_os_signal_connection id = 0;
		hx_os_error error = {};

		auto status = hx_os_signal_connect_observer(*m_owner_, signal,
			invoke_all<Function>, state, detail::callback_cleanup,
			&id, &error);
		if (status != HX_OS_OK) {
			state->release();

			return Failure(status, error);
		}

		return id;
	}

	template <typename F>
	Result<hx_os_signal_connection> connectMutable(
		const char *signal, F &&function) noexcept
	{
		typedef typename std::decay<F>::type Function;

		return connectMutation(signal ? signal : "",
			std::forward<F>(function), invoke_mutable<Function>);
	}

	template <typename F>
	Result<hx_os_signal_connection> connectAllMutable(F &&function) noexcept
	{
		typedef typename std::decay<F>::type Function;

		return connectMutation(NULL, std::forward<F>(function),
			invoke_all_mutable<Function>);
	}

	Result<size_t> notifyMutable(
		const char *signal, ParametersRef parameters) noexcept
	{
		size_t called = 0;
		hx_os_error error = {};
		auto status = hx_os_signal_emit_params(*m_owner_, signal,
			parameters.native(), &called, &error);

		return Result<size_t>::withValue(called, status, error);
	}

	Result<size_t> notify(
		const char *signal, const CallData &data = CallData()) noexcept
	{
		size_t called = 0;
		hx_os_error error = {};

		auto status = hx_os_signal_emit(
			*m_owner_, signal, &data.native(), &called, &error);

		return Result<size_t>::withValue(called, status, error);
	}

#pragma push_macro("emit")
#undef emit

	/**
	 * \brief Compatibility alias.
	 *
	 * \details notify() also works when Qt defines emit.
	 */
	Result<size_t> emit(
		const char *signal, const CallData &data = CallData()) noexcept
	{
		return notify(signal, data);
	}

#pragma pop_macro("emit")

	Result<void> disconnectUntil(
		hx_os_signal_connection id, hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_signal_disconnect_until(
			*m_owner_, id, deadline, &error);

		return detail::Status(status, error);
	}

	void stop() noexcept
	{
		hx_os_signal_handler_stop(*m_owner_);
	}

	Result<void> closeUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_signal_handler_close_until(
			m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_signal_handler **m_owner_;
	hx_os_context *m_context_;

	template <typename F>
	Result<hx_os_signal_connection> connectMutation(const char *signal,
		F &&function, hx_os_signal_mutable_fn invoke) noexcept
	{
		auto callback = detail::CallbackOwner::create(
			m_context_, std::forward<F>(function));
		if (!callback)
			return Failure(callback.error());
		auto *state = callback.value().nativeLease();
		hx_os_signal_connection id = 0;
		hx_os_error error = {};

		auto status = signal
			? hx_os_signal_connect_mutable(*m_owner_, signal,
				  invoke, state, detail::callback_cleanup, &id,
				  &error)
			: hx_os_signal_connect_all_mutable(*m_owner_, invoke,
				  state, detail::callback_cleanup, &id, &error);
		if (status != HX_OS_OK) {
			state->release();

			return Failure(status, error);
		}

		return id;
	}

	template <typename F>
	static hx_os_status HX_OS_CALL invoke_mutable(const char *,
		hx_os_params *parameters, void *user,
		hx_os_error *error) noexcept
	{
		auto *state = static_cast<detail::CallbackModel<F> *>(
			static_cast<detail::CallbackState *>(user));
		auto result = detail::InvokeCallback(
			state->function(), ParametersRef(parameters));
		if (error)
			*error = result.error();

		return result.status();
	}

	template <typename F>
	static hx_os_status HX_OS_CALL invoke_all_mutable(const char *name,
		hx_os_params *parameters, void *user,
		hx_os_error *error) noexcept
	{
		auto *state = static_cast<detail::CallbackModel<F> *>(
			static_cast<detail::CallbackState *>(user));
		auto result = detail::InvokeCallback(state->function(),
			StringView(name), ParametersRef(parameters));
		if (error)
			*error = result.error();

		return result.status();
	}

	template <typename F>
	static hx_os_status HX_OS_CALL invoke_all(const char *name,
		const hx_os_signal_arguments *arguments, void *user,
		hx_os_error *error) noexcept
	{
		auto *state = static_cast<detail::CallbackModel<F> *>(
			static_cast<detail::CallbackState *>(user));
		auto result = detail::InvokeCallback(state->function(),
			StringView(name), SignalArguments(arguments));
		if (error)
			*error = result.error();

		return result.status();
	}

	template <typename F>
	static hx_os_status HX_OS_CALL invoke(const hx_os_calldata *data,
		void *user, hx_os_error *error) noexcept
	{
		auto *state = static_cast<detail::CallbackModel<F> *>(
			static_cast<detail::CallbackState *>(user));

		auto result = detail::InvokeCallback(
			state->function(), CallDataView(*data));
		if (error)
			*error = result.error();

		return result.status();
	}
};
} // namespace hx
#endif
