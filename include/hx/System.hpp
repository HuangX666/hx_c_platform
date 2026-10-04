#ifndef HX_C_PLATFORM_CPP_SYSTEM_HPP
#define HX_C_PLATFORM_CPP_SYSTEM_HPP

#include <hx_os_system.h>
#include <hx/Values.hpp>

namespace hx {
class StringList {
public:
	StringList() noexcept
	    : m_list_(NULL)
	{
	}

	StringList(const StringList &) = delete;

	StringList &operator=(const StringList &) = delete;

	StringList(StringList &&other) noexcept
	    : m_list_(other.m_list_)
	{
		other.m_list_ = NULL;
	}

	StringList &operator=(StringList &&other) noexcept
	{
		std::swap(m_list_, other.m_list_);

		return *this;
	}

	~StringList() noexcept
	{
		hx_os_string_list_destroy(&m_list_);
	}

	static StringList adopt(hx_os_string_list *list) noexcept
	{
		StringList value;

		value.m_list_ = list;

		return value;
	}

	size_t size() const noexcept
	{
		return hx_os_string_list_count(m_list_);
	}

	bool empty() const noexcept
	{
		return size() == 0;
	}

	const char *at(size_t index) const noexcept
	{
		return hx_os_string_list_at(m_list_, index);
	}

	const char *const *data() const noexcept
	{
		return hx_os_string_list_data(m_list_);
	}

private:
	hx_os_string_list *m_list_;
};

class System {
public:
	static Result<StringList> arguments(
		hx_os_context *context = NULL) noexcept
	{
		hx_os_string_list *list = NULL;
		hx_os_error error = {};

		auto status = hx_os_process_arguments(context, &list, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return StringList::adopt(list);
	}

	static Result<String> hostName(hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(hx_os_host_name_string, context);
	}

	static Result<uint64_t> processId() noexcept
	{
		uint64_t identifier = 0;

		auto status = hx_os_process_id(&identifier);
		if (status != HX_OS_OK)
			return Failure(status);

		return identifier;
	}

	static Result<uint64_t> cpuFeatures() noexcept
	{
		uint64_t features = 0;
		hx_os_error error = {};

		auto status = hx_os_cpu_features_query(&features, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return features;
	}
};
} // namespace hx
#endif
