#ifndef HX_C_PLATFORM_CPP_LOCAL_DUMPS_HPP
#define HX_C_PLATFORM_CPP_LOCAL_DUMPS_HPP

#include <hx_os_local_dumps.h>
#include <hx/Values.hpp>

namespace hx {
struct LocalDumpsInfo {
	hx_os_local_dumps_info info;
	String folder;

	explicit LocalDumpsInfo(hx_os_context *context = NULL) noexcept
	    : info{}
	    , folder(context)
	{
	}
};

/**
 * \brief Explicit persistent Windows configuration, not an RAII resource.
 * \details No destructor removes settings. The C ownership, permission,
 * registry-view and conflict contracts apply unchanged.
 */
class LocalDumps {
public:
	static bool available() noexcept
	{
		return hx_os_local_dumps_available() != 0;
	}

	static hx_os_local_dumps_options options() noexcept
	{
		hx_os_local_dumps_options value;

		(void)hx_os_local_dumps_options_init(&value, sizeof(value));

		return value;
	}

	static Result<void> enable(const char *application = NULL,
		const hx_os_local_dumps_options *options = NULL) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_local_dumps_enable(application, options, &error);

		return detail::Status(status, error);
	}

	static Result<LocalDumpsInfo> query(const char *application = NULL,
		unsigned registryView = HX_OS_LOCAL_DUMPS_VIEW_DEFAULT,
		hx_os_context *context = NULL) noexcept
	{
		LocalDumpsInfo result(context);
		hx_os_error error = {};
		auto status = hx_os_local_dumps_query(application, registryView,
			&result.info, sizeof(result.info),
			result.folder.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<void> remove(const char *application = NULL,
		const hx_os_local_dumps_options *expected = NULL) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_local_dumps_remove(application, expected, &error);

		return detail::Status(status, error);
	}
};
} // namespace hx
#endif
