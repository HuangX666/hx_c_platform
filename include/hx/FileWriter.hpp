#ifndef HX_C_PLATFORM_CPP_FILE_WRITER_HPP
#define HX_C_PLATFORM_CPP_FILE_WRITER_HPP

#include <hx_os_buffered_file.h>
#include <hx/Values.hpp>

namespace hx {

/* Host-owned handle, like ThreadPool. Keep owner alive until close succeeds
 * or consumes it on a native close error. This facade's destructor does not
 * block, discard data, detach or terminate a worker.
 */
class FileWriter {
public:
	explicit FileWriter(hx_os_file_writer *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	static Result<FileWriter> open(hx_os_file_writer *&owner,
		const char *path, hx_os_context *context = NULL,
		const hx_os_file_writer_options *options = NULL) noexcept
	{
		if (owner)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};
		auto status = hx_os_file_writer_open_ex(context, path,
			HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
				HX_OS_FILE_TRUNCATE,
			options, &owner, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return FileWriter(owner);
	}

	Result<size_t> writeUntil(const void *data, size_t bytes,
		hx_os_deadline deadline) const noexcept
	{
		if (!*m_owner_)
			return Failure(HX_OS_CLOSED);

		hx_os_error error = {};
		size_t accepted = 0;
		auto status = hx_os_file_writer_write_until(
			*m_owner_, data, bytes, &accepted, deadline, &error);

		return Result<size_t>::withValue(accepted, status, error);
	}

	Result<size_t> write(StringView text,
		hx_os_deadline deadline =
			HX_OS_DEADLINE_INFINITE) const noexcept
	{
		return writeUntil(text.data(), text.size(), deadline);
	}

	Result<void> flushUntil(
		hx_os_deadline deadline, bool persistent = false) const noexcept
	{
		if (!*m_owner_)
			return Failure(HX_OS_CLOSED);

		hx_os_error error = {};
		auto status = hx_os_file_writer_flush_until(
			*m_owner_, persistent ? 1 : 0, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> retry() const noexcept
	{
		if (!*m_owner_)
			return Failure(HX_OS_CLOSED);

		hx_os_error error = {};
		auto status = hx_os_file_writer_retry(*m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<size_t> pending() const noexcept
	{
		if (!*m_owner_)
			return Failure(HX_OS_CLOSED);

		hx_os_error error = {};
		size_t bytes = 0;
		auto status =
			hx_os_file_writer_pending(*m_owner_, &bytes, &error);

		return Result<size_t>::withValue(bytes, status, error);
	}

	Result<void> closeUntil(hx_os_deadline deadline) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_file_writer_close_until(
			m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> stop() const noexcept
	{
		if (!*m_owner_)
			return Failure(HX_OS_CLOSED);

		hx_os_error error = {};
		auto status = hx_os_file_writer_stop(*m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<void> abortUntil(hx_os_deadline deadline) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_file_writer_abort_until(
			m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_file_writer **m_owner_;
};

} // namespace hx
#endif
