#ifndef HX_EXAMPLE_SERIALIZATION_SCHEMA_H
#define HX_EXAMPLE_SERIALIZATION_SCHEMA_H
#include <hx_codec.h>

typedef struct sample {
	hx_string_view unit;
	double value;
	int32_t channel;
} sample;

typedef struct limits {
	double low;
	double high;
} limits;

typedef struct device {
	hx_string_view name;
	hx_os_codec_array samples;
	limits range;
	int enabled;
} device;

static const hx_os_codec_field sample_fields[] = {
	HX_OS_CODEC_FIELD(sample, unit, HX_OS_CODEC_STRING),
	HX_OS_CODEC_FIELD(sample, value, HX_OS_CODEC_DOUBLE),
	HX_OS_CODEC_FIELD(sample, channel, HX_OS_CODEC_I32)};
static const hx_os_codec_schema sample_schema =
	HX_OS_CODEC_SCHEMA(sample, sample_fields, "sample");
static const hx_os_codec_type sample_type = {
	&sample_schema, NULL, sizeof(sample), 0, HX_OS_CODEC_OBJECT};
static const hx_os_codec_field limits_fields[] = {
	HX_OS_CODEC_FIELD(limits, low, HX_OS_CODEC_DOUBLE),
	HX_OS_CODEC_FIELD(limits, high, HX_OS_CODEC_DOUBLE)};
static const hx_os_codec_schema limits_schema =
	HX_OS_CODEC_SCHEMA(limits, limits_fields, "limits");
static const hx_os_codec_field device_fields[] = {
	HX_OS_CODEC_FIELD(device, name, HX_OS_CODEC_STRING),
	HX_OS_CODEC_ARRAY_FIELD(device, samples, &sample_type),
	HX_OS_CODEC_OBJECT_FIELD(device, range, &limits_schema),
	HX_OS_CODEC_FIELD(device, enabled, HX_OS_CODEC_BOOL)};
static const hx_os_codec_schema device_schema =
	HX_OS_CODEC_SCHEMA(device, device_fields, "device");
#endif
