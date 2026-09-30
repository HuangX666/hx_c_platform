#ifndef HX_C_PLATFORM_CPP_BITSTREAM_HPP
#define HX_C_PLATFORM_CPP_BITSTREAM_HPP
#include <hx_os_bitstream.h>
#include <hx/Status.hpp>
namespace hx {
/* Buffers are borrowed; neither copying a cursor nor destroying it extends
 * buffer lifetime. Synchronize overlapping writes separately.
 */
class BitReader {
public:
	BitReader(const void *data, size_t bytes)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_bit_reader_init(&m_reader_, data, bytes, &error),
			error);
	}
	uint64_t read(unsigned bits)
	{
		uint64_t value;
		hx_os_error error;
		detail::CheckStatus(hx_os_bit_reader_read_bits(
					    &m_reader_, bits, &value, &error),
			error);
		return value;
	}
	void align()
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_bit_reader_align(&m_reader_, &error), error);
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
	BitWriter(void *data, size_t bytes)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_bit_writer_init(&m_writer_, data, bytes, &error),
			error);
	}
	void write(unsigned bits, uint64_t value)
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_bit_writer_write_bits(
					    &m_writer_, bits, value, &error),
			error);
	}
	void align()
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_bit_writer_align(&m_writer_, &error), error);
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
