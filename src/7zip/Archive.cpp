#include "Bridge.hpp"
#include <C/7zVersion.h>

namespace hxArchive {

class Open final : public IArchiveOpenCallback,
		   public ICryptoGetTextPassword,
		   public Reference<Open> {
public:
	explicit Open(const hx_ar_bridge *host) noexcept
	    : Reference(host)
	{
	}

	HX_AR_REFERENCES

	HRESULT STDMETHODCALLTYPE QueryInterface(
		REFIID id, void **out) noexcept override
	{
		*out = nullptr;
		if (id == IID_IUnknown || id == IID_IArchiveOpenCallback)
			*out = static_cast<IArchiveOpenCallback *>(this);
		else if (id == IID_ICryptoGetTextPassword)
			*out = static_cast<ICryptoGetTextPassword *>(this);
		if (!*out)
			return E_NOINTERFACE;
		AddRef();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE SetTotal(
		const UInt64 *, const UInt64 *) noexcept override
	{
		return m_host_->check(m_host_->user) == HX_OS_OK ? S_OK
								 : E_ABORT;
	}

	HRESULT STDMETHODCALLTYPE SetCompleted(
		const UInt64 *, const UInt64 *) noexcept override
	{
		return m_host_->check(m_host_->user) == HX_OS_OK ? S_OK
								 : E_ABORT;
	}

	HRESULT STDMETHODCALLTYPE CryptoGetTextPassword(
		BSTR *out) noexcept override
	{
		return password(m_host_, out);
	}
};

class Update final : public IArchiveUpdateCallback,
		     public ICryptoGetTextPassword2,
		     public Reference<Update> {
public:
	explicit Update(const hx_ar_bridge *host) noexcept
	    : Reference(host)
	{
	}

	HX_AR_REFERENCES

	HRESULT STDMETHODCALLTYPE QueryInterface(
		REFIID id, void **out) noexcept override
	{
		*out = nullptr;
		if (id == IID_IUnknown || id == IID_IArchiveUpdateCallback ||
			id == IID_IProgress)
			*out = static_cast<IArchiveUpdateCallback *>(this);
		else if (id == IID_ICryptoGetTextPassword2)
			*out = static_cast<ICryptoGetTextPassword2 *>(this);
		if (!*out)
			return E_NOINTERFACE;
		AddRef();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE SetTotal(UInt64) noexcept override
	{
		return m_host_->check(m_host_->user) == HX_OS_OK ? S_OK
								 : E_ABORT;
	}

	HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64 *) noexcept override
	{
		return m_host_->check(m_host_->user) == HX_OS_OK ? S_OK
								 : E_ABORT;
	}

	HRESULT STDMETHODCALLTYPE GetUpdateItemInfo(UInt32 index, Int32 *data,
		Int32 *properties, UInt32 *original) noexcept override
	{
		if (index >= m_host_->count)
			return E_INVALIDARG;
		*data = 1;
		*properties = 1;
		*original = UINT32_MAX;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetProperty(
		UInt32 index, PROPID id, PROPVARIANT *out) noexcept override
	{
		hx_os_archive_entry entry;

		if (m_host_->source(m_host_->user, index, &entry, nullptr) !=
			HX_OS_OK)
			return E_ABORT;
		return property(m_host_, entry, id, out);
	}

	HRESULT STDMETHODCALLTYPE GetStream(
		UInt32 index, ISequentialInStream **out) noexcept override
	{
		hx_os_archive_entry entry;
		void *stream = nullptr;

		*out = nullptr;
		if (m_host_->source(m_host_->user, index, &entry, &stream) !=
			HX_OS_OK)
			return E_ABORT;
		if (stream) {
			*out = input(m_host_, stream, false);
			if (!*out)
				return E_OUTOFMEMORY;
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE SetOperationResult(
		Int32 result) noexcept override
	{
		return result == 0 ? S_OK
				   : failure(m_host_, E_FAIL,
					     "7z packing operation failed");
	}

	HRESULT STDMETHODCALLTYPE CryptoGetTextPassword2(
		Int32 *defined, BSTR *out) noexcept override
	{
		*defined = m_host_->password ? 1 : 0;
		*out = nullptr;
		return *defined ? password(m_host_, out) : S_OK;
	}
};

class Extract final : public IArchiveExtractCallback,
		      public ICryptoGetTextPassword,
		      public Reference<Extract> {
	IInArchive *m_archive_;

public:
	Extract(const hx_ar_bridge *host, IInArchive *archive) noexcept
	    : Reference(host)
	    , m_archive_(archive)
	{
	}

	HX_AR_REFERENCES

	HRESULT STDMETHODCALLTYPE QueryInterface(
		REFIID id, void **out) noexcept override
	{
		*out = nullptr;
		if (id == IID_IUnknown || id == IID_IArchiveExtractCallback ||
			id == IID_IProgress)
			*out = static_cast<IArchiveExtractCallback *>(this);
		else if (id == IID_ICryptoGetTextPassword)
			*out = static_cast<ICryptoGetTextPassword *>(this);
		if (!*out)
			return E_NOINTERFACE;
		AddRef();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE SetTotal(UInt64) noexcept override
	{
		return m_host_->check(m_host_->user) == HX_OS_OK ? S_OK
								 : E_ABORT;
	}

	HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64 *) noexcept override
	{
		return m_host_->check(m_host_->user) == HX_OS_OK ? S_OK
								 : E_ABORT;
	}

	HRESULT STDMETHODCALLTYPE GetStream(UInt32 index,
		ISequentialOutStream **out, Int32 mode) noexcept override
	{
		hx_os_archive_entry entry;
		void *stream = nullptr;
		char *name = nullptr;
		*out = nullptr;
		if (mode != NArchive::NExtract::NAskMode::kExtract)
			return S_OK;
		HRESULT result =
			metadata(m_host_, m_archive_, index, &entry, &name);
		if (result == S_OK &&
			m_host_->extract(m_host_->user, &entry, &stream) !=
				HX_OS_OK)
			result = E_ABORT;
		m_host_->free(name);
		if (result == S_OK && stream) {
			*out = output(m_host_, stream, false);
			if (!*out)
				result = E_OUTOFMEMORY;
		}
		return result;
	}

	HRESULT STDMETHODCALLTYPE PrepareOperation(Int32) noexcept override
	{
		return m_host_->check(m_host_->user) == HX_OS_OK ? S_OK
								 : E_ABORT;
	}

	HRESULT STDMETHODCALLTYPE SetOperationResult(
		Int32 result) noexcept override
	{
		if (result != NArchive::NExtract::NOperationResult::kOK) {
			m_host_->fail(m_host_->user,
				result ==
						NArchive::NExtract::
							NOperationResult::
								kWrongPassword
					? HX_OS_AUTH_FAILED
					: HX_OS_IO_ERROR,
				static_cast<uint32_t>(result),
				"7z entry verification failed");
			return E_ABORT;
		}
		return m_host_->finish(m_host_->user) == HX_OS_OK ? S_OK
								  : E_ABORT;
	}

	HRESULT STDMETHODCALLTYPE CryptoGetTextPassword(
		BSTR *out) noexcept override
	{
		return password(m_host_, out);
	}
};

using CreateObject = HRESULT(STDAPICALLTYPE *)(
	const GUID *, const GUID *, void **);
static const GUID archiveClass = {
	0x23170f69, 0x40c1, 0x278a, {0x10, 0, 0, 1, 0x10, 7, 0, 0}};

static HRESULT configure(const hx_ar_bridge *host, IUnknown *archive)
{
	ISetProperties *settings = nullptr;
	HRESULT result = archive->QueryInterface(
		IID_ISetProperties, reinterpret_cast<void **>(&settings));
	const wchar_t *names[] = {L"mt", L"x", L"he", L"s"};
	PROPVARIANT values[4] = {};

	if (result != S_OK)
		return failure(host, result, "7z properties interface");
	values[0].vt = VT_UI4;
	values[0].ulVal = host->workers;
	values[1].vt = VT_UI4;
	values[1].ulVal = host->preset == HX_OS_ARCHIVE_FAST ? 1
		: host->preset == HX_OS_ARCHIVE_SMALL	     ? 9
							     : 5;
	values[2].vt = VT_BOOL;
	values[2].boolVal = host->encrypt_names ? VARIANT_TRUE : VARIANT_FALSE;
	values[3].vt = VT_BOOL;
	values[3].boolVal = VARIANT_FALSE;
	result = settings->SetProperties(names, values, host->packing ? 4 : 1);
	settings->Release();
	return failure(host, result, "Configure 7z codec");
}

static HRESULT pack(const hx_ar_bridge *host, CreateObject create)
{
	IOutArchive *archive = nullptr;
	HRESULT result = create(&archiveClass, &IID_IOutArchive,
		reinterpret_cast<void **>(&archive));

	if (result != S_OK)
		return failure(host, result, "Create 7z writer");
	result = configure(host, archive);
	IOutStream *stream = output(host, host->stream);
	Update *callback = make<Update>(host);
	if (!stream || !callback)
		result = E_OUTOFMEMORY;
	if (result == S_OK)
		result = archive->UpdateItems(stream, host->count, callback);
	archive->Release();
	if (callback)
		callback->Release();
	if (stream)
		stream->Release();
	return failure(host, result, "Write 7z archive");
}

static HRESULT read(const hx_ar_bridge *host, CreateObject create)
{
	IInArchive *archive = nullptr;
	HRESULT result = create(&archiveClass, &IID_IInArchive,
		reinterpret_cast<void **>(&archive));

	if (result != S_OK)
		return failure(host, result, "Create 7z reader");
	IInStream *stream = input(host, host->stream);
	Open *opening = make<Open>(host);
	Extract *extracting = make<Extract>(host, archive);
	UInt32 count = 0, selected = UINT32_MAX;
	UInt64 search = 0;

	result = configure(host, archive);
	if (!stream || !opening || !extracting)
		result = E_OUTOFMEMORY;
	if (result == S_OK)
		result = archive->Open(stream, &search, opening);
	if (result == S_OK)
		result = archive->GetNumberOfItems(&count);
	if (result == S_OK && count > host->max_entries) {
		host->fail(host->user, HX_OS_LIMIT, 0, "7z entry count");
		result = E_ABORT;
	}
	for (UInt32 i = 0; result == S_OK && i < count; ++i) {
		hx_os_archive_entry entry;
		char *name = nullptr;
		int chosen = 0;

		result = metadata(host, archive, i, &entry, &name);
		if (result == S_OK &&
			host->inspect(host->user, i, &entry, &chosen) !=
				HX_OS_OK)
			result = E_ABORT;
		if (chosen)
			selected = i;
		host->free(name);
	}
	if (result == S_OK && host->listing != 1) {
		if (host->listing == 0)
			result = archive->Extract(
				nullptr, UINT32_MAX, 0, extracting);
		else if (selected != UINT32_MAX)
			result = archive->Extract(&selected, 1, 0, extracting);
	}
	HRESULT closed = archive->Close();
	if (result == S_OK)
		result = closed;
	archive->Release();
	if (extracting)
		extracting->Release();
	if (opening)
		opening->Release();
	if (stream)
		stream->Release();
	return failure(host, result, "Read 7z archive");
}

} // namespace hxArchive

#if defined(_WIN32)
#define HX_AR_EXPORT __declspec(dllexport)
#else
#define HX_AR_EXPORT __attribute__((visibility("default")))
#endif

extern "C" HX_AR_EXPORT hx_os_status HX_OS_CALL hx_archive_7zip_run(
	const hx_ar_bridge *host)
{
	if (!host || host->size != sizeof(*host) || host->version != 1)
		return HX_OS_NOT_SUPPORTED;
	hxArchive::CreateObject create;
	static_assert(sizeof(create) == sizeof(host->create_object),
		"Native loader function pointer size");
	std::memcpy(&create, &host->create_object, sizeof(create));
	Func_GetModuleProp module;
	PROPVARIANT value = {};
	std::memcpy(&module, &host->module_property, sizeof(module));
	HRESULT queried = module(NModulePropID::kInterfaceType, &value);
	bool compatible = queried == S_OK && value.vt == VT_UI4 &&
		value.ulVal ==
			NModuleInterfaceType::
				k_IUnknown_VirtDestructor_ThisModule;
	if (value.vt == VT_BSTR)
		hxArchive::freeString(value.bstrVal);
	std::memset(&value, 0, sizeof(value));
	queried = module(NModulePropID::kVersion, &value);
	compatible = compatible && queried == S_OK && value.vt == VT_UI4 &&
		value.ulVal == ((MY_VER_MAJOR << 16) | MY_VER_MINOR);
	if (value.vt == VT_BSTR)
		hxArchive::freeString(value.bstrVal);
	if (!compatible)
		return host->fail(host->user, HX_OS_NOT_SUPPORTED, 0,
			"7-Zip runtime version/interface differs from the "
			"pinned SDK");
	HRESULT result = host->packing ? hxArchive::pack(host, create)
				       : hxArchive::read(host, create);
	return result == S_OK ? host->check(host->user) : HX_OS_IO_ERROR;
}
