#ifndef HX_C_PLATFORM_CPP_FILE_EXTRA_HPP
#define HX_C_PLATFORM_CPP_FILE_EXTRA_HPP
#include <hx_os_file_extra.h>
#include <hx/Files.hpp>
namespace hx {

/* Borrows File until unlock/destruction. Range ownership cannot overlap. */
class FileRangeLock {
public:
	FileRangeLock() noexcept
	    : m_offset_(0)
	    , m_bytes_(0)
	    , m_file_(NULL)
	    , m_active_(false)
	{
	}
	static Result<FileRangeLock> acquire(File &file, uint64_t offset,
		uint64_t bytes, bool shared = false,
		hx_os_deadline deadline = HX_OS_DEADLINE_NOW) noexcept
	{
		FileRangeLock result;
		hx_os_error error = {};
		auto status = hx_os_file_lock_at(file.get(), offset, bytes,
			shared ? 1 : 0, deadline, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		result.m_offset_ = offset;
		result.m_bytes_ = bytes;
		result.m_file_ = file.get();
		result.m_active_ = true;
		return result;
	}
	FileRangeLock(const FileRangeLock &) = delete;
	FileRangeLock &operator=(const FileRangeLock &) = delete;
	FileRangeLock(FileRangeLock &&other) noexcept
	    : m_offset_(other.m_offset_)
	    , m_bytes_(other.m_bytes_)
	    , m_file_(other.m_file_)
	    , m_active_(other.m_active_)
	{
		other.m_file_ = NULL;
		other.m_active_ = false;
	}
	FileRangeLock &operator=(FileRangeLock &&) = delete;
	~FileRangeLock() noexcept
	{
		if (m_active_)
			(void)hx_os_file_unlock_at(
				m_file_, m_offset_, m_bytes_, NULL);
	}
	Result<void> unlock() noexcept
	{
		if (!m_active_)
			return Result<void>();
		hx_os_error error = {};
		auto status = hx_os_file_unlock_at(
			m_file_, m_offset_, m_bytes_, &error);
		if (status == HX_OS_OK)
			m_active_ = false;
		return detail::Status(status, error);
	}

private:
	uint64_t m_offset_, m_bytes_;
	hx_os_file *m_file_;
	bool m_active_;
};
class FileAttributes {
public:
	static Result<void> setTimes(const char *path,
		const hx_os_file_times &times, bool followLinks = true) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_file_set_times(
			path, &times, followLinks ? 1 : 0, &error);
		return detail::Status(status, error);
	}
	static Result<void> permissions(const char *path, unsigned permissions,
		bool followLinks = true) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_path_set_permissions(
			path, permissions, followLinks ? 1 : 0, &error);
		return detail::Status(status, error);
	}
};
} // namespace hx
#endif
