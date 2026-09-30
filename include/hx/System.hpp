#ifndef HX_C_PLATFORM_CPP_SYSTEM_HPP
#define HX_C_PLATFORM_CPP_SYSTEM_HPP
#include <hx_os_system.h>
#include <hx/Status.hpp>
#include <memory>
#include <vector>
namespace hx {
class System {
public:
	static std::vector<std::string> arguments()
	{
		hx_os_string_list *list = NULL;
		hx_os_error error;
		detail::CheckStatus(
			hx_os_process_arguments(NULL, &list, &error), error);
		std::unique_ptr<hx_os_string_list *,
			decltype(&hx_os_string_list_destroy)>
			cleanup(&list, hx_os_string_list_destroy);
		std::vector<std::string> result;
		result.reserve(hx_os_string_list_count(list));
		for (size_t i = 0; i < hx_os_string_list_count(list); ++i)
			result.emplace_back(hx_os_string_list_at(list, i));
		return result;
	}
	static std::string hostName()
	{
		hx_string text = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&text, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(
			hx_os_host_name_string(&text, &error), error);
		return std::string(hx_string_c_str(&text), text.size);
	}
	static uint64_t processId()
	{
		uint64_t identifier;
		hx_os_status status = hx_os_process_id(&identifier);
		if (status != HX_OS_OK)
			throw StatusError(status);
		return identifier;
	}
	static uint64_t cpuFeatures()
	{
		uint64_t features;
		hx_os_error error;
		detail::CheckStatus(
			hx_os_cpu_features_query(&features, &error), error);
		return features;
	}
};
} // namespace hx
#endif
