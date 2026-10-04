#ifndef HX_C_PLATFORM_CPP_PROCESS_HPP
#define HX_C_PLATFORM_CPP_PROCESS_HPP

#include <hx_os_process.h>
#include <hx/Values.hpp>
#include <initializer_list>

namespace hx {

namespace detail {
inline Result<Buffer> ProcessArguments(
	std::initializer_list<const char *> arguments,
	hx_os_context *context) noexcept
{
	if (arguments.size() >= 65536)
		return Failure(HX_OS_LIMIT);

	auto buffer =
		Buffer::allocate((arguments.size() + 1) * sizeof(const char *),
			context, alignof(const char *));

	if (!buffer)
		return Failure(buffer.error());

	auto output = reinterpret_cast<const char **>(buffer.value().data());

	typedef const char *Argument;
	size_t index = 0;

	for (auto argument : arguments) {
		if (!argument)
			return Failure(HX_OS_INVALID);

		new (&output[index++]) Argument(argument);
	}

	new (&output[index]) Argument(NULL);

	return buffer;
}
} // namespace detail

/* Host-owned handles survive timeout/limit/error. Facades never kill, detach
 * or close them implicitly. Arguments are copied/consumed during the call.
 */
class Process {
public:
	explicit Process(hx_os_process *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	Result<void> spawn(const char *path,
		std::initializer_list<const char *> arguments = {},
		hx_os_context *context = NULL,
		const hx_os_process_options *options = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		auto args = detail::ProcessArguments(arguments, context);
		if (!args)
			return Failure(args.error());

		hx_os_error error = {};

		auto status = hx_os_process_spawn(context, path,
			reinterpret_cast<const char *const *>(
				args.value().data()),
			NULL, options, m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<hx_os_process_result> waitUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_process_result result = {};
		hx_os_error error = {};

		auto status = hx_os_process_wait_until(
			*m_owner_, deadline, &result, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	uint64_t identifier() const noexcept
	{
		return hx_os_process_identifier(*m_owner_);
	}

	Result<void> closeUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_process_close_until(m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_process **m_owner_;
};

class ProcessCapture {
public:
	explicit ProcessCapture(hx_os_process_capture *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	Result<void> run(const char *path,
		std::initializer_list<const char *> arguments,
		hx_os_deadline deadline, hx_os_context *context = NULL,
		const hx_os_process_capture_options *options = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		auto args = detail::ProcessArguments(arguments, context);
		if (!args)
			return Failure(args.error());

		hx_os_error error = {};
		auto status =
			hx_os_process_capture_run_with_options(context, path,
				reinterpret_cast<const char *const *>(
					args.value().data()),
				NULL, options, deadline, m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<hx_os_process_result> waitUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_process_result result = {};
		hx_os_error error = {};

		auto status = hx_os_process_capture_wait_until(
			*m_owner_, deadline, &result, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	Result<String> text(int stream = HX_OS_PROCESS_STDOUT,
		hx_os_encoding encoding = HX_OS_UTF8,
		hx_os_context *context = NULL) const noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_process_capture_text_encoded(
					*m_owner_, stream, encoding, out,
					error);
			},
			context);
	}

	Result<Buffer> bytes(int stream = HX_OS_PROCESS_STDOUT,
		hx_os_context *context = NULL) const noexcept
	{
		const void *data = NULL;
		size_t bytes = 0;

		auto status = hx_os_process_capture_view(
			*m_owner_, stream, &data, &bytes);
		if (status != HX_OS_OK)
			return Failure(status);

		auto result = Buffer::allocate(bytes, context);
		if (result && bytes)
			std::memcpy(result.value().data(), data, bytes);

		return result;
	}

	Result<void> raiseLimit(int stream, size_t bytes) noexcept
	{
		return Result<void>(hx_os_process_capture_raise_limit(
			*m_owner_, stream, bytes));
	}

	Result<void> discard(int stream) noexcept
	{
		return Result<void>(
			hx_os_process_capture_discard(*m_owner_, stream));
	}

	Result<void> closeUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_process_capture_close_until(
			m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_process_capture **m_owner_;
};
} // namespace hx
#endif
