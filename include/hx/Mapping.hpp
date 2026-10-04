#ifndef HX_C_PLATFORM_CPP_MAPPING_HPP
#define HX_C_PLATFORM_CPP_MAPPING_HPP

#include <hx_os_vm.h>
#include <hx/Status.hpp>

namespace hx {

/* Explicit close reports native failures. Move assignment swaps ownership,
 * so a retryable close failure cannot be hidden by overwriting a handle.
 */
class Pages {
public:
	Pages() noexcept
	    : m_pages_(NULL)
	{
	}

	static Result<Pages> allocate(size_t bytes,
		unsigned protection = HX_OS_PAGE_READ | HX_OS_PAGE_WRITE,
		hx_os_context *context = NULL) noexcept
	{
		Pages result;
		hx_os_error error = {};

		auto status = hx_os_pages_alloc(
			context, bytes, protection, &result.m_pages_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	Pages(const Pages &) = delete;

	Pages &operator=(const Pages &) = delete;

	Pages(Pages &&other) noexcept
	    : m_pages_(other.m_pages_)
	{
		other.m_pages_ = NULL;
	}

	Pages &operator=(Pages &&other) noexcept
	{
		std::swap(m_pages_, other.m_pages_);

		return *this;
	}

	~Pages() noexcept
	{
		(void)hx_os_pages_free(&m_pages_, NULL);
	}

	void *data() const noexcept
	{
		return hx_os_pages_data(m_pages_);
	}

	size_t size() const noexcept
	{
		return hx_os_pages_size(m_pages_);
	}

	Result<void> protect(unsigned protection) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_pages_protect(m_pages_, protection, &error);

		return detail::Status(status, error);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_pages_free(&m_pages_, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_pages *m_pages_;
};

class MappedFile {
public:
	MappedFile() noexcept
	    : m_map_(NULL)
	{
	}

	static Result<MappedFile> open(
		const char *path, int mode = HX_OS_MAP_READ) noexcept
	{
		MappedFile result;
		hx_os_error error = {};
		auto status =
			hx_os_file_map_open(path, mode, &result.m_map_, &error);

		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<MappedFile> create(hx_os_file *file, uint64_t offset,
		uint64_t bytes, int mode = HX_OS_MAP_READ,
		hx_os_context *context = NULL) noexcept
	{
		MappedFile result;
		hx_os_error error = {};

		auto status = hx_os_file_map_create(context, file, offset,
			bytes, mode, &result.m_map_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	MappedFile(const MappedFile &) = delete;

	MappedFile &operator=(const MappedFile &) = delete;

	MappedFile(MappedFile &&other) noexcept
	    : m_map_(other.m_map_)
	{
		other.m_map_ = NULL;
	}

	MappedFile &operator=(MappedFile &&other) noexcept
	{
		std::swap(m_map_, other.m_map_);

		return *this;
	}

	~MappedFile() noexcept
	{
		(void)hx_os_file_map_destroy(&m_map_, NULL);
	}

	const void *data() const noexcept
	{
		return hx_os_file_map_view(m_map_);
	}

	Result<void *> writableData() noexcept
	{
		if (!m_map_)
			return Failure(HX_OS_CLOSED);

		void *view = hx_os_file_map_write_view(m_map_);
		if (!view)
			return Failure(HX_OS_ACCESS_DENIED);

		return view;
	}

	size_t size() const noexcept
	{
		return hx_os_file_map_size(m_map_);
	}

	Result<void> flush(bool durable = false) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_file_map_flush(m_map_, durable ? 1 : 0, &error);

		return detail::Status(status, error);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_file_map_destroy(&m_map_, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_file_map *m_map_;
};
} // namespace hx
#endif
