#ifndef HX_EXAMPLE_SERIALIZATION_SCHEMA_H
#define HX_EXAMPLE_SERIALIZATION_SCHEMA_H

#include <hx_codec_macros.h>

HX_CODEC_STRUCT(sample, HX_CODEC_STRING(unit), HX_CODEC_DOUBLE(value),
	HX_CODEC_I32(channel));
HX_CODEC_STRUCT(limits, HX_CODEC_DOUBLE(low), HX_CODEC_DOUBLE(high));
HX_CODEC_STRUCT(device, HX_CODEC_STRING(name), HX_CODEC_ARRAY(samples, sample),
	HX_CODEC_OBJECT(range, limits), HX_CODEC_BOOL(enabled));
#endif
