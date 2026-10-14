#ifndef HX_C_PLATFORM_CPP_COORDINATION_HPP
#define HX_C_PLATFORM_CPP_COORDINATION_HPP

#include <hx_os_coord.h>
#include <hx/Status.hpp>

namespace hx {
/* These facades borrow host-owned pointer variables, like hx::Thread.
 * They never destroy the resource on scope exit. Keep owners and callback
 * data alive after timeout/BUSY, and retry explicit cleanup when quiescent.
 */
class CancelToken {
public:
	explicit CancelToken(hx_os_cancel_token *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	static Result<CancelToken> create(hx_os_cancel_token *&owner,
		size_t maxCallbacks = 0, hx_os_context *context = NULL) noexcept
	{
		if (owner)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};
		auto status = hx_os_cancel_create(
			context, maxCallbacks, &owner, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return CancelToken(owner);
	}

	bool requested() const noexcept
	{
		return hx_os_cancel_requested(*m_owner_) != 0;
	}

	Result<void> request() const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_cancel_request(*m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<uint64_t> subscribe(
		hx_os_cancel_fn function, void *user = NULL) const noexcept
	{
		uint64_t id = 0;
		hx_os_error error = {};
		auto status = hx_os_cancel_subscribe(
			*m_owner_, function, user, &id, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return id;
	}

	Result<void> unsubscribeUntil(
		uint64_t id, hx_os_deadline deadline) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_cancel_unsubscribe_until(
			*m_owner_, id, deadline, &error);

		return detail::Status(status, error);
	}

	Result<uint64_t> bindTask(hx_os_task *task) const noexcept
	{
		uint64_t id = 0;
		hx_os_error error = {};
		auto status =
			hx_os_cancel_bind_task(*m_owner_, task, &id, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return id;
	}

	Result<uint64_t> bindRequest(hx_os_request *request) const noexcept
	{
		uint64_t id = 0;
		hx_os_error error = {};
		auto status = hx_os_cancel_bind_request(
			*m_owner_, request, &id, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return id;
	}

	Result<uint64_t> bindCoroutine(hx_os_coro *coroutine) const noexcept
	{
		uint64_t id = 0;
		hx_os_error error = {};
		auto status = hx_os_cancel_bind_coroutine(
			*m_owner_, coroutine, &id, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return id;
	}

	Result<void> waitUntil(hx_os_deadline deadline) const noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_cancel_wait_until(*m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> sleepUntil(hx_os_deadline deadline) const noexcept
	{
		if (!*m_owner_)
			return Failure(HX_OS_CLOSED);

		hx_os_error error = {};
		auto status = hx_os_sleep_cancellable_until(
			deadline, *m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_cancel_destroy(m_owner_, &error);

		return detail::Status(status, error);
	}

	hx_os_cancel_token *nativeHandle() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_os_cancel_token **m_owner_;
};

class WaitGroup {
public:
	explicit WaitGroup(hx_os_wait_group *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	static Result<WaitGroup> create(hx_os_wait_group *&owner,
		size_t initial = 0, hx_os_context *context = NULL) noexcept
	{
		if (owner)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};
		auto status = hx_os_wait_group_create(
			context, initial, &owner, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return WaitGroup(owner);
	}

	Result<void> add(size_t count = 1) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_wait_group_add(*m_owner_, count, &error);

		return detail::Status(status, error);
	}

	Result<void> done(size_t count = 1) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_wait_group_done(*m_owner_, count, &error);

		return detail::Status(status, error);
	}

	Result<void> waitUntil(hx_os_deadline deadline,
		hx_os_cancel_token *token = NULL) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_wait_group_wait_until(
			*m_owner_, deadline, token, &error);

		return detail::Status(status, error);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_wait_group_destroy(m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<void> closeUntil(hx_os_deadline deadline,
		hx_os_cancel_token *token = NULL) noexcept
	{
		if (!*m_owner_)
			return Result<void>();

		auto waited = waitUntil(deadline, token);
		if (!waited)
			return waited;

		return close();
	}

	hx_os_wait_group *nativeHandle() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_os_wait_group **m_owner_;
};

/* Move within the acquiring thread only. The lock owner must outlive guards.
 * Destruction unlocks; explicit unlock lets callers inspect native errors.
 */
class ReadWriteGuard {
	friend class ReadWriteLock;

public:
	ReadWriteGuard(ReadWriteGuard &&other) noexcept
	    : m_lock_(other.m_lock_)
	    , m_writing_(other.m_writing_)
	{
		other.m_lock_ = NULL;
	}

	ReadWriteGuard(const ReadWriteGuard &) = delete;
	ReadWriteGuard &operator=(const ReadWriteGuard &) = delete;

	~ReadWriteGuard() noexcept
	{
		(void)unlock();
	}

	Result<void> unlock() noexcept
	{
		if (!m_lock_)
			return Result<void>();

		hx_os_error error = {};
		auto status = m_writing_
			? hx_os_rwlock_write_unlock(m_lock_, &error)
			: hx_os_rwlock_read_unlock(m_lock_, &error);
		if (status == HX_OS_OK)
			m_lock_ = NULL;

		return detail::Status(status, error);
	}

private:
	hx_os_rwlock *m_lock_;
	bool m_writing_;

	ReadWriteGuard(hx_os_rwlock *lock, bool writing) noexcept
	    : m_lock_(lock)
	    , m_writing_(writing)
	{
	}
};

class ReadWriteLock {
public:
	explicit ReadWriteLock(hx_os_rwlock *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	static Result<ReadWriteLock> create(
		hx_os_rwlock *&owner, hx_os_context *context = NULL) noexcept
	{
		if (owner)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};
		auto status = hx_os_rwlock_create(context, &owner, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return ReadWriteLock(owner);
	}

	Result<ReadWriteGuard> readUntil(hx_os_deadline deadline,
		hx_os_cancel_token *token = NULL) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_rwlock_read_until(
			*m_owner_, deadline, token, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return ReadWriteGuard(*m_owner_, false);
	}

	Result<ReadWriteGuard> writeUntil(hx_os_deadline deadline,
		hx_os_cancel_token *token = NULL) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_rwlock_write_until(
			*m_owner_, deadline, token, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return ReadWriteGuard(*m_owner_, true);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_rwlock_destroy(m_owner_, &error);

		return detail::Status(status, error);
	}

	hx_os_rwlock *nativeHandle() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_os_rwlock **m_owner_;
};
} // namespace hx
#endif
