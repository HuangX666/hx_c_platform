#ifndef HX_C_PLATFORM_CPP_CONFIG_HPP
#define HX_C_PLATFORM_CPP_CONFIG_HPP

#include <hx_config.h>
#include <hx/Status.hpp>
#include <memory>
#include <string>
#include <utility>

namespace hx {
class ConfigError : public StatusError {
public:
	explicit ConfigError(const hx_os_config_error &error)
	    : StatusError(error.detail)
	    , m_diagnostic_(error)
	{
	}
	const char *what() const noexcept override
	{
		return m_diagnostic_.message[0] ? m_diagnostic_.message
						: StatusError::what();
	}
	const hx_os_config_error &diagnostic() const noexcept
	{
		return m_diagnostic_;
	}

private:
	hx_os_config_error m_diagnostic_;
};

/* Immutable shared document. Returned C nodes borrow nativeHandle(). */
class ConfigDocument {
public:
	static ConfigDocument parse(const std::string &text,
		int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL,
		hx_os_context *context = NULL)
	{
		hx_os_config_document *document = NULL;
		hx_os_config_error error;
		if (hx_os_config_parse(context, format, text.data(),
			    text.size(), options, &document,
			    &error) != HX_OS_OK)
			throw ConfigError(error);
		return ConfigDocument(document);
	}
	static ConfigDocument load(const char *path,
		int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL,
		hx_os_context *context = NULL)
	{
		hx_os_config_document *document = NULL;
		hx_os_config_error error;
		if (hx_os_config_load(context, format, path, options, &document,
			    &error) != HX_OS_OK)
			throw ConfigError(error);
		return ConfigDocument(document);
	}
	ConfigDocument(const ConfigDocument &other) noexcept
	    : m_document_(other.m_document_)
	{
		if (m_document_)
			hx_os_config_retain(m_document_);
	}
	ConfigDocument &operator=(const ConfigDocument &other) noexcept
	{
		if (this != &other) {
			if (other.m_document_)
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
		if (this != &other) {
			hx_os_config_release(m_document_);
			m_document_ = other.m_document_;
			other.m_document_ = NULL;
		}
		return *this;
	}
	~ConfigDocument()
	{
		hx_os_config_release(m_document_);
	}
	std::string serialize(int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL) const
	{
		hx_string text = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&text, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(hx_os_config_serialize(m_document_, format,
					    &text, options, &error),
			error);
		return std::string(hx_string_c_str(&text), text.size);
	}
	void save(const char *path, int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL) const
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_config_save(m_document_, format, path,
					    options, &error),
			error);
	}
	std::string string(const char *pointer) const
	{
		hx_os_config_slice value;
		hx_os_status status =
			hx_os_config_get_string(node(pointer), &value);
		if (status != HX_OS_OK)
			throw StatusError(status);
		return std::string(value.data, value.bytes);
	}
	std::string stringOr(
		const char *pointer, const std::string &fallback) const
	{
		if (!m_document_)
			throw StatusError(HX_OS_CLOSED);
		const hx_os_config_node *value = NULL;
		hx_os_status status = hx_os_config_pointer(
			hx_os_config_root(m_document_), pointer, &value);
		if (status == HX_OS_NOT_FOUND)
			return fallback;
		if (status != HX_OS_OK)
			throw StatusError(status);
		hx_os_config_slice text;
		status = hx_os_config_get_string(value, &text);
		if (status != HX_OS_OK)
			throw StatusError(status);
		return std::string(text.data, text.bytes);
	}
	int64_t integer(const char *pointer) const
	{
		int64_t value;
		hx_os_status status =
			hx_os_config_get_i64(node(pointer), &value);
		if (status != HX_OS_OK)
			throw StatusError(status);
		return value;
	}
	double number(const char *pointer) const
	{
		double value;
		hx_os_status status =
			hx_os_config_get_double(node(pointer), &value);
		if (status != HX_OS_OK)
			throw StatusError(status);
		return value;
	}
	const hx_os_config_node *node(const char *pointer = "") const
	{
		if (!m_document_)
			throw StatusError(HX_OS_CLOSED);
		const hx_os_config_node *value = NULL;
		hx_os_status status = hx_os_config_pointer(
			hx_os_config_root(m_document_), pointer, &value);
		if (status != HX_OS_OK)
			throw StatusError(status);
		return value;
	}
	hx_os_config_document *nativeHandle() const noexcept
	{
		return m_document_;
	}

private:
	hx_os_config_document *m_document_;
	explicit ConfigDocument(hx_os_config_document *document) noexcept
	    : m_document_(document)
	{
	}
};
} // namespace hx
#endif
