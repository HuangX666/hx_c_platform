#ifndef HX_C_PLATFORM_CPP_CODEC_HPP
#define HX_C_PLATFORM_CPP_CODEC_HPP

#include <hx_codec_macros.h>
#include <hx/Values.hpp>

namespace hx {

/* An actual C++ value plus the owner of its borrowed strings, arrays and nodes.
 * Members follow hx_os_codec_schema's C-compatible value-layout contract.
 */
template <typename T> class Decoded {
	static_assert(std::is_standard_layout<T>::value &&
			std::is_trivially_copyable<T>::value &&
			std::is_nothrow_default_constructible<T>::value,
		"Decode a C-compatible value type; adapt owning C++ objects "
		"explicitly");

public:
	explicit Decoded(hx_os_codec_result *owner) noexcept
	    : m_value_()
	    , m_owner_(owner)
	{
		std::memcpy(&m_value_, hx_os_codec_data(owner), sizeof(T));
	}

	Decoded(Decoded &&other) noexcept
	    : m_value_()
	    , m_owner_(other.m_owner_)
	{
		std::memcpy(&m_value_, &other.m_value_, sizeof(T));
		other.m_owner_ = NULL;
	}

	Decoded &operator=(Decoded &&other) noexcept
	{
		if (this != &other) {
			hx_os_codec_release(m_owner_);
			m_owner_ = other.m_owner_;
			std::memcpy(&m_value_, &other.m_value_, sizeof(T));
			other.m_owner_ = NULL;
		}

		return *this;
	}

	Decoded(const Decoded &) = delete;

	Decoded &operator=(const Decoded &) = delete;

	~Decoded() noexcept
	{
		hx_os_codec_release(m_owner_);
	}

	const T &value() const & noexcept
	{
		assert(m_owner_);

		return m_value_;
	}

	const T &value() const && = delete;

	const T *operator->() const noexcept
	{
		assert(m_owner_);

		return &m_value_;
	}

	/* U must match the field's declared element type. Copies one element
	 * into a live C++ object; its views still borrow this owner. */
	template <typename U>
	Result<U> arrayItem(
		hx_os_codec_array array, size_t index) const noexcept
	{
		static_assert(std::is_standard_layout<U>::value &&
				std::is_trivially_copyable<U>::value &&
				std::is_nothrow_default_constructible<U>::value,
			"Array element must be a C-compatible value type");
		if (!m_owner_ || index >= array.count || !array.data ||
			index > SIZE_MAX / sizeof(U))
			return Failure(HX_OS_RANGE);
		U item = U();
		std::memcpy(&item,
			static_cast<const unsigned char *>(array.data) +
				index * sizeof(U),
			sizeof(U));

		return item;
	}

private:
	T m_value_;
	hx_os_codec_result *m_owner_;
};

template <typename T> class Codec {
	static_assert(std::is_standard_layout<T>::value &&
			std::is_trivially_copyable<T>::value,
		"Codec maps C-compatible value layouts, not owning STL "
		"members");

public:
	explicit Codec(const hx_os_codec_schema &schema) noexcept
	    : m_schema_(&schema)
	{
	}

	Result<String> encode(const T &value, int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL,
		hx_os_context *context = NULL,
		hx_os_config_error *diagnostic = NULL) const noexcept
	{
		if (m_schema_->size != sizeof(T))
			return Failure(HX_OS_INVALID);

		String text(context);

		hx_os_config_error error = {};

		auto status = hx_os_codec_encode(m_schema_, &value, format,
			text.native(), options, &error);
		if (diagnostic)
			*diagnostic = error;

		if (status != HX_OS_OK)
			return Failure(status, error.detail);

		return text;
	}

	Result<Decoded<T>> decode(StringView text,
		int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL,
		hx_os_context *context = NULL,
		hx_os_config_error *diagnostic = NULL) const noexcept
	{
		if (m_schema_->size != sizeof(T))
			return Failure(HX_OS_INVALID);

		hx_os_codec_result *owner = NULL;
		hx_os_config_error error = {};

		auto status = hx_os_codec_decode(context, m_schema_, format,
			text.data(), text.size(), options, &owner, &error);
		if (diagnostic)
			*diagnostic = error;

		if (status != HX_OS_OK)
			return Failure(status, error.detail);

		return Decoded<T>(owner);
	}

	Result<Decoded<T>> load(const char *path,
		int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL,
		hx_os_context *context = NULL,
		hx_os_config_error *diagnostic = NULL) const noexcept
	{
		if (m_schema_->size != sizeof(T))
			return Failure(HX_OS_INVALID);

		hx_os_codec_result *owner = NULL;
		hx_os_config_error error = {};

		auto status = hx_os_codec_load(context, m_schema_, format, path,
			options, &owner, &error);
		if (diagnostic)
			*diagnostic = error;

		if (status != HX_OS_OK)
			return Failure(status, error.detail);

		return Decoded<T>(owner);
	}

	Result<void> save(const T &value, const char *path,
		int format = HX_OS_CONFIG_JSON,
		const hx_os_config_options *options = NULL,
		hx_os_config_error *diagnostic = NULL) const noexcept
	{
		if (m_schema_->size != sizeof(T))
			return Failure(HX_OS_INVALID);

		hx_os_config_error error = {};

		auto status = hx_os_codec_save(
			m_schema_, &value, format, path, options, &error);
		if (diagnostic)
			*diagnostic = error;

		return detail::Status(status, error.detail);
	}

private:
	const hx_os_codec_schema *m_schema_;
};
} // namespace hx
#endif
