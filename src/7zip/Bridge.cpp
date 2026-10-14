#define INITGUID
#include "Bridge.hpp"

namespace hxArchive {

BSTR makeString(const wchar_t *text, size_t units)
{
	if (units > (UINT32_MAX - sizeof(wchar_t)) / sizeof(wchar_t))
		return nullptr;
#if defined(_WIN32)
	return SysAllocStringLen(text, static_cast<UINT>(units));
#else
	// Native 7-Zip 23+ BSTR ABI: uint32 byte length before wchar_t data.
	// Its DLL uses libc malloc/free for this interface-owned storage.
	size_t bytes = units * sizeof(wchar_t);
	auto memory = static_cast<unsigned char *>(
		std::malloc(sizeof(uint32_t) + bytes + sizeof(wchar_t)));

	if (!memory)
		return nullptr;
	uint32_t size = static_cast<uint32_t>(bytes);
	std::memcpy(memory, &size, sizeof(size));
	auto result = reinterpret_cast<wchar_t *>(memory + sizeof(size));
	std::memcpy(result, text, bytes);
	result[units] = 0;
	return result;
#endif
}

void freeString(BSTR text)
{
#if defined(_WIN32)
	SysFreeString(text);
#else
	if (text)
		std::free(reinterpret_cast<unsigned char *>(text) -
			sizeof(uint32_t));
#endif
}

size_t stringLength(BSTR text)
{
#if defined(_WIN32)
	return SysStringLen(text);
#else
	uint32_t bytes = 0;

	if (text)
		std::memcpy(&bytes,
			reinterpret_cast<unsigned char *>(text) - sizeof(bytes),
			sizeof(bytes));
	return bytes / sizeof(wchar_t);
#endif
}

HRESULT failure(const hx_ar_bridge *host, HRESULT result, const char *message)
{
	if (result != S_OK)
		host->fail(host->user,
			result == E_OUTOFMEMORY ? HX_OS_NO_MEMORY
						: HX_OS_IO_ERROR,
			static_cast<uint32_t>(result), message);
	return result;
}

HRESULT password(const hx_ar_bridge *host, BSTR *out)
{
	wchar_t *wide = nullptr;
	*out = nullptr;
	if (!host->password) {
		host->fail(host->user, HX_OS_AUTH_FAILED, 0,
			"7z password is required");
		return E_ABORT;
	}
	if (host->to_wide(host->user, host->password, &wide) != HX_OS_OK)
		return E_ABORT;
	size_t units = std::wcslen(wide);
	*out = makeString(wide, units);
	volatile wchar_t *wipe = wide;
	for (size_t i = 0; i < units; ++i)
		wipe[i] = 0;
	host->free(wide);
	return *out ? S_OK : failure(host, E_OUTOFMEMORY, "7z password buffer");
}

class Input final : public IInStream, public Reference<Input> {
	void *m_stream_;
	bool m_seekable_;

public:
	Input(const hx_ar_bridge *host, void *stream, bool seekable) noexcept
	    : Reference(host)
	    , m_stream_(stream)
	    , m_seekable_(seekable)
	{
	}

	~Input()
	{
		m_host_->release(m_stream_);
	}

	HX_AR_REFERENCES

	HRESULT STDMETHODCALLTYPE QueryInterface(
		REFIID id, void **out) noexcept override
	{
		*out = nullptr;
		if (id != IID_IUnknown && id != IID_ISequentialInStream &&
			(id != IID_IInStream || !m_seekable_))
			return E_NOINTERFACE;
		*out = static_cast<IInStream *>(this);
		AddRef();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Read(
		void *data, UInt32 bytes, UInt32 *done) noexcept override
	{
		uint32_t count = 0;
		auto status = m_host_->read(
			m_host_->user, m_stream_, data, bytes, &count);
		if (done)
			*done = count;
		return status == HX_OS_OK || status == HX_OS_EOF ? S_OK
								 : E_ABORT;
	}

	HRESULT STDMETHODCALLTYPE Seek(
		Int64 offset, UInt32 origin, UInt64 *position) noexcept override
	{
		uint64_t value;
		auto status = m_host_->seek(
			m_host_->user, m_stream_, offset, origin, &value);
		if (status == HX_OS_OK && position)
			*position = value;
		return status == HX_OS_OK ? S_OK : E_ABORT;
	}
};

class Output final : public IOutStream, public Reference<Output> {
	void *m_stream_;
	bool m_seekable_;

public:
	Output(const hx_ar_bridge *host, void *stream, bool seekable) noexcept
	    : Reference(host)
	    , m_stream_(stream)
	    , m_seekable_(seekable)
	{
	}

	~Output()
	{
		m_host_->release(m_stream_);
	}

	HX_AR_REFERENCES

	HRESULT STDMETHODCALLTYPE QueryInterface(
		REFIID id, void **out) noexcept override
	{
		*out = nullptr;
		if (id != IID_IUnknown && id != IID_ISequentialOutStream &&
			(id != IID_IOutStream || !m_seekable_))
			return E_NOINTERFACE;
		*out = static_cast<IOutStream *>(this);
		AddRef();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Write(
		const void *data, UInt32 bytes, UInt32 *done) noexcept override
	{
		uint32_t count = 0;
		auto status = m_host_->write(
			m_host_->user, m_stream_, data, bytes, &count);
		if (done)
			*done = count;
		return status == HX_OS_OK ? S_OK : E_ABORT;
	}

	HRESULT STDMETHODCALLTYPE Seek(
		Int64 offset, UInt32 origin, UInt64 *position) noexcept override
	{
		uint64_t value;
		auto status = m_host_->seek(
			m_host_->user, m_stream_, offset, origin, &value);
		if (status == HX_OS_OK && position)
			*position = value;
		return status == HX_OS_OK ? S_OK : E_ABORT;
	}

	HRESULT STDMETHODCALLTYPE SetSize(UInt64 bytes) noexcept override
	{
		return m_host_->resize(m_host_->user, m_stream_, bytes) ==
				HX_OS_OK
			? S_OK
			: E_ABORT;
	}
};

IInStream *input(const hx_ar_bridge *host, void *stream, bool seekable)
{
	auto value = make<Input>(host, stream, seekable);
	if (!value)
		host->release(stream);
	return value;
}

IOutStream *output(const hx_ar_bridge *host, void *stream, bool seekable)
{
	auto value = make<Output>(host, stream, seekable);
	if (!value)
		host->release(stream);
	return value;
}

HRESULT property(const hx_ar_bridge *host, const hx_os_archive_entry &entry,
	PROPID id, PROPVARIANT *out)
{
	std::memset(out, 0, sizeof(*out));
	if (id == kpidPath) {
		wchar_t *wide = nullptr;

		if (host->to_wide(host->user, entry.path.data, &wide) !=
			HX_OS_OK)
			return E_ABORT;
		out->bstrVal = makeString(wide, std::wcslen(wide));
		host->free(wide);
		if (!out->bstrVal)
			return failure(host, E_OUTOFMEMORY, "7z entry name");
		out->vt = VT_BSTR;
	}
	else if (id == kpidIsDir) {
		out->vt = VT_BOOL;
		out->boolVal = entry.kind == HX_OS_ARCHIVE_DIRECTORY
			? VARIANT_TRUE
			: VARIANT_FALSE;
	}
	else if (id == kpidSize) {
		out->vt = VT_UI8;
		out->uhVal.QuadPart = entry.bytes;
	}
	else if (id == kpidAttrib) {
		out->vt = VT_UI4;
		out->ulVal =
			entry.kind == HX_OS_ARCHIVE_DIRECTORY ? 0x10 : 0x20;
	}
	else if (id == kpidMTime && entry.modified_seconds >= -11644473600LL &&
		entry.modified_seconds <= 1833029933770LL) {
		uint64_t seconds = static_cast<uint64_t>(
			entry.modified_seconds + 11644473600LL);
		uint64_t fraction = entry.modified_nanoseconds / 100;

		if (seconds > (UINT64_MAX - fraction) / 10000000)
			return failure(host, E_INVALIDARG,
				"7z modification time range");
		uint64_t ticks = seconds * 10000000 + fraction;
		out->vt = VT_FILETIME;
		out->filetime.dwLowDateTime = static_cast<DWORD>(ticks);
		out->filetime.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
	}
	return S_OK;
}

HRESULT metadata(const hx_ar_bridge *host, IInArchive *archive, UInt32 index,
	hx_os_archive_entry *entry, char **name)
{
	static const PROPID properties[] = {kpidPath, kpidIsDir, kpidSize,
		kpidMTime, kpidEncrypted, kpidSymLink, kpidHardLink, kpidAttrib,
		kpidPosixAttrib};
	HRESULT result = S_OK;

	std::memset(entry, 0, sizeof(*entry));
	*name = nullptr;
	for (size_t i = 0; i < sizeof(properties) / sizeof(properties[0]);
		++i) {
		PROPVARIANT value = {};
		PROPID id = properties[i];

		result = archive->GetProperty(index, id, &value);
		if (result == S_OK && id == kpidPath && value.vt == VT_BSTR) {
			size_t units = stringLength(value.bstrVal);

			if (!value.bstrVal || units > host->max_path_bytes ||
				units != std::wcslen(value.bstrVal))
				result = E_INVALIDARG;
			else if (host->from_wide(host->user, value.bstrVal,
					 units, name) != HX_OS_OK)
				result = E_ABORT;
			else {
				entry->path.data = *name;
				entry->path.size = std::strlen(*name);
			}
		}
		else if (result == S_OK && id == kpidIsDir &&
			value.vt == VT_BOOL) {
			if (value.boolVal && entry->kind != HX_OS_ARCHIVE_LINK)
				entry->kind = HX_OS_ARCHIVE_DIRECTORY;
		}
		else if (result == S_OK && id == kpidSize &&
			value.vt == VT_UI8) {
			entry->bytes = value.uhVal.QuadPart;
			entry->flags |= HX_OS_ARCHIVE_SIZE_KNOWN;
		}
		else if (result == S_OK && id == kpidMTime &&
			value.vt == VT_FILETIME) {
			uint64_t ticks = value.filetime.dwLowDateTime |
				(static_cast<uint64_t>(
					 value.filetime.dwHighDateTime)
					<< 32);
			entry->modified_seconds =
				static_cast<int64_t>(ticks / 10000000) -
				11644473600LL;
			entry->modified_nanoseconds =
				static_cast<uint32_t>((ticks % 10000000) * 100);
			entry->flags |= HX_OS_ARCHIVE_MTIME_KNOWN;
		}
		else if (result == S_OK && id == kpidEncrypted &&
			value.vt == VT_BOOL && value.boolVal)
			entry->flags |= HX_OS_ARCHIVE_ENCRYPTED;
		else if (result == S_OK &&
			(id == kpidSymLink || id == kpidHardLink) &&
			value.vt == VT_BSTR && stringLength(value.bstrVal))
			entry->kind = HX_OS_ARCHIVE_LINK;
		else if (result == S_OK && id == kpidAttrib &&
			value.vt == VT_UI4) {
			uint32_t kind = (value.ulVal >> 16) & 0170000;

			if ((value.ulVal & 0x400) || kind == 0120000)
				entry->kind = HX_OS_ARCHIVE_LINK;
			else if (kind && kind != 0100000 && kind != 0040000)
				entry->kind = HX_OS_ARCHIVE_OTHER;
		}
		else if (result == S_OK && id == kpidPosixAttrib &&
			value.vt == VT_UI4) {
			uint32_t kind = value.ulVal & 0170000;
			if (kind == 0120000)
				entry->kind = HX_OS_ARCHIVE_LINK;
			else if (kind && kind != 0100000 && kind != 0040000)
				entry->kind = HX_OS_ARCHIVE_OTHER;
		}
		if (value.vt == VT_BSTR)
			freeString(value.bstrVal);
		if (result != S_OK)
			break;
	}
	if (result == S_OK && !*name)
		result = E_INVALIDARG;
	return failure(host, result, "Read 7z entry metadata");
}

} // namespace hxArchive
