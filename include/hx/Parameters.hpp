#ifndef HX_C_PLATFORM_CPP_PARAMETERS_HPP
#define HX_C_PLATFORM_CPP_PARAMETERS_HPP

#include <hx_os_params.h>
#include <hx/Values.hpp>

namespace hx {

class ParametersEdit {
public:
	explicit ParametersEdit(hx_os_params *data) noexcept
	    : m_edit_(hx_os_params_edit_begin(data))
	{
	}

	ParametersEdit &setBool(const char *name, bool value) noexcept
	{
		hx_os_params_edit_set_bool(&m_edit_, name, value ? 1 : 0);

		return *this;
	}

	ParametersEdit &setInt(const char *name, int64_t value) noexcept
	{
		hx_os_params_edit_set_i64(&m_edit_, name, value);

		return *this;
	}

	ParametersEdit &setUnsigned(const char *name, uint64_t value) noexcept
	{
		hx_os_params_edit_set_u64(&m_edit_, name, value);

		return *this;
	}

	ParametersEdit &setDouble(const char *name, double value) noexcept
	{
		hx_os_params_edit_set_double(&m_edit_, name, value);

		return *this;
	}

	ParametersEdit &setPointer(const char *name, void *value) noexcept
	{
		hx_os_params_edit_set_pointer(&m_edit_, name, value);

		return *this;
	}

	ParametersEdit &setString(const char *name, StringView value) noexcept
	{
		hx_os_params_edit_set_string(&m_edit_, name, value.native());

		return *this;
	}

	ParametersEdit &setText(const char *name, const char *value) noexcept
	{
		hx_os_params_edit_set_cstr(&m_edit_, name, value);

		return *this;
	}

	ParametersEdit &setBytes(
		const char *name, const void *data, size_t size) noexcept
	{
		hx_os_params_edit_set_bytes(&m_edit_, name, data, size);

		return *this;
	}

	Result<void> finish() const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_params_edit_finish(&m_edit_, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_params_edit m_edit_;
};

/**
 * \brief Borrowed view: the pack outlives this object and any returned byte
 * views.
 */
class ParametersView {
public:
	explicit ParametersView(const hx_os_params *data = NULL) noexcept
	    : m_data_(data)
	{
	}

	size_t size() const noexcept
	{
		return hx_os_params_count(m_data_);
	}

	Result<int64_t> integer(const char *name) const noexcept
	{
		int64_t value{};
		hx_os_error error = {};
		auto status =
			hx_os_params_get_i64(m_data_, name, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<uint64_t> unsignedInteger(const char *name) const noexcept
	{
		uint64_t value{};
		hx_os_error error = {};
		auto status =
			hx_os_params_get_u64(m_data_, name, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<double> real(const char *name) const noexcept
	{
		double value{};
		hx_os_error error = {};
		auto status =
			hx_os_params_get_double(m_data_, name, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<void *> pointer(const char *name) const noexcept
	{
		void *value{};
		hx_os_error error = {};
		auto status =
			hx_os_params_get_pointer(m_data_, name, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<StringView> stringView(const char *name) const noexcept
	{
		hx_string_view value{};
		hx_os_error error = {};
		auto status =
			hx_os_params_get_string(m_data_, name, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return StringView(value);
	}

	Result<hx_os_bytes_view> bytesView(const char *name) const noexcept
	{
		hx_os_bytes_view value{};
		hx_os_error error = {};
		auto status =
			hx_os_params_get_bytes(m_data_, name, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<bool> boolean(const char *name) const noexcept
	{
		int value{};
		hx_os_error error = {};
		auto status =
			hx_os_params_get_bool(m_data_, name, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value != 0;
	}

	Result<String> string(
		const char *name, hx_os_context *context = NULL) const noexcept
	{
		auto value = stringView(name);
		if (!value)
			return Failure(value.error());

		return String::copy(value.value(), context);
	}

	const hx_os_params *native() const noexcept
	{
		return m_data_;
	}

protected:
	const hx_os_params *m_data_;
};

/**
 * \brief Mutable borrowed reference, including synchronous callback parameters.
 */
class ParametersRef : public ParametersView {
public:
	explicit ParametersRef(hx_os_params *data) noexcept
	    : ParametersView(data)
	{
	}

	Result<void> setInt(const char *name, int64_t value) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_params_set_i64(native(), name, value, &error);

		return detail::Status(status, error);
	}

	Result<void> setUnsigned(const char *name, uint64_t value) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_params_set_u64(native(), name, value, &error);

		return detail::Status(status, error);
	}

	Result<void> setDouble(const char *name, double value) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_params_set_double(native(), name, value, &error);

		return detail::Status(status, error);
	}

	Result<void> setPointer(const char *name, void *value) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_params_set_pointer(native(), name, value, &error);

		return detail::Status(status, error);
	}

	Result<void> setString(const char *name, StringView value) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_params_set_string(
			native(), name, value.native(), &error);

		return detail::Status(status, error);
	}

	Result<void> setBytes(
		const char *name, const void *data, size_t bytes) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_params_set_bytes(
			native(), name, data, bytes, &error);

		return detail::Status(status, error);
	}

	Result<void> setBool(const char *name, bool value) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_params_set_bool(
			native(), name, value ? 1 : 0, &error);

		return detail::Status(status, error);
	}

	ParametersEdit edit() & noexcept
	{
		return ParametersEdit(native());
	}

	Result<void> remove(const char *name) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_params_remove(native(), name, &error);

		return detail::Status(status, error);
	}

	Result<void> clear() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_params_clear(native(), &error);

		return detail::Status(status, error);
	}

	hx_os_params *native() noexcept
	{
		return const_cast<hx_os_params *>(m_data_);
	}

	using ParametersView::native;
};

/**
 * \brief Synchronous move-only owner.
 *
 * \details Exclude allocator reentry into this facade, and finish using
 * borrowed views before mutation, moving or destruction.
 */
class Parameters : public ParametersRef {
public:
	Parameters() noexcept
	    : ParametersRef(NULL)
	{
	}

	Parameters(const Parameters &) = delete;
	Parameters &operator=(const Parameters &) = delete;

	Parameters(Parameters &&other) noexcept
	    : ParametersRef(other.native())
	{
		other.m_data_ = NULL;
	}

	Parameters &operator=(Parameters &&other) noexcept
	{
		if (this != &other) {
			auto *owner = native();

			(void)hx_os_params_destroy(&owner, NULL);
			m_data_ = other.m_data_;
			other.m_data_ = NULL;
		}

		return *this;
	}

	~Parameters() noexcept
	{
		auto *owner = native();

		(void)hx_os_params_destroy(&owner, NULL);
	}

	static Result<Parameters> create(hx_os_context *context = NULL,
		const hx_os_params_options *options = NULL) noexcept
	{
		Parameters result;
		hx_os_params *owner = NULL;
		hx_os_error error = {};
		auto status = hx_os_params_create_with_options(
			context, options, &owner, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		result.m_data_ = owner;

		return result;
	}

	Result<Parameters> clone(hx_os_context *context = NULL) const noexcept
	{
		Parameters result;
		hx_os_params *owner = NULL;
		hx_os_error error = {};
		auto status =
			hx_os_params_clone(native(), context, &owner, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		result.m_data_ = owner;

		return result;
	}

private:
	friend class SignalArguments;
	friend class ProcedureHandler;
};
} // namespace hx
#endif
