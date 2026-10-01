#ifndef HX_C_PLATFORM_CPP_CALENDAR_HPP
#define HX_C_PLATFORM_CPP_CALENDAR_HPP
#include <hx_os_calendar.h>
#include <hx/Status.hpp>
#include <memory>
#include <utility>
namespace hx {
class Calendar {
public:
	static std::string utc(int64_t milliseconds, const char *spec = NULL)
	{
		hx_string text = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&text, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(hx_os_wall_time_format_utc_string(
					    milliseconds, spec, &text, &error),
			error);
		return std::string(hx_string_c_str(&text), text.size);
	}
	static std::string filename(int64_t milliseconds,
		const char *extension = NULL, const char *pattern = NULL)
	{
		hx_string text = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&text, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(hx_os_filename_format_string(milliseconds,
					    pattern, extension, &text, &error),
			error);
		return std::string(hx_string_c_str(&text), text.size);
	}
	/* Offset east of UTC in minutes; DST 0/1/-1 (unknown). */
	static std::pair<int, int> localOffset()
	{
		int minutes, dst;
		hx_os_error error;
		detail::CheckStatus(hx_os_wall_time_local_offset_minutes(
					    &minutes, &dst, &error),
			error);
		return std::make_pair(minutes, dst);
	}
};
} // namespace hx
#endif
