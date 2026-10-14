#ifndef HX_C_PLATFORM_COMPRESSION_H
#define HX_C_PLATFORM_COMPRESSION_H

#include "hx_bytes.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
	HX_OS_COMPRESS_ZLIB = 1,
	HX_OS_COMPRESS_GZIP,
	HX_OS_COMPRESS_XZ,
	HX_OS_COMPRESS_ZSTD
};

typedef struct hx_os_compression_options {
	uint32_t size, version;
	size_t max_output_bytes, max_codec_bytes;
	int level;
} hx_os_compression_options;

#define HX_OS_COMPRESSION_OPTIONS_INIT                                         \
	{                                                                      \
		sizeof(hx_os_compression_options), 1, 16 * 1024 * 1024,        \
			64 * 1024 * 1024, 3                                    \
	}

/* One complete frame only; trailing bytes and concatenated frames are
 * rejected. Arbitrary binary input, up to 64 MiB. Empty input compresses to
 * a valid frame. Output is transactional and uses its own context; aliasing
 * input with output is allowed. Default output limit is 16 MiB (max 64 MiB),
 * native codec working-memory limit 64 MiB, level 3. Zlib/Gzip/XZ levels are
 * 0..9, Zstd 1..19. Native codec allocations do not use the hx allocator;
 * output growth temporarily retains old+new storage. No background work.
 */
HX_OS_API int HX_OS_CALL hx_os_compression_available(unsigned format);
HX_OS_API hx_os_status HX_OS_CALL hx_os_compression_options_init(
	hx_os_compression_options *options, size_t capacity);
HX_OS_API hx_os_status HX_OS_CALL hx_os_compress(unsigned format,
	hx_os_bytes_view input, const hx_os_compression_options *options,
	hx_bytes *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_decompress(unsigned format,
	hx_os_bytes_view input, const hx_os_compression_options *options,
	hx_bytes *out, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
