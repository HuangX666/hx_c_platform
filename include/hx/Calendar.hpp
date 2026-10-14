#ifndef HX_C_PLATFORM_CPP_CALENDAR_HPP
#define HX_C_PLATFORM_CPP_CALENDAR_HPP

#include <hx_os_calendar.h>
#include <hx/Values.hpp>

namespace hx {
class Calendar {
public:
	static Result<String> utc(int64_t milliseconds, const char *spec = NULL,
		hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_wall_time_format_utc_string(
					milliseconds, spec, out, error);
			},
			context);
	}

	static Result<String> filename(int64_t milliseconds,
		const char *extension = NULL, const char *pattern = NULL,
		hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_filename_format_string(
					milliseconds, pattern, extension, out,
					error);
			},
			context);
	}

	static Result<String> local(int64_t milliseconds,
		const char *spec = NULL, hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_wall_time_format_local_string(
					milliseconds, spec, out, error);
			},
			context);
	}

	static Result<String> localFilename(int64_t milliseconds,
		const char *extension = NULL, const char *pattern = NULL,
		hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_filename_format_local_string(
					milliseconds, pattern, extension, out,
					error);
			},
			context);
	}

	static Result<String> filenameNow(const char *extension = NULL,
		hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_filename_generate(
					extension, out, error);
			},
			context);
	}

	static Result<String> localFilenameNow(const char *extension = NULL,
		hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_filename_generate_local(
					extension, out, error);
			},
			context);
	}

	/**
	 * \brief Offset east of UTC in seconds for this instant; DST 0/1/-1.
	 */
	static Result<std::pair<int, int>> localOffsetAt(
		int64_t milliseconds) noexcept
	{
		int seconds = 0, dst = 0;
		hx_os_error error = {};
		auto status = hx_os_wall_time_local_offset_at(
			milliseconds, &seconds, &dst, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return std::make_pair(seconds, dst);
	}

	/**
	 * \brief Offset east of UTC in minutes; DST 0/1/-1.
	 */
	static Result<std::pair<int, int>> localOffset() noexcept
	{
		int minutes = 0, dst = 0;
		hx_os_error error = {};

		auto status = hx_os_wall_time_local_offset_minutes(
			&minutes, &dst, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return std::make_pair(minutes, dst);
	}
};
} // namespace hx
#endif
