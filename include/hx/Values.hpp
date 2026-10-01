#ifndef HX_C_PLATFORM_CPP_VALUES_HPP
#define HX_C_PLATFORM_CPP_VALUES_HPP
#include <hx/Status.hpp>
#include <hx_string.h>
#include <cstring>
namespace hx {
/* Call-borrowed bytes; no implicit allocating conversion at API entry. */
class StringView {
public:
	StringView(const char *text = NULL) noexcept
	    : m_view_{text, text ? std::strlen(text) : 0}
	{
	}
	StringView(const char *text, size_t bytes) noexcept
	    : m_view_{text, bytes}
	{
	}
	StringView(hx_string_view text) noexcept
	    : m_view_(text)
	{
	}
	const char *data() const noexcept
	{
		return m_view_.data;
	}
	size_t size() const noexcept
	{
		return m_view_.size;
	}
	hx_string_view native() const noexcept
	{
		return m_view_;
	}

private:
	hx_string_view m_view_;
};
class String {
public:
	explicit String(hx_os_context *context = NULL) noexcept
	    : m_string_{}
	{
		hx_string_init(&m_string_, context);
	}
	String(String &&other) noexcept
	    : m_string_{}
	{
		hx_string_move(&m_string_, &other.m_string_);
	}
	String &operator=(String &&other) noexcept
	{
		hx_string_move(&m_string_, &other.m_string_);
		return *this;
	}
	String(const String &) = delete;
	String &operator=(const String &) = delete;
	~String() noexcept
	{
		hx_string_destroy(&m_string_);
	}
	static String adopt(hx_string &owned) noexcept
	{
		String result;
		hx_string_move(&result.m_string_, &owned);
		return result;
	}
	static Result<String> copy(
		StringView text, hx_os_context *context = NULL) noexcept
	{
		String value(context);
		auto result = value.assign(text);
		if (!result)
			return Failure(result.error());
		return value;
	}
	Result<void> assign(StringView text) noexcept
	{
		hx_os_error error = {};
		hx_os_status status =
			hx_string_assign(&m_string_, text.native(), &error);
		return detail::Status(status, error);
	}
	const char *c_str() const noexcept
	{
		return hx_string_c_str(&m_string_);
	}
	const char *data() const noexcept
	{
		return c_str();
	}
	size_t size() const noexcept
	{
		return m_string_.size;
	}
	bool empty() const noexcept
	{
		return size() == 0;
	}
	StringView view() const noexcept
	{
		return StringView(data(), size());
	}
	operator StringView() const noexcept
	{
		return view();
	}
	bool operator==(StringView other) const noexcept
	{
		return size() == other.size() &&
			(!size() || !std::memcmp(data(), other.data(), size()));
	}
	bool operator!=(StringView other) const noexcept
	{
		return !(*this == other);
	}
	hx_string *native() noexcept
	{
		return &m_string_;
	}
	const hx_string *native() const noexcept
	{
		return &m_string_;
	}

private:
	hx_string m_string_;
};
class Buffer {
public:
	Buffer() noexcept
	    : m_data_(NULL)
	    , m_size_(0)
	{
	}
	Buffer(Buffer &&other) noexcept
	    : m_data_(other.m_data_)
	    , m_size_(other.m_size_)
	{
		other.m_data_ = NULL;
		other.m_size_ = 0;
	}
	Buffer &operator=(Buffer &&other) noexcept
	{
		if (this != &other) {
			hx_os_memory_free(m_data_);
			m_data_ = other.m_data_;
			m_size_ = other.m_size_;
			other.m_data_ = NULL;
			other.m_size_ = 0;
		}
		return *this;
	}
	Buffer(const Buffer &) = delete;
	Buffer &operator=(const Buffer &) = delete;
	~Buffer() noexcept
	{
		hx_os_memory_free(m_data_);
	}
	static Buffer adopt(void *owned, size_t bytes) noexcept
	{
		Buffer result;
		result.m_data_ = owned;
		result.m_size_ = bytes;
		return result;
	}
	static Result<Buffer> allocate(size_t bytes,
		hx_os_context *context = NULL, size_t alignment = 0) noexcept
	{
		void *memory = NULL;
		hx_os_error error = {};
		hx_os_status status = hx_os_memory_alloc(
			context, bytes, alignment, &memory, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return adopt(memory, bytes);
	}
	unsigned char *data() noexcept
	{
		return static_cast<unsigned char *>(m_data_);
	}
	const unsigned char *data() const noexcept
	{
		return static_cast<const unsigned char *>(m_data_);
	}
	size_t size() const noexcept
	{
		return m_size_;
	}
	bool empty() const noexcept
	{
		return m_size_ == 0;
	}

private:
	void *m_data_;
	size_t m_size_;
};
namespace detail {
template <typename Function>
Result<String> StringQuery(
	Function function, hx_os_context *context = NULL) noexcept
{
	String value(context);
	hx_os_error error = {};
	hx_os_status status = function(value.native(), &error);
	if (status != HX_OS_OK)
		return Failure(status, error);
	return value;
}
} // namespace detail
} // namespace hx
#endif
