#ifndef HX_C_PLATFORM_SEVEN_ZIP_BRIDGE_HPP
#define HX_C_PLATFORM_SEVEN_ZIP_BRIDGE_HPP

#include "archive_bridge.h"
#include <7zip/Archive/IArchive.h>
#include <7zip/IPassword.h>
#include <7zip/ICoder.h>
#include <atomic>
#include <new>
#include <cstring>
#include <cwchar>
#include <cstdlib>

namespace hxArchive {

BSTR makeString(const wchar_t *text, size_t units);
void freeString(BSTR text);
size_t stringLength(BSTR text);
HRESULT failure(const hx_ar_bridge *host, HRESULT result, const char *message);
HRESULT password(const hx_ar_bridge *host, BSTR *out);
HRESULT property(const hx_ar_bridge *host, const hx_os_archive_entry &entry,
	PROPID id, PROPVARIANT *out);
HRESULT metadata(const hx_ar_bridge *host, IInArchive *archive, UInt32 index,
	hx_os_archive_entry *entry, char **name);
IInStream *input(const hx_ar_bridge *host, void *stream, bool seekable = true);
IOutStream *output(
	const hx_ar_bridge *host, void *stream, bool seekable = true);

template <class T> class Reference {
protected:
	const hx_ar_bridge *m_host_;
	std::atomic<ULONG> m_references_;

	explicit Reference(const hx_ar_bridge *host) noexcept
	    : m_host_(host)
	    , m_references_(1)
	{
	}

public:
	ULONG retain() noexcept
	{
		return m_references_.fetch_add(1, std::memory_order_relaxed) +
			1;
	}

	ULONG release() noexcept
	{
		ULONG remaining =
			m_references_.fetch_sub(1, std::memory_order_acq_rel) -
			1;

		if (!remaining) {
			T *object = static_cast<T *>(this);
			auto dispose = m_host_->free;

			object->~T();
			dispose(object);
		}
		return remaining;
	}
};

template <class T, class... Args>
T *make(const hx_ar_bridge *host, Args... args)
{
	void *memory = host->allocate(host->user, sizeof(T));

	if (!memory)
		return nullptr;
	return new (memory) T(host, args...);
}

/* COM method names/signatures follow the vendor interface exactly. */
#define HX_AR_REFERENCES                                                       \
	ULONG STDMETHODCALLTYPE AddRef() noexcept override                     \
	{                                                                      \
		return retain();                                               \
	}                                                                      \
	ULONG STDMETHODCALLTYPE Release() noexcept override                    \
	{                                                                      \
		return release();                                              \
	}

} // namespace hxArchive
#endif
