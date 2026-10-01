#ifndef HX_C_PLATFORM_CPP_THREAD_HPP
#define HX_C_PLATFORM_CPP_THREAD_HPP
#include <hx_os_thread.h>
#include <hx/Status.hpp>
namespace hx {

class Semaphore {
public:
	Semaphore() noexcept
	    : m_handle_(NULL)
	{
	}
	static Result<Semaphore> create(uint32_t initial = 0,
		uint32_t maximum = 1, hx_os_context *context = NULL) noexcept
	{
		Semaphore result;
		hx_os_error error = {};
		auto status = hx_os_semaphore_create(
			context, initial, maximum, &result.m_handle_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}
	Semaphore(const Semaphore &) = delete;
	Semaphore &operator=(const Semaphore &) = delete;
	Semaphore(Semaphore &&other) noexcept
	    : m_handle_(other.m_handle_)
	{
		other.m_handle_ = NULL;
	}
	Semaphore &operator=(Semaphore &&other) noexcept
	{
		std::swap(m_handle_, other.m_handle_);
		return *this;
	}
	~Semaphore() noexcept
	{
		hx_os_semaphore_destroy(m_handle_);
	}
	Result<void> post(uint32_t count = 1) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_semaphore_post(m_handle_, count, &error);
		return detail::Status(status, error);
	}
	Result<void> waitUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_semaphore_wait_until(m_handle_, deadline, &error);
		return detail::Status(status, error);
	}
	hx_os_semaphore *nativeHandle() const noexcept
	{
		return m_handle_;
	}

private:
	hx_os_semaphore *m_handle_;
};
/* Host-owned pointer and borrowed data outlive unfinished work. Destruction
 * of the facade never waits, detaches or discards a timed-out owner.
 */
class Thread {
public:
	explicit Thread(hx_os_thread *&owner) noexcept
	    : m_owner_(&owner)
	{
	}
	static Result<Thread> create(hx_os_thread *&owner,
		hx_os_thread_fn function, void *user = NULL,
		size_t stackSize = 0, hx_os_context *context = NULL) noexcept
	{
		if (owner)
			return Failure(HX_OS_BUSY);
		hx_os_thread_options options = HX_OS_THREAD_OPTIONS_INIT;
		options.stack_size = stackSize;
		hx_os_error error = {};
		auto status = stackSize
			? hx_os_thread_create_with_options(context, &options,
				  function, user, &owner, &error)
			: hx_os_thread_create(
				  context, function, user, &owner, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return Thread(owner);
	}
	static Result<void> setCurrentName(const char *name) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_thread_set_current_name(name, &error);
		return detail::Status(status, error);
	}
	Result<void> setName(const char *name) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_thread_set_name(*m_owner_, name, &error);
		return detail::Status(status, error);
	}
	Result<void> setPriority(int level) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_thread_set_priority(*m_owner_, level, &error);
		return detail::Status(status, error);
	}
	Result<void> setAffinity(uint64_t mask) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_thread_set_affinity(*m_owner_, mask, &error);
		return detail::Status(status, error);
	}
	Result<void *> joinUntil(hx_os_deadline deadline) const noexcept
	{
		void *value = NULL;
		hx_os_error error = {};
		auto status = hx_os_thread_join_until(
			*m_owner_, deadline, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return value;
	}
	Result<void> closeUntil(hx_os_deadline deadline) noexcept
	{
		if (!*m_owner_)
			return Result<void>();
		hx_os_error error = {};
		auto status = hx_os_thread_join_until(
			*m_owner_, deadline, NULL, &error);
		if (status == HX_OS_OK)
			status = hx_os_thread_destroy(m_owner_, &error);
		return detail::Status(status, error);
	}
	hx_os_thread *nativeHandle() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_os_thread **m_owner_;
};
} // namespace hx
#endif
