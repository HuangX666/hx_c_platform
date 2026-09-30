#ifndef HX_C_PLATFORM_CPP_THREAD_HPP
#define HX_C_PLATFORM_CPP_THREAD_HPP

#include <hx_os_thread.h>
#include <hx/Status.hpp>
#include <memory>

namespace hx {

class Semaphore {
public:
	explicit Semaphore(uint32_t initial = 0, uint32_t maximum = 1,
		hx_os_context *context = NULL)
	    : m_handle_(NULL, hx_os_semaphore_destroy)
	{
		hx_os_semaphore *handle = NULL;
		hx_os_error error;
		detail::CheckStatus(hx_os_semaphore_create(context, initial,
					    maximum, &handle, &error),
			error);
		m_handle_.reset(handle);
	}
	Semaphore(Semaphore &&) noexcept = default;
	Semaphore &operator=(Semaphore &&) noexcept = default;
	Semaphore(const Semaphore &) = delete;
	Semaphore &operator=(const Semaphore &) = delete;
	void post(uint32_t count = 1)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_semaphore_post(m_handle_.get(), count, &error),
			error);
	}
	hx_os_status waitUntil(
		hx_os_deadline deadline, hx_os_error *error = NULL) noexcept
	{
		return hx_os_semaphore_wait_until(
			m_handle_.get(), deadline, error);
	}
	hx_os_semaphore *nativeHandle() const noexcept
	{
		return m_handle_.get();
	}

private:
	std::unique_ptr<hx_os_semaphore, decltype(&hx_os_semaphore_destroy)>
		m_handle_;
};

/*
 * Non-owning facade over a host-owned pointer variable, like ThreadPool.
 * The pointer variable and borrowed callback data outlive unfinished work.
 * closeUntil joins then destroys; failure retains the original owner.
 * Facade destruction does not wait, detach or discard that owner.
 */
class Thread {
public:
	explicit Thread(hx_os_thread *&owner) noexcept
	    : m_owner_(&owner)
	{
	}
	static Thread create(hx_os_thread *&owner, hx_os_thread_fn function,
		void *user = NULL, size_t stackSize = 0,
		hx_os_context *context = NULL)
	{
		if (owner)
			throw StatusError(HX_OS_BUSY);
		hx_os_thread_options options = HX_OS_THREAD_OPTIONS_INIT;
		options.stack_size = stackSize;
		hx_os_error error;
		hx_os_status status = stackSize
			? hx_os_thread_create_with_options(context, &options,
				  function, user, &owner, &error)
			: hx_os_thread_create(
				  context, function, user, &owner, &error);
		detail::CheckStatus(status, error);
		return Thread(owner);
	}
	static void setCurrentName(const char *name)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_thread_set_current_name(name, &error), error);
	}
	void setName(const char *name)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_thread_set_name(*m_owner_, name, &error), error);
	}
	hx_os_status joinUntil(hx_os_deadline deadline, void **result = NULL,
		hx_os_error *error = NULL) const noexcept
	{
		return hx_os_thread_join_until(
			*m_owner_, deadline, result, error);
	}
	hx_os_status closeUntil(
		hx_os_deadline deadline, hx_os_error *error = NULL) noexcept
	{
		hx_os_status status;
		if (!*m_owner_) {
			hx_os_error_clear(error);
			return HX_OS_OK;
		}
		status = hx_os_thread_join_until(
			*m_owner_, deadline, NULL, error);
		return status == HX_OS_OK
			? hx_os_thread_destroy(m_owner_, error)
			: status;
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
