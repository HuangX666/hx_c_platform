#ifndef HX_C_PLATFORM_CPP_SETTINGS_HPP
#define HX_C_PLATFORM_CPP_SETTINGS_HPP
#include <hx_settings.h>
#include <hx/Config.hpp>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>
namespace hx {
class SettingValue {
public:
	/* Adopts one C reference. Default construction is an empty handle. */
	explicit SettingValue(hx_settings_value *value = NULL) noexcept
	    : m_value_(value)
	{
	}
	SettingValue(const SettingValue &other) noexcept
	    : m_value_(other.m_value_)
	{
		hx_settings_value_retain(m_value_);
	}
	SettingValue &operator=(const SettingValue &other) noexcept
	{
		if (this != &other) {
			hx_settings_value_retain(other.m_value_);
			hx_settings_value_release(m_value_);
			m_value_ = other.m_value_;
		}
		return *this;
	}
	SettingValue(SettingValue &&other) noexcept
	    : m_value_(other.m_value_)
	{
		other.m_value_ = NULL;
	}
	SettingValue &operator=(SettingValue &&other) noexcept
	{
		if (this != &other) {
			hx_settings_value_release(m_value_);
			m_value_ = other.m_value_;
			other.m_value_ = NULL;
		}
		return *this;
	}
	~SettingValue()
	{
		hx_settings_value_release(m_value_);
	}
	static SettingValue null(hx_os_context *context = NULL)
	{
		hx_settings_value *value = NULL;
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_value_null(context, &value, &error), error);
		return SettingValue(value);
	}
	static SettingValue integer(
		int64_t input, hx_os_context *context = NULL)
	{
		hx_settings_value *value = NULL;
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_value_i64(context, input, &value, &error),
			error);
		return SettingValue(value);
	}
	static SettingValue real(double input, hx_os_context *context = NULL)
	{
		hx_settings_value *value = NULL;
		hx_os_error error;
		detail::CheckStatus(hx_settings_value_double(
					    context, input, &value, &error),
			error);
		return SettingValue(value);
	}
	static SettingValue boolean(bool input, hx_os_context *context = NULL)
	{
		hx_settings_value *value = NULL;
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_value_bool(context, input, &value, &error),
			error);
		return SettingValue(value);
	}
	static SettingValue string(
		const std::string &input, hx_os_context *context = NULL)
	{
		hx_settings_value *value = NULL;
		hx_os_error error;
		hx_string_view text = {input.data(), input.size()};
		detail::CheckStatus(
			hx_settings_value_string(context, text, &value, &error),
			error);
		return SettingValue(value);
	}
	static SettingValue pointer(void *input, hx_os_context *context = NULL)
	{
		hx_settings_value *value = NULL;
		hx_os_error error;
		detail::CheckStatus(hx_settings_value_pointer(
					    context, input, &value, &error),
			error);
		return SettingValue(value);
	}
	static SettingValue array(std::initializer_list<SettingValue> input,
		hx_os_context *context = NULL)
	{
		std::vector<const hx_settings_value *> items;
		items.reserve(input.size());
		for (const auto &value : input)
			items.push_back(value.m_value_);
		hx_settings_value *result = NULL;
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_value_array(context, items.data(),
				items.size(), &result, &error),
			error);
		return SettingValue(result);
	}
	static SettingValue object(
		std::initializer_list<std::pair<std::string, SettingValue>>
			input,
		hx_os_context *context = NULL)
	{
		std::vector<hx_settings_pair> items;
		items.reserve(input.size());
		for (const auto &pair : input) {
			hx_settings_pair item = {
				{pair.first.data(), pair.first.size()},
				pair.second.m_value_};
			items.push_back(item);
		}
		hx_settings_value *result = NULL;
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_value_object(context, items.data(),
				items.size(), &result, &error),
			error);
		return SettingValue(result);
	}
	int kind() const noexcept
	{
		return hx_settings_value_kind(m_value_);
	}
	size_t size() const noexcept
	{
		return hx_settings_value_count(m_value_);
	}
	const hx_settings_value *native() const noexcept
	{
		return m_value_;
	}
	int64_t asInt() const
	{
		int64_t value;
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_value_get_i64(m_value_, &value, &error),
			error);
		return value;
	}
	double asDouble() const
	{
		double value;
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_value_get_double(m_value_, &value, &error),
			error);
		return value;
	}
	bool asBool() const
	{
		int value;
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_value_get_bool(m_value_, &value, &error),
			error);
		return value != 0;
	}
	void *asPointer() const
	{
		void *value;
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_value_get_pointer(m_value_, &value, &error),
			error);
		return value;
	}
	std::string asString() const
	{
		hx_string text = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&text, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_value_get_string(m_value_, &text, &error),
			error);
		return std::string(hx_string_c_str(&text), text.size);
	}
	SettingValue at(size_t index) const
	{
		hx_settings_value *value = NULL;
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_value_at(m_value_, index, &value, &error),
			error);
		return SettingValue(value);
	}
	SettingValue member(const std::string &name) const
	{
		hx_settings_value *value = NULL;
		hx_os_error error;
		hx_string_view key = {name.data(), name.size()};
		detail::CheckStatus(
			hx_settings_value_member(m_value_, key, &value, &error),
			error);
		return SettingValue(value);
	}
	std::string key(size_t index) const
	{
		hx_string text = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&text, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(hx_settings_value_name_at(
					    m_value_, index, &text, &error),
			error);
		return std::string(hx_string_c_str(&text), text.size);
	}

private:
	hx_settings_value *m_value_;
};
/* Mutable owners keep explicit closeUntil because another host thread may
 * still be in a setter, observer or I/O. Destruction of this facade leaves
 * the host-owned handle reachable. Snapshots use SettingValue RAII.
 * Optional C observers in options must return normally without exceptions.
 */
class Settings {
public:
	explicit Settings(hx_settings *&owner) noexcept
	    : m_owner_(&owner)
	{
	}
	void create(hx_os_context *context = NULL,
		const hx_settings_options *options = NULL)
	{
		if (*m_owner_)
			throw StatusError(HX_OS_BUSY);
		hx_os_error error;
		detail::CheckStatus(hx_settings_create_with_options(
					    context, options, m_owner_, &error),
			error);
	}
	Settings &setInt(const char *key, int64_t value)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_set_i64(*m_owner_, key, value, &error),
			error);
		return *this;
	}
	Settings &setBool(const char *key, bool value)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_set_bool(*m_owner_, key, value, &error),
			error);
		return *this;
	}
	Settings &setDouble(const char *key, double value)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_set_double(*m_owner_, key, value, &error),
			error);
		return *this;
	}
	Settings &setString(const std::string &key, const std::string &value)
	{
		hx_string_view name = {key.data(), key.size()};
		hx_string_view text = {value.data(), value.size()};
		hx_os_error error;
		detail::CheckStatus(hx_settings_set_string_view(
					    *m_owner_, name, text, &error),
			error);
		return *this;
	}
	Settings &setValue(const std::string &key, const SettingValue &value)
	{
		hx_string_view name = {key.data(), key.size()};
		hx_os_error error;
		detail::CheckStatus(hx_settings_set_value(*m_owner_, name,
					    value.native(), &error),
			error);
		return *this;
	}
	Settings &setDefaultValue(
		const std::string &key, const SettingValue &value)
	{
		hx_string_view name = {key.data(), key.size()};
		hx_os_error error;
		detail::CheckStatus(hx_settings_set_default_value(*m_owner_,
					    name, value.native(), &error),
			error);
		return *this;
	}
	SettingValue get(const std::string &key) const
	{
		hx_string_view name = {key.data(), key.size()};
		hx_settings_value *value = NULL;
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_get_value(*m_owner_, name, &value, &error),
			error);
		return SettingValue(value);
	}
	std::string string(const std::string &key) const
	{
		return get(key).asString();
	}
	int64_t integer(const std::string &key) const
	{
		return get(key).asInt();
	}
	bool boolean(const std::string &key) const
	{
		return get(key).asBool();
	}
	double real(const std::string &key) const
	{
		return get(key).asDouble();
	}
	void unset(const std::string &key, int layer = HX_SETTINGS_EXPLICIT)
	{
		hx_string_view name = {key.data(), key.size()};
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_unset(*m_owner_, name, layer, &error),
			error);
	}
	SettingValue snapshot(int layer = HX_SETTINGS_EFFECTIVE) const
	{
		hx_settings_value *value = NULL;
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_snapshot(*m_owner_, layer, &value, &error),
			error);
		return SettingValue(value);
	}
	std::string json() const
	{
		hx_string text = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&text, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(
			hx_settings_to_json(*m_owner_, &text, &error), error);
		return std::string(hx_string_c_str(&text), text.size);
	}
	void save(const char *path, int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL) const
	{
		hx_os_error error;
		detail::CheckStatus(hx_settings_save(*m_owner_, format, path,
					    options, &error),
			error);
	}
	void load(const char *path, int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL)
	{
		hx_os_config_error error;
		if (hx_settings_load(*m_owner_, format, path, options,
			    &error) != HX_OS_OK)
			throw ConfigError(error);
	}
	hx_os_status closeUntil(
		hx_os_deadline deadline, hx_os_error *error = NULL) noexcept
	{
		return hx_settings_close_until(m_owner_, deadline, error);
	}

private:
	hx_settings **m_owner_;
};
} // namespace hx
#endif
