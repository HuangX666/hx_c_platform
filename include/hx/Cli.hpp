#ifndef HX_C_PLATFORM_CPP_CLI_HPP
#define HX_C_PLATFORM_CPP_CLI_HPP

#include <hx_os_cli.h>
#include <hx/Values.hpp>

namespace hx {
class Cli {
public:
	Cli() noexcept
	    : m_cli_(NULL)
	{
	}

	Cli(Cli &&other) noexcept
	    : m_cli_(other.m_cli_)
	{
		other.m_cli_ = NULL;
	}

	Cli &operator=(Cli &&other) noexcept
	{
		std::swap(m_cli_, other.m_cli_);
		return *this;
	}

	Cli(const Cli &) = delete;
	Cli &operator=(const Cli &) = delete;

	~Cli() noexcept
	{
		hx_os_cli_destroy(&m_cli_);
	}

	static Result<Cli> parse(int argc, const char *const *argv,
		const hx_os_cli_option *options, size_t count,
		hx_os_context *context = NULL) noexcept
	{
		Cli result;
		hx_os_error error = {};
		auto status = hx_os_cli_parse_process(context, argc, argv,
			options, count, &result.m_cli_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	bool has(const char *name) const noexcept
	{
		return hx_os_cli_count(m_cli_, name) != 0;
	}

	size_t count(const char *name = NULL) const noexcept
	{
		return hx_os_cli_count(m_cli_, name);
	}

	Result<String> value(
		const char *name, hx_os_context *context = NULL) const noexcept
	{
		String result(context);
		hx_os_error error = {};
		auto status =
			hx_os_cli_value(m_cli_, name, result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	Result<int64_t> integer(const char *name) const noexcept
	{
		int64_t result = 0;
		hx_os_error error = {};
		auto status = hx_os_cli_i64(m_cli_, name, &result, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	Result<hx_string_view> at(const char *name, size_t index) const noexcept
	{
		hx_string_view result = {};
		hx_os_error error = {};
		auto status = hx_os_cli_value_at(
			m_cli_, name, index, &result, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

private:
	hx_os_cli *m_cli_;
};
} // namespace hx
#endif
