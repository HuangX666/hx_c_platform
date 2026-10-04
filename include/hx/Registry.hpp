#ifndef HX_C_PLATFORM_CPP_REGISTRY_HPP
#define HX_C_PLATFORM_CPP_REGISTRY_HPP

#include <hx_os_registry.h>
#include <hx/Values.hpp>

namespace hx {

class Registry {
public:
	Registry() noexcept
	    : m_key_(NULL)
	{
	}

	static Result<Registry> open(int root, const char *path,
		unsigned flags = HX_OS_REGISTRY_READ,
		hx_os_context *context = NULL) noexcept
	{
		Registry result;
		hx_os_error error = {};

		auto status = hx_os_registry_open(
			context, root, path, flags, &result.m_key_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<Registry> user(const char *path,
		unsigned flags = HX_OS_REGISTRY_READ,
		hx_os_context *context = NULL) noexcept
	{
		return open(HX_OS_REGISTRY_CURRENT_USER, path, flags, context);
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
		std::swap(m_key_, other.m_key_);

		return *this;
	}

	~Registry() noexcept
	{
		(void)hx_os_registry_close(&m_key_, NULL);
	}

	Result<String> readString(const char *name = NULL,
		hx_os_context *context = NULL) const noexcept
	{
		if (!m_key_)
			return Failure(HX_OS_CLOSED);

		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_registry_read_string(
					m_key_, name, out, error);
			},
			context);
	}

	Result<uint32_t> readU32(const char *name = NULL) const noexcept
	{
		if (!m_key_)
			return Failure(HX_OS_CLOSED);

		uint32_t value = 0;
		hx_os_error error = {};
		auto status =
			hx_os_registry_read_u32(m_key_, name, &value, &error);

		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<void> writeString(const char *name, const char *value) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_registry_write_string(
			m_key_, name, value, &error);

		return detail::Status(status, error);
	}

	Result<void> writeU32(const char *name, uint32_t value) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_registry_write_u32(m_key_, name, value, &error);

		return detail::Status(status, error);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_registry_close(&m_key_, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_registry *m_key_;
};
} // namespace hx
#endif
