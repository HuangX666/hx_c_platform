#ifndef HX_C_PLATFORM_CPP_SETTINGS_HPP
#define HX_C_PLATFORM_CPP_SETTINGS_HPP
#include <hx_settings.h>
#include <hx/Config.hpp>
#include <initializer_list>
namespace hx {

class SettingValue {
public:
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
		std::swap(m_value_, other.m_value_);
		return *this;
	}
	~SettingValue() noexcept
	{
		hx_settings_value_release(m_value_);
	}
	static Result<SettingValue> null(hx_os_context *context = NULL) noexcept
	{
		hx_settings_value *value = NULL;
		hx_os_error error = {};
		auto status = hx_settings_value_null(context, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return SettingValue(value);
	}
	static Result<SettingValue> integer(
		int64_t input, hx_os_context *context = NULL) noexcept
	{
		hx_settings_value *value = NULL;
		hx_os_error error = {};
		auto status =
			hx_settings_value_i64(context, input, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return SettingValue(value);
	}
	static Result<SettingValue> real(
		double input, hx_os_context *context = NULL) noexcept
	{
		hx_settings_value *value = NULL;
		hx_os_error error = {};
		auto status = hx_settings_value_double(
			context, input, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return SettingValue(value);
	}
	static Result<SettingValue> boolean(
		bool input, hx_os_context *context = NULL) noexcept
	{
		hx_settings_value *value = NULL;
		hx_os_error error = {};
		auto status =
			hx_settings_value_bool(context, input, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return SettingValue(value);
	}
	static Result<SettingValue> string(
		StringView input, hx_os_context *context = NULL) noexcept
	{
		hx_settings_value *value = NULL;
		hx_os_error error = {};
		auto status = hx_settings_value_string(
			context, input.native(), &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return SettingValue(value);
	}
	static Result<SettingValue> pointer(
		void *input, hx_os_context *context = NULL) noexcept
	{
		hx_settings_value *value = NULL;
		hx_os_error error = {};
		auto status = hx_settings_value_pointer(
			context, input, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return SettingValue(value);
	}

	static Result<SettingValue> array(
		std::initializer_list<SettingValue> input,
		hx_os_context *context = NULL) noexcept
	{
		if (input.size() > 65536)
			return Failure(HX_OS_LIMIT);
		auto storage = Buffer::allocate(
			input.size() * sizeof(hx_settings_value *), context,
			alignof(hx_settings_value *));
		if (!storage)
			return Failure(storage.error());
		auto items = reinterpret_cast<const hx_settings_value **>(
			storage.value().data());
		size_t index = 0;
		typedef const hx_settings_value *Item;
		for (const auto &value : input)
			new (&items[index++]) Item(value.m_value_);
		hx_settings_value *result = NULL;
		hx_os_error error = {};
		auto status = hx_settings_value_array(
			context, items, input.size(), &result, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return SettingValue(result);
	}
	static Result<SettingValue>
	object(std::initializer_list<std::pair<StringView, SettingValue>> input,
		hx_os_context *context = NULL) noexcept
	{
		if (input.size() > 65536)
			return Failure(HX_OS_LIMIT);
		auto storage = Buffer::allocate(
			input.size() * sizeof(hx_settings_pair), context,
			alignof(hx_settings_pair));
		if (!storage)
			return Failure(storage.error());
		auto items = reinterpret_cast<hx_settings_pair *>(
			storage.value().data());
		size_t index = 0;
		for (const auto &pair : input)
			new (&items[index++]) hx_settings_pair{
				pair.first.native(), pair.second.m_value_};
		hx_settings_value *result = NULL;
		hx_os_error error = {};
		auto status = hx_settings_value_object(
			context, items, input.size(), &result, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
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
	Result<int64_t> asInt() const noexcept
	{
		int64_t value{};
		hx_os_error error = {};
		auto status =
			hx_settings_value_get_i64(m_value_, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return value;
	}
	Result<double> asDouble() const noexcept
	{
		double value{};
		hx_os_error error = {};
		auto status =
			hx_settings_value_get_double(m_value_, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return value;
	}
	Result<bool> asBool() const noexcept
	{
		int value{};
		hx_os_error error = {};
		auto status =
			hx_settings_value_get_bool(m_value_, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return value != 0;
	}
	Result<void *> asPointer() const noexcept
	{
		void *value{};
		hx_os_error error = {};
		auto status =
			hx_settings_value_get_pointer(m_value_, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return value;
	}

	Result<String> asString(hx_os_context *context = NULL) const noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_settings_value_get_string(
					m_value_, out, error);
			},
			context);
	}
	Result<SettingValue> at(size_t index) const noexcept
	{
		hx_settings_value *value = NULL;
		hx_os_error error = {};
		auto status =
			hx_settings_value_at(m_value_, index, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return SettingValue(value);
	}
	Result<SettingValue> member(StringView name) const noexcept
	{
		hx_settings_value *value = NULL;
		hx_os_error error = {};
		auto status = hx_settings_value_member(
			m_value_, name.native(), &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return SettingValue(value);
	}
	Result<String> key(
		size_t index, hx_os_context *context = NULL) const noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_settings_value_name_at(
					m_value_, index, out, error);
			},
			context);
	}

private:
	hx_settings_value *m_value_;
};
/* Mutable owner stays with the host through setters, observers and close.
 * Snapshots own C references. Observers return normally through the C ABI.
 */
class Settings {
public:
	explicit Settings(hx_settings *&owner) noexcept
	    : m_owner_(&owner)
	{
	}
	Result<void> create(hx_os_context *context = NULL,
		const hx_settings_options *options = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);
		hx_os_error error = {};
		auto status = hx_settings_create_with_options(
			context, options, m_owner_, &error);
		return detail::Status(status, error);
	}
	Result<void> setInt(const char *key, int64_t value) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_settings_set_i64(*m_owner_, key, value, &error);
		return detail::Status(status, error);
	}
	Result<void> setBool(const char *key, bool value) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_settings_set_bool(*m_owner_, key, value, &error);
		return detail::Status(status, error);
	}
	Result<void> setDouble(const char *key, double value) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_settings_set_double(*m_owner_, key, value, &error);
		return detail::Status(status, error);
	}

	Result<void> setString(StringView key, StringView value) noexcept
	{
		hx_os_error error = {};
		auto status = hx_settings_set_string_view(
			*m_owner_, key.native(), value.native(), &error);
		return detail::Status(status, error);
	}
	Result<void> setValue(
		StringView key, const SettingValue &value) noexcept
	{
		hx_os_error error = {};
		auto status = hx_settings_set_value(
			*m_owner_, key.native(), value.native(), &error);
		return detail::Status(status, error);
	}
	Result<void> setDefaultValue(
		StringView key, const SettingValue &value) noexcept
	{
		hx_os_error error = {};
		auto status = hx_settings_set_default_value(
			*m_owner_, key.native(), value.native(), &error);
		return detail::Status(status, error);
	}
	Result<SettingValue> get(StringView key) const noexcept
	{
		hx_settings_value *value = NULL;
		hx_os_error error = {};
		auto status = hx_settings_get_value(
			*m_owner_, key.native(), &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return SettingValue(value);
	}
	Result<String> string(
		StringView key, hx_os_context *context = NULL) const noexcept
	{
		auto value = get(key);
		if (!value)
			return Failure(value.error());
		return value.value().asString(context);
	}
	Result<int64_t> integer(StringView key) const noexcept
	{
		auto value = get(key);
		if (!value)
			return Failure(value.error());
		return value.value().asInt();
	}
	Result<bool> boolean(StringView key) const noexcept
	{
		auto value = get(key);
		if (!value)
			return Failure(value.error());
		return value.value().asBool();
	}
	Result<double> real(StringView key) const noexcept
	{
		auto value = get(key);
		if (!value)
			return Failure(value.error());
		return value.value().asDouble();
	}

	Result<void> unset(
		StringView key, int layer = HX_SETTINGS_EXPLICIT) noexcept
	{
		hx_os_error error = {};
		auto status = hx_settings_unset(
			*m_owner_, key.native(), layer, &error);
		return detail::Status(status, error);
	}
	Result<SettingValue> snapshot(
		int layer = HX_SETTINGS_EFFECTIVE) const noexcept
	{
		hx_settings_value *value = NULL;
		hx_os_error error = {};
		auto status =
			hx_settings_snapshot(*m_owner_, layer, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return SettingValue(value);
	}
	Result<String> json(hx_os_context *context = NULL) const noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_settings_to_json(
					*m_owner_, out, error);
			},
			context);
	}
	Result<void> save(const char *path, int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_settings_save(
			*m_owner_, format, path, options, &error);
		return detail::Status(status, error);
	}
	Result<void> load(const char *path, int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL,
		hx_os_config_error *diagnostic = NULL) noexcept
	{
		hx_os_config_error error = {};
		auto status = hx_settings_load(
			*m_owner_, format, path, options, &error);
		if (diagnostic)
			*diagnostic = error;
		return detail::Status(status, error.detail);
	}
	Result<void> closeUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_settings_close_until(m_owner_, deadline, &error);
		return detail::Status(status, error);
	}

private:
	hx_settings **m_owner_;
};
} // namespace hx
#endif
