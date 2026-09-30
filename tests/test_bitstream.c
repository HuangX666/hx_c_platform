#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)
int main(void)
{
	unsigned char bytes[12], before[12];
	hx_os_bit_reader reader;
	hx_os_bit_writer writer;
	uint64_t value = 99;
	unsigned offset, bits;
	if (!(hx_os_capabilities() & HX_OS_CAP_BITSTREAM)) {
		CHECK(hx_os_bit_reader_init(&reader, NULL, 0, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_bit_writer_align(NULL, NULL) ==
			HX_OS_NOT_SUPPORTED);
		return 0;
	}
	memset(bytes, 0xff, sizeof(bytes));
	CHECK(hx_os_bit_writer_init(&writer, bytes, 2, NULL) == HX_OS_OK);
	CHECK(hx_os_bit_writer_write_bits(&writer, 3, 5, NULL) == HX_OS_OK);
	CHECK(bytes[0] == 0xbf);
	CHECK(hx_os_bit_writer_align(&writer, NULL) == HX_OS_OK &&
		bytes[0] == 0xa0);
	CHECK(hx_os_bit_writer_write_bits(&writer, 8, 0x35, NULL) == HX_OS_OK);
	CHECK(bytes[1] == 0x35 && bytes[2] == 0xff);
	CHECK(hx_os_bit_reader_init(&reader, bytes, 2, NULL) == HX_OS_OK);
	CHECK(hx_os_bit_reader_read_bits(&reader, 3, &value, NULL) ==
			HX_OS_OK &&
		value == 5);
	CHECK(hx_os_bit_reader_align(&reader, NULL) == HX_OS_OK);
	CHECK(hx_os_bit_reader_read_bits(&reader, 8, &value, NULL) ==
			HX_OS_OK &&
		value == 0x35);
	CHECK(hx_os_bit_reader_read_bits(&reader, 1, &value, NULL) ==
			HX_OS_EOF &&
		value == 0x35 && reader.position_bits == 16);
	memcpy(before, bytes, sizeof(bytes));
	CHECK(hx_os_bit_writer_write_bits(&writer, 1, 1, NULL) ==
			HX_OS_OUTPUT_FULL &&
		!memcmp(before, bytes, sizeof(bytes)));
	CHECK(hx_os_bit_writer_write_bits(&writer, 0, 1, NULL) == HX_OS_RANGE);
	CHECK(hx_os_bit_writer_write_bits(&writer, 65, 0, NULL) ==
		HX_OS_INVALID);
	CHECK(hx_os_bit_reader_init(&reader, bytes, SIZE_MAX, NULL) ==
		HX_OS_RANGE);
	CHECK(hx_os_bit_reader_init(&reader, NULL, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_bit_reader_read_bits(&reader, 0, &value, NULL) ==
			HX_OS_OK &&
		value == 0);
	for (offset = 0; offset < 8; ++offset)
		for (bits = 0; bits <= 64; ++bits) {
			unsigned i;
			uint64_t expected = UINT64_C(0xa513e7812ca43569);
			if (bits < 64)
				expected &= (UINT64_C(1) << bits) - 1;
			memset(bytes, 0xcc, sizeof(bytes));
			CHECK(hx_os_bit_writer_init(&writer, bytes,
				      sizeof(bytes), NULL) == HX_OS_OK);
			CHECK(hx_os_bit_writer_write_bits(
				      &writer, offset, 0, NULL) == HX_OS_OK);
			CHECK(hx_os_bit_writer_write_bits(&writer, bits,
				      expected, NULL) == HX_OS_OK);
			/* Inspect each output bit independently of the reader.
			 */
			for (i = 0; i < bits; ++i)
				CHECK(((bytes[(offset + i) / 8] >>
					       (7 - (offset + i) % 8)) &
					      1u) ==
					((expected >> (bits - i - 1)) & 1u));
			CHECK(hx_os_bit_reader_init(&reader, bytes,
				      sizeof(bytes), NULL) == HX_OS_OK);
			CHECK(hx_os_bit_reader_read_bits(&reader, offset,
				      &value, NULL) == HX_OS_OK);
			CHECK(hx_os_bit_reader_read_bits(&reader, bits, &value,
				      NULL) == HX_OS_OK &&
				value == expected);
		}
	return 0;
}
