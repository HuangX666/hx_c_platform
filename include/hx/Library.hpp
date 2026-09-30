#ifndef HX_C_PLATFORM_CPP_LIBRARY_HPP
#define HX_C_PLATFORM_CPP_LIBRARY_HPP

#include <hx_os_library.h>
#include <hx/Status.hpp>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace hx {

class LibraryError : public StatusError {
public:
	LibraryError(const hx_os_error &error, const char *message)
	    : StatusError(error)
	    , m_message_(message[0] ? message : hx_os_status_name(error.status))
	{
	}
	const char *what() const noexcept override
	{
		return m_message_.c_str();
	}

private:
	std::string m_message_;
};

class Library {
public:
	explicit Library(const char *path, unsigned flags = HX_OS_LIBRARY_LOCAL,
		hx_os_context *context = NULL)
	    : m_library_(NULL)
	{
		hx_string diagnostic = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&diagnostic, hx_string_destroy);
		hx_os_error error;
		if (hx_os_library_open_ex(context, path, flags, &m_library_,
			    &diagnostic, &error) != HX_OS_OK)
			throw LibraryError(error, hx_string_c_str(&diagnostic));
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
	~Library()
	{
		(void)hx_os_library_close(&m_library_, NULL);
	}
	template <typename Pointer> Pointer symbol(const char *name) const
	{
		static_assert(std::is_pointer<Pointer>::value,
			"Library::symbol requires a pointer type");
		static_assert(sizeof(Pointer) == sizeof(void *),
			"Native symbol pointer representation is unsupported");
		hx_string diagnostic = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&diagnostic, hx_string_destroy);
		hx_os_error error;
		void *address;
		Pointer result;
		if (hx_os_library_symbol_ex(m_library_, name, &address,
			    &diagnostic, &error) != HX_OS_OK)
			throw LibraryError(error, hx_string_c_str(&diagnostic));
		std::memcpy(&result, &address, sizeof(result));
		return result;
	}
	void close()
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_library_close(&m_library_, &error), error);
	}

private:
	hx_os_library *m_library_;
};

} // namespace hx
#endif
