#ifndef HX_C_PLATFORM_CPP_REGISTRY_HPP
#define HX_C_PLATFORM_CPP_REGISTRY_HPP
#include <hx_os_registry.h>
#include <hx/Status.hpp>
#include <memory>
#include <utility>
namespace hx {
class Registry {
public:
	Registry(int root, const char *path,
		unsigned flags = HX_OS_REGISTRY_READ,
		hx_os_context *context = NULL)
	    : m_key_(NULL)
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_registry_open(context, root, path,
					    flags, &m_key_, &error),
			error);
	}
	static Registry user(const char *path,
		unsigned flags = HX_OS_REGISTRY_READ,
		hx_os_context *context = NULL)
	{
		return Registry(
			HX_OS_REGISTRY_CURRENT_USER, path, flags, context);
	}
	Registry(const Registry &) = delete;
	Registry &operator=(const Registry &) = delete;
	Registry(Registry &&other) noexcept
	    : m_key_(other.m_key_)
	{
		other.m_key_ = NULL;
	}
	Registry &operator=(Registry &&other) noexcept
	{
		if (this != &other) {
			(void)hx_os_registry_close(&m_key_, NULL);
			m_key_ = other.m_key_;
			other.m_key_ = NULL;
		}
		return *this;
	}
	~Registry()
	{
		(void)hx_os_registry_close(&m_key_, NULL);
	}
	std::string readString(const char *name = NULL) const
	{
		hx_string text = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&text, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(
			hx_os_registry_read_string(m_key_, name, &text, &error),
			error);
		return std::string(hx_string_c_str(&text), text.size);
	}
	uint32_t readU32(const char *name = NULL) const
	{
		uint32_t value;
		hx_os_error error;
		detail::CheckStatus(
			hx_os_registry_read_u32(m_key_, name, &value, &error),
			error);
		return value;
	}
	void writeString(const char *name, const char *value)
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_registry_write_string(
					    m_key_, name, value, &error),
			error);
	}
	void writeU32(const char *name, uint32_t value)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_registry_write_u32(m_key_, name, value, &error),
			error);
	}
	void close()
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_registry_close(&m_key_, &error), error);
	}

private:
	hx_os_registry *m_key_;
};
} // namespace hx
#endif
