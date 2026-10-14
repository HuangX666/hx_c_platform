#ifndef HX_C_PLATFORM_CPP_STREAM_HPP
#define HX_C_PLATFORM_CPP_STREAM_HPP

#include <hx_os_stream.h>
#include <hx_os_varint.h>
#include <hx/Values.hpp>

namespace hx {
namespace detail {
template <typename T, bool Signed> struct StreamInteger;

template <typename T> struct StreamInteger<T, false> {
	static hx_os_status read(hx_os_stream *stream, int order, T *out,
		hx_os_error *error) noexcept
	{
		uint64_t value = 0;
		auto status = hx_os_stream_read_uint(
			stream, sizeof(T), order, &value, error);
		if (status == HX_OS_OK)
			*out = static_cast<T>(value);

		return status;
	}

	static hx_os_status write(hx_os_stream *stream, int order, T value,
		hx_os_error *error) noexcept
	{
		return hx_os_stream_write_uint(stream, sizeof(T), order,
			static_cast<uint64_t>(value), error);
	}
};

template <typename T> struct StreamInteger<T, true> {
	static hx_os_status read(hx_os_stream *stream, int order, T *out,
		hx_os_error *error) noexcept
	{
		int64_t value = 0;
		auto status = hx_os_stream_read_int(
			stream, sizeof(T), order, &value, error);
		if (status == HX_OS_OK)
			*out = static_cast<T>(value);

		return status;
	}

	static hx_os_status write(hx_os_stream *stream, int order, T value,
		hx_os_error *error) noexcept
	{
		return hx_os_stream_write_int(stream, sizeof(T), order,
			static_cast<int64_t>(value), error);
	}
};

template <typename T>
struct StreamNumber : StreamInteger<T, std::is_signed<T>::value> {
	static_assert(std::is_integral<T>::value && sizeof(T) <= 8 &&
			!std::is_same<T, bool>::value,
		"Use an integer up to 64 bits, float or double");
};

template <> struct StreamNumber<float> {
	static hx_os_status read(hx_os_stream *stream, int order, float *out,
		hx_os_error *error) noexcept
	{
		return hx_os_stream_read_f32(stream, order, out, error);
	}

	static hx_os_status write(hx_os_stream *stream, int order, float value,
		hx_os_error *error) noexcept
	{
		return hx_os_stream_write_f32(stream, order, value, error);
	}
};

template <> struct StreamNumber<double> {
	static hx_os_status read(hx_os_stream *stream, int order, double *out,
		hx_os_error *error) noexcept
	{
		return hx_os_stream_read_f64(stream, order, out, error);
	}

	static hx_os_status write(hx_os_stream *stream, int order, double value,
		hx_os_error *error) noexcept
	{
		return hx_os_stream_write_f64(stream, order, value, error);
	}
};
} // namespace detail

class Stream {
public:
	struct View {
		const void *data;
		size_t size;
	};

	Stream() noexcept
	    : m_stream_(NULL)
	{
	}

	Stream(Stream &&other) noexcept
	    : m_stream_(other.m_stream_)
	{
		other.m_stream_ = NULL;
	}

	Stream &operator=(Stream &&other) noexcept
	{
		std::swap(m_stream_, other.m_stream_);
		return *this;
	}

	Stream(const Stream &) = delete;
	Stream &operator=(const Stream &) = delete;

	~Stream() noexcept
	{
		(void)hx_os_stream_close(&m_stream_, NULL);
	}

	static Result<Stream> open(const char *path, uint32_t flags,
		hx_os_context *context = NULL) noexcept
	{
		Stream result;
		hx_os_error error = {};
		auto status = hx_os_stream_open(
			context, path, flags, &result.m_stream_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<Stream> openRead(
		const char *path, hx_os_context *context = NULL) noexcept
	{
		return open(path, HX_OS_FILE_READ, context);
	}

	static Result<Stream> openWrite(
		const char *path, hx_os_context *context = NULL) noexcept
	{
		return open(path,
			HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
				HX_OS_FILE_TRUNCATE,
			context);
	}

	static Result<Stream> memory(
		size_t maxBytes = 0, hx_os_context *context = NULL) noexcept
	{
		Stream result;
		hx_os_error error = {};
		auto status = hx_os_stream_memory(
			context, maxBytes, &result.m_stream_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<Stream> fromMemory(const void *data, size_t bytes,
		hx_os_context *context = NULL) noexcept
	{
		Stream result;
		hx_os_error error = {};
		auto status = hx_os_stream_from_memory(
			context, data, bytes, &result.m_stream_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	template <typename T>
	Result<T> read(int order = HX_OS_LITTLE_ENDIAN) noexcept
	{
		if (!m_stream_)
			return Failure(HX_OS_CLOSED);

		T value = {};
		hx_os_error error = {};
		auto status = detail::StreamNumber<T>::read(
			m_stream_, order, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	template <typename T>
	Result<void> write(T value, int order = HX_OS_LITTLE_ENDIAN) noexcept
	{
		if (!m_stream_)
			return Failure(HX_OS_CLOSED);

		hx_os_error error = {};
		auto status = detail::StreamNumber<T>::write(
			m_stream_, order, value, &error);

		return detail::Status(status, error);
	}

	Result<uint64_t> readVarUInt() noexcept
	{
		if (!m_stream_)
			return Failure(HX_OS_CLOSED);

		uint64_t value = 0;
		hx_os_error error = {};
		auto status =
			hx_os_stream_read_varuint(m_stream_, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<int64_t> readVarInt() noexcept
	{
		if (!m_stream_)
			return Failure(HX_OS_CLOSED);

		int64_t value = 0;
		hx_os_error error = {};
		auto status =
			hx_os_stream_read_varint(m_stream_, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<size_t> writeVarUInt(uint64_t value) noexcept
	{
		if (!m_stream_)
			return Failure(HX_OS_CLOSED);

		size_t completed = 0;
		hx_os_error error = {};
		auto status = hx_os_stream_write_varuint(
			m_stream_, value, &completed, &error);

		return Result<size_t>::withValue(completed, status, error);
	}

	Result<size_t> writeVarInt(int64_t value) noexcept
	{
		if (!m_stream_)
			return Failure(HX_OS_CLOSED);

		size_t completed = 0;
		hx_os_error error = {};
		auto status = hx_os_stream_write_varint(
			m_stream_, value, &completed, &error);

		return Result<size_t>::withValue(completed, status, error);
	}

	Result<uint64_t> seek(
		int64_t offset, int origin = HX_OS_SEEK_BEGIN) noexcept
	{
		if (!m_stream_)
			return Failure(HX_OS_CLOSED);

		uint64_t position = 0;
		hx_os_error error = {};
		auto status = hx_os_stream_seek(
			m_stream_, offset, origin, &position, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return position;
	}

	Result<uint64_t> tell() noexcept
	{
		return seek(0, HX_OS_SEEK_CURRENT);
	}

	Result<size_t> readBytes(void *data, size_t bytes) noexcept
	{
		if (!m_stream_)
			return Failure(HX_OS_CLOSED);

		size_t completed = 0;
		hx_os_error error = {};
		auto status = hx_os_stream_read_exact(
			m_stream_, data, bytes, &completed, &error);
		return Result<size_t>::withValue(completed, status, error);
	}

	Result<size_t> writeBytes(const void *data, size_t bytes) noexcept
	{
		if (!m_stream_)
			return Failure(HX_OS_CLOSED);

		size_t completed = 0;
		hx_os_error error = {};
		auto status = hx_os_stream_write_all(
			m_stream_, data, bytes, &completed, &error);
		return Result<size_t>::withValue(completed, status, error);
	}

	Result<View> view() noexcept
	{
		if (!m_stream_)
			return Failure(HX_OS_CLOSED);

		View result = {};
		hx_os_error error = {};
		auto status = hx_os_stream_data(
			m_stream_, &result.data, &result.size, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	Result<void> flush(bool persistent = false) noexcept
	{
		if (!m_stream_)
			return Failure(HX_OS_CLOSED);

		hx_os_error error = {};
		auto status = hx_os_stream_flush(
			m_stream_, persistent ? 1 : 0, &error);

		return detail::Status(status, error);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_stream_close(&m_stream_, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_stream *m_stream_;
};
} // namespace hx
#endif
