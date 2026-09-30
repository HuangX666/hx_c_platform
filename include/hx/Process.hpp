#ifndef HX_C_PLATFORM_CPP_PROCESS_HPP
#define HX_C_PLATFORM_CPP_PROCESS_HPP
#include <hx_os_process.h>
#include <hx/Status.hpp>
#include <memory>
#include <vector>
namespace hx {
namespace detail {
inline std::vector<const char *> process_arguments(
	const std::vector<std::string> &arguments)
{
	std::vector<const char *> result;
	result.reserve(arguments.size() + 1);
	for (const auto &argument : arguments) {
		if (argument.find('\0') != std::string::npos)
			throw StatusError(HX_OS_INVALID);
		result.push_back(argument.c_str());
	}
	result.push_back(NULL);
	return result;
}
} // namespace detail
/* Host-owned handles remain reachable on timeout or exception. Facades do
 * not close, detach or kill in their destructors. The owner variable must
 * outlive the facade and must be explicitly closed.
 */
class Process {
public:
	explicit Process(hx_os_process *&owner) noexcept
	    : m_owner_(&owner)
	{
	}
	void spawn(const char *path,
		const std::vector<std::string> &arguments = {},
		hx_os_context *context = NULL,
		const hx_os_process_options *options = NULL)
	{
		if (*m_owner_)
			throw StatusError(HX_OS_BUSY);
		auto args = detail::process_arguments(arguments);
		hx_os_error error;
		detail::CheckStatus(
			hx_os_process_spawn(context, path, args.data(), NULL,
				options, m_owner_, &error),
			error);
	}
	hx_os_status waitUntil(hx_os_deadline deadline,
		hx_os_process_result &result,
		hx_os_error *error = NULL) noexcept
	{
		return hx_os_process_wait_until(
			*m_owner_, deadline, &result, error);
	}
	uint64_t identifier() const noexcept
	{
		return hx_os_process_identifier(*m_owner_);
	}
	hx_os_status closeUntil(
		hx_os_deadline deadline, hx_os_error *error = NULL) noexcept
	{
		return hx_os_process_close_until(m_owner_, deadline, error);
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
	hx_os_status run(const char *path,
		const std::vector<std::string> &arguments,
		hx_os_deadline deadline, hx_os_error *error = NULL,
		hx_os_context *context = NULL,
		const hx_os_process_capture_options *options = NULL)
	{
		if (*m_owner_)
			throw StatusError(HX_OS_BUSY);
		auto args = detail::process_arguments(arguments);
		return hx_os_process_capture_run_with_options(context, path,
			args.data(), NULL, options, deadline, m_owner_, error);
	}
	hx_os_status waitUntil(hx_os_deadline deadline,
		hx_os_process_result *result = NULL,
		hx_os_error *error = NULL) noexcept
	{
		return hx_os_process_capture_wait_until(
			*m_owner_, deadline, result, error);
	}
	std::string text(int stream = HX_OS_PROCESS_STDOUT,
		hx_os_encoding encoding = HX_OS_UTF8) const
	{
		hx_string value = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&value, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(
			hx_os_process_capture_text_encoded(
				*m_owner_, stream, encoding, &value, &error),
			error);
		return std::string(hx_string_c_str(&value), value.size);
	}
	std::vector<unsigned char> bytes(
		int stream = HX_OS_PROCESS_STDOUT) const
	{
		const void *data;
		size_t size;
		hx_os_status status = hx_os_process_capture_view(
			*m_owner_, stream, &data, &size);
		if (status != HX_OS_OK)
			throw StatusError(status);
		if (!size)
			return {};
		const auto *begin = static_cast<const unsigned char *>(data);
		return std::vector<unsigned char>(begin, begin + size);
	}
	void raiseLimit(int stream, size_t bytes)
	{
		hx_os_status status = hx_os_process_capture_raise_limit(
			*m_owner_, stream, bytes);
		if (status != HX_OS_OK)
			throw StatusError(status);
	}
	void discard(int stream)
	{
		hx_os_status status =
			hx_os_process_capture_discard(*m_owner_, stream);
		if (status != HX_OS_OK)
			throw StatusError(status);
	}
	hx_os_status closeUntil(
		hx_os_deadline deadline, hx_os_error *error = NULL) noexcept
	{
		return hx_os_process_capture_close_until(
			m_owner_, deadline, error);
	}

private:
	hx_os_process_capture **m_owner_;
};
} // namespace hx
#endif
