#ifndef HX_C_PLATFORM_CPP_FILE_EXTRA_HPP
#define HX_C_PLATFORM_CPP_FILE_EXTRA_HPP
#include <hx_os_file_extra.h>
#include <hx/Files.hpp>

namespace hx {
/* Borrows File through unlock/destruction; no overlapping range ownership. */
class FileRangeLock {
public:
	FileRangeLock(File &file, uint64_t offset, uint64_t bytes,
		bool shared = false,
		hx_os_deadline deadline = HX_OS_DEADLINE_NOW)
	    : m_offset_(offset)
	    , m_bytes_(bytes)
	    , m_file_(file.get())
	    , m_active_(false)
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_file_lock_at(m_file_, offset, bytes,
					    shared ? 1 : 0, deadline, &error),
			error);
		m_active_ = true;
	}
	FileRangeLock(const FileRangeLock &) = delete;
	FileRangeLock &operator=(const FileRangeLock &) = delete;
	FileRangeLock(FileRangeLock &&other) noexcept
	    : m_offset_(other.m_offset_)
	    , m_bytes_(other.m_bytes_)
	    , m_file_(other.m_file_)
	    , m_active_(other.m_active_)
	{
		other.m_active_ = false;
		other.m_file_ = NULL;
	}
	FileRangeLock &operator=(FileRangeLock &&other) = delete;
	~FileRangeLock()
	{
		if (m_active_)
			(void)hx_os_file_unlock_at(
				m_file_, m_offset_, m_bytes_, NULL);
	}
	void unlock()
	{
		if (!m_active_)
			return;
		hx_os_error error;
		detail::CheckStatus(hx_os_file_unlock_at(m_file_, m_offset_,
					    m_bytes_, &error),
			error);
		m_active_ = false;
	}

private:
	uint64_t m_offset_;
	uint64_t m_bytes_;
	hx_os_file *m_file_;
	bool m_active_;
};

class FileAttributes {
public:
	static void setTimes(const char *path, const hx_os_file_times &times,
		bool followLinks = true)
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_file_set_times(path, &times,
					    followLinks ? 1 : 0, &error),
			error);
	}
	static void permissions(
		const char *path, unsigned permissions, bool followLinks = true)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_path_set_permissions(
				path, permissions, followLinks ? 1 : 0, &error),
			error);
	}
};
} // namespace hx
#endif
