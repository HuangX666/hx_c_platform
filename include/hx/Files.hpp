#ifndef HX_C_PLATFORM_CPP_FILES_HPP
#define HX_C_PLATFORM_CPP_FILES_HPP

#include <hx_os_file.h>
#include <hx/Status.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace hx {

class IoError : public StatusError {
public:
	IoError(const hx_os_error &error, size_t completed) noexcept
	    : StatusError(error)
	    , m_completed_(completed)
	{
	}
	size_t completed() const noexcept
	{
		return m_completed_;
	}

private:
	size_t m_completed_;
};

class File {
public:
	struct ReadResult {
		size_t bytes;
		bool eof;
	};
	explicit File(const char *path, uint32_t flags = HX_OS_FILE_READ,
		hx_os_context *context = NULL)
	    : m_file_(NULL)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_file_open(context, path, flags, &m_file_, &error),
			error);
	}
	File(const File &) = delete;
	File &operator=(const File &) = delete;
	File(File &&other) noexcept
	    : m_file_(other.m_file_)
	{
		other.m_file_ = NULL;
	}
	File &operator=(File &&other) noexcept
	{
		if (this != &other) {
			(void)hx_os_file_close(&m_file_, NULL);
			m_file_ = other.m_file_;
			other.m_file_ = NULL;
		}
		return *this;
	}
	~File()
	{
		(void)hx_os_file_close(&m_file_, NULL);
	}
	ReadResult readSome(void *data, size_t bytes)
	{
		size_t completed = 0;
		hx_os_error error;
		hx_os_status status = hx_os_file_read_some(
			m_file_, data, bytes, &completed, &error);
		if (status != HX_OS_OK && status != HX_OS_EOF)
			throw IoError(error, completed);
		return {completed, status == HX_OS_EOF};
	}
	void writeAll(const void *data, size_t bytes)
	{
		size_t completed = 0;
		hx_os_error error;
		if (hx_os_file_write_all(m_file_, data, bytes, &completed,
			    &error) != HX_OS_OK)
			throw IoError(error, completed);
	}
	void sync()
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_file_sync(m_file_, &error), error);
	}
	uint64_t size() const
	{
		uint64_t value;
		hx_os_error error;
		detail::CheckStatus(
			hx_os_file_size(m_file_, &value, &error), error);
		return value;
	}
	void close()
	{
		if (m_file_) {
			hx_os_error error;
			detail::CheckStatus(
				hx_os_file_close(&m_file_, &error), error);
		}
	}
	hx_os_file *get() const noexcept
	{
		return m_file_;
	}

private:
	hx_os_file *m_file_;
};

class Directory {
public:
	explicit Directory(const char *path, hx_os_context *context = NULL)
	    : m_directory_(NULL)
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_directory_open(context, path,
					    &m_directory_, &error),
			error);
	}
	Directory(const Directory &) = delete;
	Directory &operator=(const Directory &) = delete;
	Directory(Directory &&other) noexcept
	    : m_directory_(other.m_directory_)
	{
		other.m_directory_ = NULL;
	}
	Directory &operator=(Directory &&other) noexcept
	{
		if (this != &other) {
			hx_os_directory_close(m_directory_);
			m_directory_ = other.m_directory_;
			other.m_directory_ = NULL;
		}
		return *this;
	}
	~Directory()
	{
		hx_os_directory_close(m_directory_);
	}
	bool next(std::string &out)
	{
		std::string value(256, '\0');
		hx_os_error error;
		size_t required;
		for (;;) {
			hx_os_status status = hx_os_directory_next(m_directory_,
				&value[0], value.size(), &required, &error);
			if (status == HX_OS_EOF)
				return false;
			if (status == HX_OS_OUTPUT_FULL) {
				value.resize(required);
				continue;
			}
			detail::CheckStatus(status, error);
			value.resize(required - 1);
			out.swap(value);
			return true;
		}
	}

private:
	hx_os_directory *m_directory_;
};

class Files {
public:
	static std::string readText(const char *path)
	{
		hx_string text = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&text, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(
			hx_os_file_read_text(path, &text, &error), error);
		return std::string(hx_string_c_str(&text), text.size);
	}
	static void writeText(const char *path, const std::string &text)
	{
		size_t invalid;
		hx_os_status status = hx_os_text_validate_utf8(
			text.data(), text.size(), &invalid);
		if (status != HX_OS_OK)
			throw StatusError(status);
		hx_os_error error;
		detail::CheckStatus(hx_os_file_save_atomic(path, text.data(),
					    text.size(), &error),
			error);
	}
	static std::vector<unsigned char> load(
		const char *path, size_t maxBytes = 16 * 1024 * 1024)
	{
		void *data;
		size_t bytes;
		hx_os_error error;
		detail::CheckStatus(hx_os_file_load(NULL, path, maxBytes, &data,
					    &bytes, &error),
			error);
		std::unique_ptr<void, decltype(&hx_os_memory_free)> cleanup(
			data, hx_os_memory_free);
		const auto *begin = static_cast<const unsigned char *>(data);
		return std::vector<unsigned char>(begin, begin + bytes);
	}
	static bool exists(const char *path)
	{
		int value;
		hx_os_error error;
		detail::CheckStatus(
			hx_os_file_exists(path, &value, &error), error);
		return value != 0;
	}
	static void copy(const char *from, const char *to, bool replace = false)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_file_copy(from, to, replace, &error), error);
	}
	static void removeTree(const char *path)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_directory_remove_recursive(path, &error), error);
	}
	static std::string currentPath()
	{
		hx_string text = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&text, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(
			hx_os_path_current_string(&text, &error), error);
		return std::string(hx_string_c_str(&text), text.size);
	}
	static std::string absolutePath(const char *path)
	{
		hx_string text = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&text, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(
			hx_os_path_absolute_string(path, &text, &error), error);
		return std::string(hx_string_c_str(&text), text.size);
	}
};

} // namespace hx
#endif
