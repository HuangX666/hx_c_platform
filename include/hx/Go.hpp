#ifndef HX_C_PLATFORM_CPP_GO_HPP
#define HX_C_PLATFORM_CPP_GO_HPP

#include <hx/Coroutines.hpp>

namespace hx {
namespace coro {
class Scope {
public:
	Scope() noexcept
	    : m_binding_(NULL)
	{
	}

	Scope(Scope &&other) noexcept
	    : m_binding_(other.m_binding_)
	{
		other.m_binding_ = NULL;
	}

	Scope(const Scope &) = delete;

	Scope &operator=(const Scope &) = delete;

	~Scope() noexcept
	{
		(void)hx_os_coro_binding_close(&m_binding_, NULL);
	}

	static Result<Scope> enter(const Runtime &runtime) noexcept
	{
		Scope value;
		hx_os_error error = {};

		auto status = hx_os_coro_binding_create(
			runtime.nativeHandle(), &value.m_binding_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_coro_binding_close(&m_binding_, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_coro_binding *m_binding_;
};

/* A small expression builder; no static scheduler or implicit worker exists.
 * The runtime records failures even when the returned Result is discarded.
 */
template <typename Owner> class Go {
public:
	explicit Go(Owner &runtime) noexcept
	    : m_runtime_(runtime)
	{
	}

	template <typename F>
	Result<void> operator-(F &&function) const noexcept
	{
		return m_runtime_.spawn(std::forward<F>(function));
	}

private:
	Owner &m_runtime_;
};

template <typename Owner> Go<Owner> makeGo(Owner &owner) noexcept
{
	return Go<Owner>(owner);
}

class CurrentGo {
public:
	template <typename F>
	Result<void> operator-(F &&function) const noexcept
	{
		hx_os_coro_runtime *owner = hx_os_coro_current_runtime();

		return Runtime(owner).spawn(std::forward<F>(function));
	}
};

/* Convenience channel bound to the explicit native Scope/current coroutine.
 * Construction errors remain inspectable; send/receive return the same error.
 */
template <typename T> class Chan {
public:
	explicit Chan(size_t capacity = 0) noexcept
	    : m_value_(create(capacity))
	{
	}

	Chan(const Chan &other) noexcept
	    : m_value_(copy(other))
	{
	}

	Chan(Chan &&other) noexcept
	    : m_value_(std::move(other.m_value_))
	{
	}

	Chan &operator=(Chan other) noexcept
	{
		std::swap(m_value_, other.m_value_);

		return *this;
	}

	explicit operator bool() const noexcept
	{
		return m_value_.ok();
	}

	const hx_os_error &error() const noexcept
	{
		return m_value_.error();
	}

	Result<void> send(const T &value,
		hx_os_deadline deadline =
			HX_OS_DEADLINE_INFINITE) const noexcept
	{
		return m_value_ ? m_value_.value().send(value, deadline)
				: Result<void>(Failure(m_value_.error()));
	}

	Result<void> send(T &&value,
		hx_os_deadline deadline =
			HX_OS_DEADLINE_INFINITE) const noexcept
	{
		return m_value_
			? m_value_.value().send(std::move(value), deadline)
			: Result<void>(Failure(m_value_.error()));
	}

	Result<T> receive(hx_os_deadline deadline =
				  HX_OS_DEADLINE_INFINITE) const noexcept
	{
		return m_value_ ? m_value_.value().receive(deadline)
				: Result<T>(Failure(m_value_.error()));
	}

	Result<void> close() const noexcept
	{
		return m_value_ ? m_value_.value().close()
				: Result<void>(Failure(m_value_.error()));
	}

	Result<void> operator<<(const T &value) const noexcept
	{
		return send(value);
	}

	Result<void> operator<<(T &&value) const noexcept
	{
		return send(std::move(value));
	}

	Result<void> operator>>(T &value) const noexcept
	{
		return m_value_ ? (m_value_.value() >> value)
				: Result<void>(Failure(m_value_.error()));
	}

private:
	Result<Channel<T>> m_value_;
	static Result<Channel<T>> create(size_t capacity) noexcept
	{
		hx_os_coro_runtime *owner = hx_os_coro_current_runtime();

		return Channel<T>::create(Runtime(owner), capacity);
	}

	static Result<Channel<T>> copy(const Chan &other) noexcept
	{
		if (!other.m_value_)
			return Failure(other.m_value_.error());

		return other.m_value_.value();
	}
};
} // namespace coro
} // namespace hx

/* Opt-in header: hx_go(runtime) [capture]() noexcept { ... };
 * Named functions work too: hx_go(runtime) function;
 */
#define hx_go(runtime) ::hx::coro::makeGo(runtime) -
#define hx_go_here ::hx::coro::CurrentGo() -
#endif
