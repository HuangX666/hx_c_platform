#ifndef HX_C_PLATFORM_CPP_ARCHIVE_HPP
#define HX_C_PLATFORM_CPP_ARCHIVE_HPP

#include <hx_os_archive.h>
#include <hx/Callback.hpp>

namespace hx {

class Archive {
	template <class F> struct Visitor {
		F *function;

		static hx_os_status HX_OS_CALL call(
			const hx_os_archive_entry *entry, void *user,
			hx_os_error *error) noexcept
		{
			auto &self = *static_cast<Visitor *>(user);
			auto result =
				detail::InvokeCallback(*self.function, *entry);

			if (!result && error)
				*error = result.error();
			return result.status();
		}
	};

public:
	/**
	 * \brief A failed result still holds partial counters.
	 *
	 * \details value().published is the number of files already published,
	 * not a transaction guarantee.
	 */
	static Result<hx_os_archive_result> pack(const char *source,
		const char *archivePath,
		const hx_os_archive_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		hx_os_archive_result result = {};
		hx_os_error error = {};

		hx_os_archive_result_init(&result, sizeof(result));
		auto status = hx_os_archive_pack_ex(
			context, source, archivePath, options, &result, &error);
		return Result<hx_os_archive_result>::withValue(
			result, status, error);
	}

	static Result<hx_os_archive_result> unpack(const char *archivePath,
		const char *directory,
		const hx_os_archive_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		hx_os_archive_result result = {};
		hx_os_error error = {};

		hx_os_archive_result_init(&result, sizeof(result));
		auto status = hx_os_archive_unpack_ex(context, archivePath,
			directory, options, &result, &error);
		return Result<hx_os_archive_result>::withValue(
			result, status, error);
	}

	static Result<hx_os_archive_result> extractFile(const char *archivePath,
		const char *entry, const char *destination,
		const hx_os_archive_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		hx_os_archive_result result = {};
		hx_os_error error = {};

		hx_os_archive_result_init(&result, sizeof(result));
		auto status =
			hx_os_archive_extract_file_ex(context, archivePath,
				entry, destination, options, &result, &error);
		return Result<hx_os_archive_result>::withValue(
			result, status, error);
	}

	// The callback borrows each entry only during this synchronous call.
	// Return void, hx_os_status, or Result<void>; no exceptions.
	template <class F>
	static Result<hx_os_archive_result> forEach(const char *archivePath,
		F &&function, const hx_os_archive_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		typedef typename std::remove_reference<F>::type Function;
		if (detail::NullCallable(function, std::is_pointer<Function>()))
			return Failure(HX_OS_INVALID);
		Visitor<Function> visitor = {&function};
		hx_os_archive_result result = {};
		hx_os_error error = {};

		hx_os_archive_result_init(&result, sizeof(result));
		auto status = hx_os_archive_visit_ex(context, archivePath,
			options, Visitor<Function>::call, &visitor, &result,
			&error);
		return Result<hx_os_archive_result>::withValue(
			result, status, error);
	}
};

} // namespace hx
#endif
