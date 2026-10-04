#ifndef HX_C_PLATFORM_CPP_BITSTREAM_HPP
#define HX_C_PLATFORM_CPP_BITSTREAM_HPP

#include <hx_os_bitstream.h>
#include <hx/Status.hpp>

namespace hx {
/* Cursors borrow their buffers; copies do not extend buffer lifetime. */
class BitReader {
public:
	BitReader() noexcept
	    : m_reader_{}
	{
	}

	static Result<BitReader> create(const void *data, size_t bytes) noexcept
	{
		BitReader result;
		hx_os_error error = {};

		auto status = hx_os_bit_reader_init(
			&result.m_reader_, data, bytes, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	Result<uint64_t> read(unsigned bits) noexcept
	{
		uint64_t value = 0;
		hx_os_error error = {};

		auto status = hx_os_bit_reader_read_bits(
			&m_reader_, bits, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<void> align() noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_bit_reader_align(&m_reader_, &error);

		return detail::Status(status, error);
	}

	size_t remaining() const noexcept
	{
		return m_reader_.size_bits - m_reader_.position_bits;
	}

private:
	hx_os_bit_reader m_reader_;
};

class BitWriter {
public:
	BitWriter() noexcept
	    : m_writer_{}
	{
	}

	static Result<BitWriter> create(void *data, size_t bytes) noexcept
	{
		BitWriter result;
		hx_os_error error = {};

		auto status = hx_os_bit_writer_init(
			&result.m_writer_, data, bytes, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	Result<void> write(unsigned bits, uint64_t value) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_bit_writer_write_bits(
			&m_writer_, bits, value, &error);

		return detail::Status(status, error);
	}

	Result<void> align() noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_bit_writer_align(&m_writer_, &error);

		return detail::Status(status, error);
	}

	size_t bytesWritten() const noexcept
	{
		return (m_writer_.position_bits + 7) / 8;
	}

private:
	hx_os_bit_writer m_writer_;
};
} // namespace hx
#endif
