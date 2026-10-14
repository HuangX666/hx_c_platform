#ifndef HX_C_PLATFORM_CPP_TEMP_HPP
#define HX_C_PLATFORM_CPP_TEMP_HPP

#include <hx_os_temp.h>
#include <hx/Status.hpp>

namespace hx {
/**
 * \brief Durable caller-owned handle permits cleanup retry after I/O failure.
 */
class Temp {
public:
	explicit Temp(hx_os_temp *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	Result<void> createFile(const char *parent = NULL,
		const char *prefix = NULL,
		hx_os_context *context = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);
		hx_os_error error = {};
		auto status = hx_os_temp_create_file(
			context, parent, prefix, m_owner_, &error);
		return detail::Status(status, error);
	}

	Result<void> createDirectory(const char *parent = NULL,
		const char *prefix = NULL,
		hx_os_context *context = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);
		hx_os_error error = {};
		auto status = hx_os_temp_create_directory(
			context, parent, prefix, m_owner_, &error);
		return detail::Status(status, error);
	}

	const char *path() const noexcept
	{
		return hx_os_temp_path(*m_owner_);
	}

	hx_os_file *file() const noexcept
	{
		return hx_os_temp_file(*m_owner_);
	}

	Result<void> publish(
		const char *destination, bool replace = false) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_temp_publish(
			*m_owner_, destination, replace, &error);
		return detail::Status(status, error);
	}

	Result<void> keep() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_temp_keep(*m_owner_, &error);
		return detail::Status(status, error);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_temp_close(m_owner_, &error);
		return detail::Status(status, error);
	}

private:
	hx_os_temp **m_owner_;
};
} // namespace hx
#endif
