#ifndef HX_C_PLATFORM_CPP_PREPROCESSOR_HPP
#define HX_C_PLATFORM_CPP_PREPROCESSOR_HPP

#include <hx_os_preprocess.h>
#include <hx/Values.hpp>

namespace hx {

class PreprocessDiagnostic {
public:
	explicit PreprocessDiagnostic(hx_os_context *context = NULL) noexcept
	{
		hx_os_preprocess_diagnostic_init(&m_value_, context);
	}

	PreprocessDiagnostic(const PreprocessDiagnostic &) = delete;
	PreprocessDiagnostic &operator=(const PreprocessDiagnostic &) = delete;

	~PreprocessDiagnostic() noexcept
	{
		hx_os_preprocess_diagnostic_destroy(&m_value_);
	}

	StringView source() const noexcept
	{
		return StringView(hx_string_view_of(&m_value_.source));
	}

	const char *sourceName() const noexcept
	{
		return hx_string_c_str(&m_value_.source);
	}

	const char *description() const noexcept
	{
		return m_value_.description ? m_value_.description : "";
	}

	size_t line() const noexcept
	{
		return m_value_.line;
	}

	size_t column() const noexcept
	{
		return m_value_.column;
	}

	hx_os_preprocess_diagnostic *native() noexcept
	{
		return &m_value_;
	}

private:
	hx_os_preprocess_diagnostic m_value_;
};

class Preprocessor {
public:
	static Result<String> text(StringView input,
		const hx_os_preprocess_options *options = NULL,
		PreprocessDiagnostic *diagnostic = NULL,
		hx_os_context *context = NULL,
		const char *sourceName = NULL) noexcept
	{
		return detail::StringQuery(
			[input, options, diagnostic, sourceName](
				hx_string *out, hx_os_error *error) noexcept {
				return hx_os_preprocess_text_ex(input.native(),
					sourceName, options, out,
					diagnostic ? diagnostic->native()
						   : NULL,
					error);
			},
			context);
	}

	static Result<String> file(const char *path,
		const hx_os_preprocess_options *options = NULL,
		PreprocessDiagnostic *diagnostic = NULL,
		hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			[path, options, diagnostic](
				hx_string *out, hx_os_error *error) noexcept {
				return hx_os_preprocess_file_ex(path, options,
					out,
					diagnostic ? diagnostic->native()
						   : NULL,
					error);
			},
			context);
	}
};
} // namespace hx
#endif
