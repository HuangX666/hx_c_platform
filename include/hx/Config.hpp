#ifndef HX_C_PLATFORM_CPP_CONFIG_HPP
#define HX_C_PLATFORM_CPP_CONFIG_HPP

#include <hx_config.h>
#include <hx_config_file.h>
#include <hx/Values.hpp>

namespace hx {

/**
 * \brief Immutable shared document.
 *
 * \details Nodes borrow it; diagnostics are caller-owned.
 */
class ConfigDocument {
public:
	ConfigDocument() noexcept
	    : m_document_(NULL)
	{
	}

	static Result<ConfigDocument> parse(StringView text,
		int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL,
		hx_os_context *context = NULL,
		hx_os_config_error *diagnostic = NULL) noexcept
	{
		ConfigDocument result;
		hx_os_config_error error = {};

		auto status = hx_os_config_parse(context, format, text.data(),
			text.size(), options, &result.m_document_, &error);
		if (diagnostic)
			*diagnostic = error;

		if (status != HX_OS_OK)
			return Failure(status, error.detail);

		return result;
	}

	static Result<ConfigDocument> load(const char *path,
		int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL,
		hx_os_context *context = NULL,
		hx_os_config_error *diagnostic = NULL) noexcept
	{
		ConfigDocument result;
		hx_os_config_error error = {};

		auto status = hx_os_config_load(context, format, path, options,
			&result.m_document_, &error);
		if (diagnostic)
			*diagnostic = error;

		if (status != HX_OS_OK)
			return Failure(status, error.detail);

		return result;
	}

	static Result<ConfigDocument> create(const hx_os_config_value &root,
		const hx_os_config_options *options = NULL,
		hx_os_context *context = NULL,
		hx_os_config_error *diagnostic = NULL) noexcept
	{
		ConfigDocument result;
		hx_os_config_error error = {};

		auto status = hx_os_config_create(
			context, &root, options, &result.m_document_, &error);
		if (diagnostic)
			*diagnostic = error;

		if (status != HX_OS_OK)
			return Failure(status, error.detail);

		return result;
	}

	Result<ConfigDocument> withValue(const char *pointer,
		const hx_os_config_value &value,
		const hx_os_config_options *options = NULL,
		hx_os_config_error *diagnostic = NULL) const noexcept
	{
		auto found = node(pointer);
		if (!found)
			return Failure(found.error());

		ConfigDocument result;
		hx_os_config_error error = {};

		auto status = hx_os_config_replace(m_document_, found.value(),
			&value, options, &result.m_document_, &error);
		if (diagnostic)
			*diagnostic = error;

		if (status != HX_OS_OK)
			return Failure(status, error.detail);

		return result;
	}

	static Result<String> convert(StringView text, int from, int to,
		const hx_os_config_options *options = NULL,
		hx_os_context *context = NULL,
		hx_os_config_error *diagnostic = NULL) noexcept
	{
		String output(context);

		hx_os_config_error error = {};

		auto status = hx_os_config_convert(context, from, text.data(),
			text.size(), to, output.native(), options, &error);
		if (diagnostic)
			*diagnostic = error;

		if (status != HX_OS_OK)
			return Failure(status, error.detail);

		return output;
	}

	ConfigDocument(const ConfigDocument &other) noexcept
	    : m_document_(other.m_document_)
	{
		hx_os_config_retain(m_document_);
	}

	ConfigDocument &operator=(const ConfigDocument &other) noexcept
	{
		if (this != &other) {
			hx_os_config_retain(other.m_document_);

			hx_os_config_release(m_document_);
			m_document_ = other.m_document_;
		}

		return *this;
	}

	ConfigDocument(ConfigDocument &&other) noexcept
	    : m_document_(other.m_document_)
	{
		other.m_document_ = NULL;
	}

	ConfigDocument &operator=(ConfigDocument &&other) noexcept
	{
		std::swap(m_document_, other.m_document_);

		return *this;
	}

	~ConfigDocument() noexcept
	{
		hx_os_config_release(m_document_);
	}

	Result<String> serialize(int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL,
		hx_os_context *context = NULL) const noexcept
	{
		if (!m_document_)
			return Failure(HX_OS_CLOSED);

		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_config_serialize(m_document_,
					format, out, options, error);
			},
			context);
	}

	Result<void> save(const char *path, int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL) const noexcept
	{
		if (!m_document_)
			return Failure(HX_OS_CLOSED);

		hx_os_error error = {};

		auto status = hx_os_config_save(
			m_document_, format, path, options, &error);

		return detail::Status(status, error);
	}

	Result<hx_os_config_backup_result> saveBackup(const char *path,
		int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL,
		hx_os_config_error *diagnostic = NULL) const noexcept
	{
		if (!m_document_)
			return Failure(HX_OS_CLOSED);

		hx_os_config_backup_result result = {};
		hx_os_config_error error = {};
		auto status = hx_os_config_save_backup(
			m_document_, format, path, options, &result, &error);
		if (diagnostic)
			*diagnostic = error;

		return Result<hx_os_config_backup_result>::withValue(
			result, status, error.detail);
	}

	static Result<ConfigDocument> restoreBackup(const char *path,
		int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL,
		hx_os_context *context = NULL,
		hx_os_config_error *diagnostic = NULL,
		hx_os_file_save_result *publication = NULL) noexcept
	{
		ConfigDocument result;
		hx_os_config_error error = {};
		auto status = hx_os_config_restore_backup(context, format, path,
			options, &result.m_document_, publication, &error);
		if (diagnostic)
			*diagnostic = error;
		if (status != HX_OS_OK)
			return Failure(status, error.detail);

		return result;
	}

	static Result<ConfigDocument> emptyIni(
		hx_os_context *context = NULL) noexcept
	{
		return parse("", HX_OS_CONFIG_INI, NULL, context);
	}

	Result<void> setIni(const char *section, const char *key,
		const char *value,
		hx_os_config_error *diagnostic = NULL) noexcept
	{
		if (!m_document_)
			return Failure(HX_OS_CLOSED);

		hx_os_config_error error = {};
		auto status = hx_os_ini_set(
			&m_document_, section, key, value, &error);
		if (diagnostic)
			*diagnostic = error;

		return detail::Status(status, error.detail);
	}

	Result<String> ini(const char *section, const char *key,
		hx_os_context *context = NULL) const noexcept
	{
		if (!m_document_)
			return Failure(HX_OS_CLOSED);

		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_ini_get(
					m_document_, section, key, out, error);
			},
			context);
	}

	Result<void> removeIniKey(const char *section, const char *key) noexcept
	{
		if (!m_document_)
			return Failure(HX_OS_CLOSED);

		hx_os_config_error error = {};
		auto status = hx_os_ini_remove_key(
			&m_document_, section, key, &error);

		return detail::Status(status, error.detail);
	}

	Result<void> removeIniSection(const char *section) noexcept
	{
		if (!m_document_)
			return Failure(HX_OS_CLOSED);

		hx_os_config_error error = {};
		auto status =
			hx_os_ini_remove_section(&m_document_, section, &error);

		return detail::Status(status, error.detail);
	}

	Result<const hx_os_config_node *> node(
		const char *pointer = "") const noexcept
	{
		if (!m_document_)
			return Failure(HX_OS_CLOSED);

		const hx_os_config_node *value = NULL;

		auto status = hx_os_config_pointer(
			hx_os_config_root(m_document_), pointer, &value);
		if (status != HX_OS_OK)
			return Failure(status);

		return value;
	}

	Result<String> string(const char *pointer,
		hx_os_context *context = NULL) const noexcept
	{
		auto found = stringView(pointer);
		if (!found)
			return Failure(found.error());

		return String::copy(found.value(), context);
	}

	/**
	 * \brief The immutable document owns the borrowed bytes.
	 */
	Result<StringView> stringView(const char *pointer) const & noexcept
	{
		auto found = node(pointer);
		if (!found)
			return Failure(found.error());

		hx_os_config_slice value = {};

		auto status = hx_os_config_get_string(found.value(), &value);
		if (status != HX_OS_OK)
			return Failure(status);

		return StringView(value.data, value.bytes);
	}

	Result<StringView> stringView(const char *) const && = delete;

	Result<String> stringOr(const char *pointer, StringView fallback,
		hx_os_context *context = NULL) const noexcept
	{
		auto result = string(pointer, context);
		if (result.status() == HX_OS_NOT_FOUND)
			return String::copy(fallback, context);

		return result;
	}

	Result<int64_t> integer(const char *pointer) const noexcept
	{
		auto found = node(pointer);
		if (!found)
			return Failure(found.error());

		int64_t value = 0;

		auto status = hx_os_config_get_i64(found.value(), &value);
		if (status != HX_OS_OK)
			return Failure(status);

		return value;
	}

	Result<double> number(const char *pointer) const noexcept
	{
		auto found = node(pointer);
		if (!found)
			return Failure(found.error());

		double value = 0;

		auto status = hx_os_config_get_double(found.value(), &value);
		if (status != HX_OS_OK)
			return Failure(status);

		return value;
	}

	hx_os_config_document *nativeHandle() const noexcept
	{
		return m_document_;
	}

private:
	hx_os_config_document *m_document_;
};
} // namespace hx
#endif
