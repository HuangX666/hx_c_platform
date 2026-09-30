#ifndef HX_C_PLATFORM_CPP_MAPPING_HPP
#define HX_C_PLATFORM_CPP_MAPPING_HPP

#include <hx_os_vm.h>
#include <hx/Status.hpp>
#include <utility>

namespace hx {
/* Synchronous RAII; use close explicitly to observe native cleanup errors. */
class Pages {
public:
	explicit Pages(size_t bytes,
		unsigned protection = HX_OS_PAGE_READ | HX_OS_PAGE_WRITE,
		hx_os_context *context = NULL)
	    : m_pages_(NULL)
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_pages_alloc(context, bytes,
					    protection, &m_pages_, &error),
			error);
	}
	Pages(const Pages &) = delete;
	Pages &operator=(const Pages &) = delete;
	Pages(Pages &&other) noexcept
	    : m_pages_(other.m_pages_)
	{
		other.m_pages_ = NULL;
	}
	Pages &operator=(Pages &&other)
	{
		if (this != &other) {
			close();
			m_pages_ = other.m_pages_;
			other.m_pages_ = NULL;
		}
		return *this;
	}
	~Pages()
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
	void protect(unsigned protection)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_pages_protect(m_pages_, protection, &error),
			error);
	}
	void close()
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_pages_free(&m_pages_, &error), error);
	}

private:
	hx_os_pages *m_pages_;
};

class MappedFile {
public:
	explicit MappedFile(const char *path, int mode = HX_OS_MAP_READ)
	    : m_map_(NULL)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_file_map_open(path, mode, &m_map_, &error),
			error);
	}
	MappedFile(hx_os_file *file, uint64_t offset, uint64_t bytes,
		int mode = HX_OS_MAP_READ, hx_os_context *context = NULL)
	    : m_map_(NULL)
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_file_map_create(context, file, offset,
					    bytes, mode, &m_map_, &error),
			error);
	}
	MappedFile(const MappedFile &) = delete;
	MappedFile &operator=(const MappedFile &) = delete;
	MappedFile(MappedFile &&other) noexcept
	    : m_map_(other.m_map_)
	{
		other.m_map_ = NULL;
	}
	MappedFile &operator=(MappedFile &&other)
	{
		if (this != &other) {
			close();
			m_map_ = other.m_map_;
			other.m_map_ = NULL;
		}
		return *this;
	}
	~MappedFile()
	{
		(void)hx_os_file_map_destroy(&m_map_, NULL);
	}
	const void *data() const noexcept
	{
		return hx_os_file_map_view(m_map_);
	}
	void *writableData()
	{
		if (!m_map_)
			throw StatusError(HX_OS_CLOSED);
		void *view = hx_os_file_map_write_view(m_map_);
		if (!view)
			throw StatusError(HX_OS_ACCESS_DENIED);
		return view;
	}
	size_t size() const noexcept
	{
		return hx_os_file_map_size(m_map_);
	}
	void flush(bool durable = false)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_file_map_flush(m_map_, durable ? 1 : 0, &error),
			error);
	}
	void close()
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_file_map_destroy(&m_map_, &error), error);
	}

private:
	hx_os_file_map *m_map_;
};
} // namespace hx
#endif
