#ifndef HX_TEST_CODEC_MACROS_FIXTURE_H
#define HX_TEST_CODEC_MACROS_FIXTURE_H
#include <hx_codec_macros.h>

HX_CODEC_STRUCT(macro_one, HX_CODEC_I32(value));
HX_CODEC_STRUCT_NAMED(macro_connection, "connection",
	HX_CODEC_NAME("server_host", HX_CODEC_STRING(host)),
	HX_CODEC_OPTIONAL(HX_CODEC_XML_ATTRIBUTE(
		HX_CODEC_NAME("port", HX_CODEC_I32(socket_port)))),
	HX_CODEC_NAME("caption", HX_CODEC_OPTIONAL(HX_CODEC_STRING(note))));
HX_CODEC_STRUCT(macro_container, HX_CODEC_ARRAY(values, int32_t),
	HX_CODEC_ARRAY(switches, hx_codec_bool),
	HX_CODEC_ARRAY(texts, hx_string_view),
	HX_CODEC_ARRAY(objects, macro_connection),
	HX_CODEC_FIXED(points, double, 3),
	HX_CODEC_FIXED(connections, macro_connection, 2),
	HX_CODEC_BUFFER(label, 12), HX_CODEC_OBJECT(current, macro_connection));
HX_CODEC_STRUCT(macro_nodes, HX_CODEC_NODE(payload),
	HX_CODEC_ARRAY(parts, hx_codec_node));
HX_CODEC_STRUCT(macro_scalars, HX_CODEC_I8(s8), HX_CODEC_U8(u8),
	HX_CODEC_I16(s16), HX_CODEC_U16(u16), HX_CODEC_I32(s32),
	HX_CODEC_U32(u32), HX_CODEC_I64(s64), HX_CODEC_U64(u64),
	HX_CODEC_FLOAT(real32), HX_CODEC_DOUBLE(real64), HX_CODEC_BOOL(flag));
HX_CODEC_STRUCT(macro_many, HX_CODEC_I32(field_01), HX_CODEC_I32(field_02),
	HX_CODEC_I32(field_03), HX_CODEC_I32(field_04), HX_CODEC_I32(field_05),
	HX_CODEC_I32(field_06), HX_CODEC_I32(field_07), HX_CODEC_I32(field_08),
	HX_CODEC_I32(field_09), HX_CODEC_I32(field_10), HX_CODEC_I32(field_11),
	HX_CODEC_I32(field_12), HX_CODEC_I32(field_13), HX_CODEC_I32(field_14),
	HX_CODEC_I32(field_15), HX_CODEC_I32(field_16), HX_CODEC_I32(field_17),
	HX_CODEC_I32(field_18), HX_CODEC_I32(field_19), HX_CODEC_I32(field_20),
	HX_CODEC_I32(field_21), HX_CODEC_I32(field_22), HX_CODEC_I32(field_23),
	HX_CODEC_I32(field_24), HX_CODEC_I32(field_25), HX_CODEC_I32(field_26),
	HX_CODEC_I32(field_27), HX_CODEC_I32(field_28), HX_CODEC_I32(field_29),
	HX_CODEC_I32(field_30), HX_CODEC_I32(field_31), HX_CODEC_I32(field_32));
#endif
