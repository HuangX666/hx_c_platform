#ifndef HX_C_PLATFORM_CPP_SHARED_MEMORY_HPP
#define HX_C_PLATFORM_CPP_SHARED_MEMORY_HPP

#include <hx_os_shared_memory.h>
#include <hx/Status.hpp>

namespace hx {
class SharedMemory {
public:
	explicit SharedMemory(hx_os_shared_memory *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	Result<void> create(const char *name, size_t bytes,
		const char *directory = NULL,
		hx_os_context *context = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);
		hx_os_error error = {};
		auto status = directory
			? hx_os_shared_memory_create_at(context, directory,
				  name, bytes, m_owner_, &error)
			: hx_os_shared_memory_create(
				  context, name, bytes, m_owner_, &error);
		return detail::Status(status, error);
	}

	Result<void> open(const char *name, const char *directory = NULL,
		hx_os_context *context = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);
		hx_os_error error = {};
		auto status = directory
			? hx_os_shared_memory_open_at(
				  context, directory, name, m_owner_, &error)
			: hx_os_shared_memory_open(
				  context, name, m_owner_, &error);
		return detail::Status(status, error);
	}

	void *data() const noexcept
	{
		return hx_os_shared_memory_data(*m_owner_);
	}

	size_t size() const noexcept
	{
		return hx_os_shared_memory_size(*m_owner_);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_shared_memory_close(m_owner_, &error);
		return detail::Status(status, error);
	}

private:
	hx_os_shared_memory **m_owner_;
};
} // namespace hx
#endif
