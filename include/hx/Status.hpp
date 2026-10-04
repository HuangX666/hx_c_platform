#ifndef HX_C_PLATFORM_CPP_STATUS_HPP
#define HX_C_PLATFORM_CPP_STATUS_HPP

#include <hx_os_base.h>
#include <cassert>
#include <new>
#include <type_traits>
#include <utility>

namespace hx {
struct Failure {
	hx_os_error detail;

	explicit Failure(hx_os_status status) noexcept
	    : detail{status, HX_OS_ERROR_NONE, 0, 0}
	{
	}

	explicit Failure(const hx_os_error &error) noexcept
	    : detail(error)
	{
	}

	Failure(hx_os_status status, const hx_os_error &error) noexcept
	    : detail(error)
	{
		detail.status = status;
	}
};

/* Move-only, allocation-free result. value() requires hasValue(); get()
 * returns NULL otherwise. I/O may carry progress together with an error.
 * Moving consumes the source. Resource values use nonthrowing moves.
 */
template <typename T> class Result {
	static_assert(std::is_nothrow_move_constructible<T>::value,
		"Result values need noexcept move construction");
	static_assert(std::is_nothrow_destructible<T>::value,
		"Result values need noexcept destruction");

public:
	Result(Failure failure) noexcept
	    : m_error_(failure.detail)
	    , m_hasValue_(false)
	{
	}

	Result(T &&value) noexcept
	    : m_error_{HX_OS_OK, HX_OS_ERROR_NONE, 0, 0}
	    , m_hasValue_(true)
	{
		new (&m_storage_) T(std::move(value));
	}

	Result(const T &value) noexcept
	    : m_error_{HX_OS_OK, HX_OS_ERROR_NONE, 0, 0}
	    , m_hasValue_(true)
	{
		static_assert(std::is_nothrow_copy_constructible<T>::value,
			"Copy Result values without failure, or move them");
		new (&m_storage_) T(value);
	}

	Result(Result &&other) noexcept
	    : m_error_(other.m_error_)
	    , m_hasValue_(other.m_hasValue_)
	{
		if (m_hasValue_)
			new (&m_storage_) T(std::move(other.value()));

		other.clear();
	}

	Result &operator=(Result &&other) noexcept
	{
		if (this != &other) {
			clear();
			m_error_ = other.m_error_;
			m_hasValue_ = other.m_hasValue_;
			if (m_hasValue_)
				new (&m_storage_) T(std::move(other.value()));

			other.clear();
		}

		return *this;
	}

	Result(const Result &) = delete;

	Result &operator=(const Result &) = delete;

	~Result() noexcept
	{
		clear();
	}

	template <typename U>
	static Result withValue(U &&value, hx_os_status status,
		const hx_os_error &error) noexcept
	{
		Result result(std::forward<U>(value));
		result.m_error_ = error;
		result.m_error_.status = status;

		return result;
	}

	bool ok() const noexcept
	{
		return m_hasValue_ && m_error_.status == HX_OS_OK;
	}

	explicit operator bool() const noexcept
	{
		return ok();
	}

	bool hasValue() const noexcept
	{
		return m_hasValue_;
	}

	hx_os_status status() const noexcept
	{
		return m_error_.status;
	}

	const hx_os_error &error() const noexcept
	{
		return m_error_;
	}

	T &value() & noexcept
	{
		assert(m_hasValue_);

		return *reinterpret_cast<T *>(&m_storage_);
	}

	const T &value() const & noexcept
	{
		assert(m_hasValue_);

		return *reinterpret_cast<const T *>(&m_storage_);
	}

	T &&value() && noexcept
	{
		assert(m_hasValue_);

		return std::move(*reinterpret_cast<T *>(&m_storage_));
	}

	T *get() noexcept
	{
		return m_hasValue_ ? &value() : NULL;
	}

	const T *get() const noexcept
	{
		return m_hasValue_ ? &value() : NULL;
	}

private:
	alignas(T) unsigned char m_storage_[sizeof(T)];
	hx_os_error m_error_;
	bool m_hasValue_;

	void clear() noexcept
	{
		if (m_hasValue_)
			value().~T();

		m_hasValue_ = false;
		m_error_ = {HX_OS_CLOSED, HX_OS_ERROR_NONE, 0, 0};
	}
};

template <typename T> class Result<T &> {
public:
	Result(T &value) noexcept
	    : m_value_(&value)
	    , m_error_{HX_OS_OK, HX_OS_ERROR_NONE, 0, 0}
	{
	}

	Result(Failure failure) noexcept
	    : m_value_(NULL)
	    , m_error_(failure.detail)
	{
	}

	bool ok() const noexcept
	{
		return m_value_ && m_error_.status == HX_OS_OK;
	}

	explicit operator bool() const noexcept
	{
		return ok();
	}

	bool hasValue() const noexcept
	{
		return m_value_ != NULL;
	}

	hx_os_status status() const noexcept
	{
		return m_error_.status;
	}

	const hx_os_error &error() const noexcept
	{
		return m_error_;
	}

	T &value() const noexcept
	{
		assert(m_value_);

		return *m_value_;
	}

	T *get() const noexcept
	{
		return m_value_;
	}

private:
	T *m_value_;
	hx_os_error m_error_;
};

template <> class Result<void> {
public:
	Result() noexcept
	    : m_error_{HX_OS_OK, HX_OS_ERROR_NONE, 0, 0}
	{
	}

	Result(Failure failure) noexcept
	    : m_error_(failure.detail)
	{
	}

	explicit Result(hx_os_status status) noexcept
	    : m_error_{status, HX_OS_ERROR_NONE, 0, 0}
	{
	}

	bool ok() const noexcept
	{
		return m_error_.status == HX_OS_OK;
	}

	explicit operator bool() const noexcept
	{
		return ok();
	}

	hx_os_status status() const noexcept
	{
		return m_error_.status;
	}

	const hx_os_error &error() const noexcept
	{
		return m_error_;
	}

private:
	hx_os_error m_error_;
};

namespace detail {
inline Result<void> Status(
	hx_os_status status, const hx_os_error &error) noexcept
{
	return status == HX_OS_OK ? Result<void>()
				  : Result<void>(Failure(status, error));
}
} // namespace detail
} // namespace hx
#endif
