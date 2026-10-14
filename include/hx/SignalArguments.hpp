#ifndef HX_C_PLATFORM_CPP_SIGNAL_ARGUMENTS_HPP
#define HX_C_PLATFORM_CPP_SIGNAL_ARGUMENTS_HPP

#include <hx_os_signal.h>
#include <hx/Parameters.hpp>

namespace hx {

struct SignalArgument {
	StringView name;
	hx_os_param_value value;
};

/**
 * \brief Borrowed only through the current callback; copy() creates an owned
 * pack.
 */
class SignalArguments {
public:
	explicit SignalArguments(
		const hx_os_signal_arguments *arguments = NULL) noexcept
	    : m_arguments_(arguments)
	{
	}

	size_t size() const noexcept
	{
		return hx_os_signal_arguments_count(m_arguments_);
	}

	Result<int64_t> integer(const char *name) const noexcept
	{
		return read<int64_t>(name, hx_os_signal_arguments_get_i64);
	}

	Result<bool> boolean(const char *name) const noexcept

	{
		auto value = read<int>(name, hx_os_signal_arguments_get_bool);
		if (!value)
			return Failure(value.error());

		return value.value() != 0;
	}

	Result<uint64_t> unsignedInteger(const char *name) const noexcept
	{
		return read<uint64_t>(name, hx_os_signal_arguments_get_u64);
	}

	Result<hx_os_bytes_view> bytesView(const char *name) const noexcept
	{
		return read<hx_os_bytes_view>(
			name, hx_os_signal_arguments_get_bytes);
	}

	Result<double> real(const char *name) const noexcept
	{
		return read<double>(name, hx_os_signal_arguments_get_double);
	}

	Result<void *> pointer(const char *name) const noexcept
	{
		return read<void *>(name, hx_os_signal_arguments_get_pointer);
	}

	Result<StringView> stringView(const char *name) const noexcept
	{
		auto value = read<hx_string_view>(
			name, hx_os_signal_arguments_get_string);
		if (!value)
			return Failure(value.error());

		return StringView(value.value());
	}

	Result<String> string(
		const char *name, hx_os_context *context = NULL) const noexcept
	{
		auto value = stringView(name);
		if (!value)
			return Failure(value.error());

		return String::copy(value.value(), context);
	}

	Result<hx_os_param_value> value(const char *name) const noexcept
	{
		return read<hx_os_param_value>(
			name, hx_os_signal_arguments_get);
	}

	Result<SignalArgument> at(size_t index) const noexcept
	{
		SignalArgument entry = {};
		hx_string_view name = {};
		hx_os_error error = {};

		auto status = hx_os_signal_arguments_at(
			m_arguments_, index, &name, &entry.value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		entry.name = StringView(name);

		return entry;
	}

	Result<Parameters> copy(hx_os_context *context = NULL) const noexcept
	{
		Parameters result;
		hx_os_params *owner = NULL;
		hx_os_error error = {};

		auto status = hx_os_signal_arguments_copy(
			m_arguments_, context, &owner, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		result.m_data_ = owner;

		return result;
	}

private:
	const hx_os_signal_arguments *m_arguments_;

	template <typename T, typename F>
	Result<T> read(const char *name, F function) const noexcept
	{
		T out = {};
		hx_os_error error = {};

		auto status = function(m_arguments_, name, &out, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return out;
	}
};
} // namespace hx
#endif
