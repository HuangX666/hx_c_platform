#ifndef HX_C_PLATFORM_CPP_COROUTINE_SYNC_HPP
#define HX_C_PLATFORM_CPP_COROUTINE_SYNC_HPP

#include <hx/Coroutines.hpp>

namespace hx {
namespace coro {
class Sync {
public:
	Sync() noexcept
	    : m_sync_(NULL)
	{
	}

	Sync(const Sync &other) noexcept
	    : m_sync_(other.m_sync_)
	{
		hx_os_coro_sync_retain(m_sync_);
	}

	Sync(Sync &&other) noexcept
	    : m_sync_(other.m_sync_)
	{
		other.m_sync_ = NULL;
	}

	Sync &operator=(Sync other) noexcept
	{
		std::swap(m_sync_, other.m_sync_);

		return *this;
	}

	~Sync() noexcept
	{
		hx_os_coro_sync_release(m_sync_);
	}

	hx_os_coro_sync *nativeHandle() const noexcept
	{
		return m_sync_;
	}

	Result<void> close() const noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_coro_sync_close(m_sync_, &error);

		return detail::Status(status, error);
	}

protected:
	hx_os_coro_sync *m_sync_;
};

/* Keep guards on the acquiring coroutine/native thread. Destruction unlocks,
 * never waits. A condition wait temporarily releases and then reacquires it.
 */
class Lock {
	friend class Mutex;
	friend class RwLock;

	typedef hx_os_status(HX_OS_CALL *Unlock)(
		hx_os_coro_sync *, hx_os_error *);

public:
	Lock(Lock &&other) noexcept
	    : m_sync_(other.m_sync_)
	    , m_unlock_(other.m_unlock_)
	{
		other.m_sync_ = NULL;
	}

	Lock(const Lock &) = delete;

	Lock &operator=(const Lock &) = delete;

	~Lock() noexcept
	{
		(void)unlock();
	}

	hx_os_coro_sync *nativeHandle() const noexcept
	{
		return m_sync_;
	}

	Result<void> unlock() noexcept
	{
		if (!m_sync_)
			return Result<void>();

		hx_os_error error = {};

		auto status = m_unlock_(m_sync_, &error);
		if (status == HX_OS_OK) {
			hx_os_coro_sync_release(m_sync_);
			m_sync_ = NULL;
		}

		return detail::Status(status, error);
	}

private:
	hx_os_coro_sync *m_sync_;
	Unlock m_unlock_;

	Lock(hx_os_coro_sync *sync, Unlock unlock) noexcept
	    : m_sync_(sync)
	    , m_unlock_(unlock)
	{
		hx_os_coro_sync_retain(m_sync_);
	}
};

class Mutex : public Sync {
public:
	static Result<Mutex> create(const Runtime &runtime) noexcept
	{
		Mutex value;
		hx_os_error error = {};

		auto status = hx_os_coro_mutex_create(
			runtime.nativeHandle(), &value.m_sync_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<void> lockUntil(hx_os_deadline deadline =
				       HX_OS_DEADLINE_INFINITE) const noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_coro_mutex_lock_until(m_sync_, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> unlock() const noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_coro_mutex_unlock(m_sync_, &error);

		return detail::Status(status, error);
	}

	Result<Lock> acquireUntil(
		hx_os_deadline deadline =
			HX_OS_DEADLINE_INFINITE) const noexcept
	{
		auto status = lockUntil(deadline);
		if (!status)
			return Failure(status.error());

		return Lock(m_sync_, hx_os_coro_mutex_unlock);
	}
};

class Event : public Sync {
public:
	static Result<Event> create(const Runtime &runtime, bool manual = true,
		bool set = false) noexcept
	{
		Event value;
		hx_os_error error = {};

		auto status = hx_os_coro_event_create(runtime.nativeHandle(),
			manual, set, &value.m_sync_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<void> waitUntil(hx_os_deadline deadline =
				       HX_OS_DEADLINE_INFINITE) const noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_coro_event_wait_until(m_sync_, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> set() const noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_coro_event_set(m_sync_, &error);

		return detail::Status(status, error);
	}

	Result<void> reset() const noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_coro_event_reset(m_sync_, &error);

		return detail::Status(status, error);
	}
};

class Semaphore : public Sync {
public:
	static Result<Semaphore> create(
		const Runtime &runtime, size_t initial, size_t maximum) noexcept
	{
		Semaphore value;
		hx_os_error error = {};
		auto status =
			hx_os_coro_semaphore_create(runtime.nativeHandle(),
				initial, maximum, &value.m_sync_, &error);

		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<void> waitUntil(hx_os_deadline deadline =
				       HX_OS_DEADLINE_INFINITE) const noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_coro_semaphore_wait_until(
			m_sync_, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> post(size_t count = 1) const noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_coro_semaphore_post(m_sync_, count, &error);

		return detail::Status(status, error);
	}
};

class RwLock : public Sync {
public:
	static Result<RwLock> create(const Runtime &runtime) noexcept
	{
		RwLock value;
		hx_os_error error = {};

		auto status = hx_os_coro_rwlock_create(
			runtime.nativeHandle(), &value.m_sync_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<Lock> readUntil(hx_os_deadline deadline =
				       HX_OS_DEADLINE_INFINITE) const noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_coro_rwlock_read_until(m_sync_, deadline, &error);

		if (status != HX_OS_OK)
			return Failure(status, error);

		return Lock(m_sync_, hx_os_coro_rwlock_read_unlock);
	}

	Result<Lock> writeUntil(hx_os_deadline deadline =
					HX_OS_DEADLINE_INFINITE) const noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_coro_rwlock_write_until(
			m_sync_, deadline, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return Lock(m_sync_, hx_os_coro_rwlock_write_unlock);
	}
};

class Condition : public Sync {
public:
	static Result<Condition> create(const Runtime &runtime) noexcept
	{
		Condition value;
		hx_os_error error = {};

		auto status = hx_os_coro_condition_create(
			runtime.nativeHandle(), &value.m_sync_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<void> waitUntil(const Lock &lock,
		hx_os_deadline deadline =
			HX_OS_DEADLINE_INFINITE) const noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_coro_condition_wait_until(
			m_sync_, lock.nativeHandle(), deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> notifyOne() const noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_coro_condition_notify_one(m_sync_, &error);

		return detail::Status(status, error);
	}

	Result<void> notifyAll() const noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_coro_condition_notify_all(m_sync_, &error);

		return detail::Status(status, error);
	}
};

template <typename T> class Local {
public:
	Local() noexcept
	    : m_key_(NULL)
	{
	}

	Local(const Local &other) noexcept
	    : m_key_(other.m_key_)
	{
		hx_os_coro_local_key_retain(m_key_);
	}

	Local(Local &&other) noexcept
	    : m_key_(other.m_key_)
	{
		other.m_key_ = NULL;
	}

	Local &operator=(Local other) noexcept
	{
		std::swap(m_key_, other.m_key_);

		return *this;
	}

	~Local() noexcept
	{
		hx_os_coro_local_key_release(m_key_);
	}

	static Result<Local> create(hx_os_context *context = NULL) noexcept
	{
		Local value;
		hx_os_error error = {};

		auto status = hx_os_coro_local_key_create(
			context, cleanup, NULL, &value.m_key_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	T *get() const noexcept
	{
		return static_cast<T *>(hx_os_coro_local_get(m_key_));
	}

	template <typename U> Result<void> set(U &&input) const noexcept
	{
		static_assert(std::is_nothrow_constructible<T, U &&>::value &&
				std::is_nothrow_destructible<T>::value,
			"Coroutine locals require noexcept "
			"construction/destruction");
		if (!m_key_ || !hx_os_coro_current())
			return Failure(HX_OS_INVALID);

		void *memory = NULL;
		hx_os_error error = {};

		auto status = hx_os_memory_alloc(
			hx_os_coro_runtime_context(
				hx_os_coro_current_runtime()),
			sizeof(T), alignof(T), &memory, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		auto *value = new (memory) T(std::forward<U>(input));

		status = hx_os_coro_local_set(m_key_, value, &error);
		if (status != HX_OS_OK)
			cleanup(value, NULL);

		return detail::Status(status, error);
	}

	Result<void> clear() const noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_coro_local_set(m_key_, NULL, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_coro_local_key *m_key_;
	static void HX_OS_CALL cleanup(void *memory, void *) noexcept
	{
		static_cast<T *>(memory)->~T();

		hx_os_memory_free(memory);
	}
};
} // namespace coro
} // namespace hx
#endif
