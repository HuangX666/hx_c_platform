#ifndef HX_C_PLATFORM_CPP_LIBRARY_HPP
#define HX_C_PLATFORM_CPP_LIBRARY_HPP

#include <hx_os_library.h>
#include <hx/Values.hpp>

namespace hx {

class Library {
public:
	Library() noexcept
	    : m_library_(NULL)
	{
	}

	/**
	 * \brief Optional diagnostics use their own context and remain owned by
	 * the caller.
	 */
	static Result<Library> open(const char *path,
		unsigned flags = HX_OS_LIBRARY_LOCAL,
		hx_os_context *context = NULL,
		String *diagnostic = NULL) noexcept
	{
		Library result;
		hx_os_error error = {};

		auto status = hx_os_library_open_ex(context, path, flags,
			&result.m_library_,
			diagnostic ? diagnostic->native() : NULL, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	Library(const Library &) = delete;

	Library &operator=(const Library &) = delete;

	Library(Library &&other) noexcept
	    : m_library_(other.m_library_)
	{
		other.m_library_ = NULL;
	}

	Library &operator=(Library &&other) noexcept
	{
		std::swap(m_library_, other.m_library_);

		return *this;
	}

	~Library() noexcept
	{
		(void)hx_os_library_close(&m_library_, NULL);
	}

	template <typename Pointer>
	Result<Pointer> symbol(
		const char *name, String *diagnostic = NULL) const noexcept
	{
		static_assert(std::is_pointer<Pointer>::value,
			"Use a native pointer type");
		if (!m_library_)
			return Failure(HX_OS_CLOSED);

		Pointer result = NULL;
		hx_os_error error = {};

		auto status = hx_os_library_bind_ex(m_library_, name, &result,
			sizeof(result),
			diagnostic ? diagnostic->native() : NULL, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_library_close(&m_library_, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_library *m_library_;
};
} // namespace hx
#endif
