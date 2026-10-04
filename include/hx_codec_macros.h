#ifndef HX_C_PLATFORM_CODEC_MACROS_H
#define HX_C_PLATFORM_CODEC_MACROS_H

#include "hx_codec.h"

/* Define a value struct and its existing codec metadata in one declaration:
 * HX_CODEC_STRUCT(connection, HX_CODEC_STRING(host), HX_CODEC_I32(port));
 * Generates connection, connection_fields and connection_schema. Use at file
 * or C++ namespace scope, in the same order as ordinary member declarations.
 * Supports 1..32 fields; no trailing comma. No registration, generated source,
 * packing, new allocator, or runtime initialization. All layouts are computed
 * by the target compiler. Existing public structs should keep their existing
 * declarations and use hx_codec.h's explicit mapping macros instead.
 *
 * ARRAY/FIXED element types are identifiers: one of the integer-width types,
 * float, double, hx_string_view, hx_codec_bool, hx_codec_node, or a struct
 * previously defined by HX_CODEC_STRUCT in this scope. Use the explicit API
 * for other type aliases, qualified C++ names, or larger mappings.
 * Containers and strings retain the ordinary codec borrowing/ownership rules.
 */
typedef int hx_codec_bool;
typedef const hx_os_config_node *hx_codec_node;

#define HX_CODEC_I_CAT_RAW(a, b) a##b
#define HX_CODEC_I_CAT(a, b) HX_CODEC_I_CAT_RAW(a, b)
#define HX_CODEC_I_EXPAND(...) __VA_ARGS__

/* Per-type array descriptors may be unused when only the schema is needed.
 * Annotate only the generated readonly metadata.
 */
#if defined(__GNUC__) || defined(__clang__)
#define HX_CODEC_I_UNUSED __attribute__((unused))
#else
#define HX_CODEC_I_UNUSED
#endif
#define HX_CODEC_I_PRIMITIVE(type, kind)                                       \
	static const hx_os_codec_type HX_CODEC_I_CAT(hx_codec_i_type_, type)   \
		HX_CODEC_I_UNUSED = HX_OS_CODEC_TYPE(type, kind)
HX_CODEC_I_PRIMITIVE(int8_t, HX_OS_CODEC_I8);
HX_CODEC_I_PRIMITIVE(uint8_t, HX_OS_CODEC_U8);
HX_CODEC_I_PRIMITIVE(int16_t, HX_OS_CODEC_I16);
HX_CODEC_I_PRIMITIVE(uint16_t, HX_OS_CODEC_U16);
HX_CODEC_I_PRIMITIVE(int32_t, HX_OS_CODEC_I32);
HX_CODEC_I_PRIMITIVE(uint32_t, HX_OS_CODEC_U32);
HX_CODEC_I_PRIMITIVE(int64_t, HX_OS_CODEC_I64);
HX_CODEC_I_PRIMITIVE(uint64_t, HX_OS_CODEC_U64);
HX_CODEC_I_PRIMITIVE(float, HX_OS_CODEC_FLOAT);
HX_CODEC_I_PRIMITIVE(double, HX_OS_CODEC_DOUBLE);
HX_CODEC_I_PRIMITIVE(hx_string_view, HX_OS_CODEC_STRING);
HX_CODEC_I_PRIMITIVE(hx_codec_bool, HX_OS_CODEC_BOOL);
HX_CODEC_I_PRIMITIVE(hx_codec_node, HX_OS_CODEC_NODE);
#undef HX_CODEC_I_PRIMITIVE

/* Field descriptions are parenthesized tuples until the struct expands them.
 * Separate expansion steps also support MSVC's traditional preprocessor.
 */
#define HX_CODEC_I_SCALAR(type, member, kind)                                  \
	(type, member, , NULL, NULL, 0, kind, #member, 0)
#define HX_CODEC_I8(member) HX_CODEC_I_SCALAR(int8_t, member, HX_OS_CODEC_I8)
#define HX_CODEC_U8(member) HX_CODEC_I_SCALAR(uint8_t, member, HX_OS_CODEC_U8)
#define HX_CODEC_I16(member) HX_CODEC_I_SCALAR(int16_t, member, HX_OS_CODEC_I16)
#define HX_CODEC_U16(member)                                                   \
	HX_CODEC_I_SCALAR(uint16_t, member, HX_OS_CODEC_U16)
#define HX_CODEC_I32(member) HX_CODEC_I_SCALAR(int32_t, member, HX_OS_CODEC_I32)
#define HX_CODEC_U32(member)                                                   \
	HX_CODEC_I_SCALAR(uint32_t, member, HX_OS_CODEC_U32)
#define HX_CODEC_I64(member) HX_CODEC_I_SCALAR(int64_t, member, HX_OS_CODEC_I64)
#define HX_CODEC_U64(member)                                                   \
	HX_CODEC_I_SCALAR(uint64_t, member, HX_OS_CODEC_U64)
#define HX_CODEC_FLOAT(member)                                                 \
	HX_CODEC_I_SCALAR(float, member, HX_OS_CODEC_FLOAT)
#define HX_CODEC_DOUBLE(member)                                                \
	HX_CODEC_I_SCALAR(double, member, HX_OS_CODEC_DOUBLE)
#define HX_CODEC_STRING(member)                                                \
	HX_CODEC_I_SCALAR(hx_string_view, member, HX_OS_CODEC_STRING)
#define HX_CODEC_BOOL(member)                                                  \
	HX_CODEC_I_SCALAR(hx_codec_bool, member, HX_OS_CODEC_BOOL)
#define HX_CODEC_NODE(member)                                                  \
	HX_CODEC_I_SCALAR(hx_codec_node, member, HX_OS_CODEC_NODE)
#define HX_CODEC_BUFFER(member, capacity)                                      \
	(char, member, [capacity], NULL, NULL, 0, HX_OS_CODEC_BUFFER, #member, \
		0)
#define HX_CODEC_OBJECT(member, type)                                          \
	(type, member, , &HX_CODEC_I_CAT(type, _schema), NULL, 0,              \
		HX_OS_CODEC_OBJECT, #member, 0)
#define HX_CODEC_ARRAY(member, element_type)                                   \
	(hx_os_codec_array, member, , NULL,                                    \
		&HX_CODEC_I_CAT(hx_codec_i_type_, element_type), 0,            \
		HX_OS_CODEC_ARRAY, #member, 0)
#define HX_CODEC_FIXED(member, element_type, count)                            \
	(element_type, member, [count], NULL,                                  \
		&HX_CODEC_I_CAT(hx_codec_i_type_, element_type), count,        \
		HX_OS_CODEC_FIXED_ARRAY, #member, 0)

#define HX_CODEC_I_RENAME_CALL(m, args) HX_CODEC_I_EXPAND(m args)
#define HX_CODEC_I_RENAME(name, type, member, suffix, schema, element, count,  \
	kind, old_name, flags)                                                 \
	(type, member, suffix, schema, element, count, kind, name, flags)
#define HX_CODEC_NAME(name, field)                                             \
	HX_CODEC_I_RENAME_CALL(                                                \
		HX_CODEC_I_RENAME, (name, HX_CODEC_I_EXPAND field))
#define HX_CODEC_I_FLAGS_CALL(m, args) HX_CODEC_I_EXPAND(m args)
#define HX_CODEC_I_FLAGS(                                                      \
	add, type, member, suffix, schema, element, count, kind, name, flags)  \
	(type, member, suffix, schema, element, count, kind, name,             \
		((flags) | (add)))
#define HX_CODEC_OPTIONAL(field)                                               \
	HX_CODEC_I_FLAGS_CALL(HX_CODEC_I_FLAGS,                                \
		(HX_OS_CODEC_OPTIONAL, HX_CODEC_I_EXPAND field))
#define HX_CODEC_XML_ATTRIBUTE(field)                                          \
	HX_CODEC_I_FLAGS_CALL(HX_CODEC_I_FLAGS,                                \
		(HX_OS_CODEC_XML_ATTRIBUTE, HX_CODEC_I_EXPAND field))

#define HX_CODEC_I_DECL_CALL(m, args) HX_CODEC_I_EXPAND(m args)
#define HX_CODEC_I_DECL(                                                       \
	type, member, suffix, schema, element, count, kind, name, flags)       \
	type member suffix;
#define HX_CODEC_I_DECLARE(object, field)                                      \
	HX_CODEC_I_DECL_CALL(HX_CODEC_I_DECL, field)
#define HX_CODEC_I_FIELD_CALL(m, args) HX_CODEC_I_EXPAND(m args)
#define HX_CODEC_I_FIELD(object, type, member, suffix, schema, element, count, \
	kind, name, flags)                                                     \
	{name, {schema, element, sizeof(((object *)0)->member), count, kind},  \
		offsetof(object, member), flags},
#define HX_CODEC_I_METADATA(object, field)                                     \
	HX_CODEC_I_FIELD_CALL(                                                 \
		HX_CODEC_I_FIELD, (object, HX_CODEC_I_EXPAND field))

#define HX_CODEC_I_COUNT_CALL(m, args) HX_CODEC_I_EXPAND(m args)
#define HX_CODEC_I_COUNT(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12,    \
	_13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26,  \
	_27, _28, _29, _30, _31, _32, count, ...)                              \
	count
#define HX_CODEC_I_LENGTH(...)                                                 \
	HX_CODEC_I_COUNT_CALL(HX_CODEC_I_COUNT,                                \
		(__VA_ARGS__, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21,  \
			20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7,   \
			6, 5, 4, 3, 2, 1, 0))
#define HX_CODEC_I_MAP_CALL(m, args) HX_CODEC_I_EXPAND(m args)
#define HX_CODEC_I_MAP(m, object, ...)                                         \
	HX_CODEC_I_MAP_CALL(HX_CODEC_I_CAT(HX_CODEC_I_MAP_,                    \
				    HX_CODEC_I_LENGTH(__VA_ARGS__)),           \
		(m, object, __VA_ARGS__))
#define HX_CODEC_I_MAP_1(m, object, field) m(object, field)
#define HX_CODEC_I_NEXT_1(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_1 args)
#define HX_CODEC_I_MAP_2(m, object, field, ...)                                \
	m(object, field) HX_CODEC_I_NEXT_1((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_2(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_2 args)
#define HX_CODEC_I_MAP_3(m, object, field, ...)                                \
	m(object, field) HX_CODEC_I_NEXT_2((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_3(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_3 args)
#define HX_CODEC_I_MAP_4(m, object, field, ...)                                \
	m(object, field) HX_CODEC_I_NEXT_3((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_4(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_4 args)
#define HX_CODEC_I_MAP_5(m, object, field, ...)                                \
	m(object, field) HX_CODEC_I_NEXT_4((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_5(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_5 args)
#define HX_CODEC_I_MAP_6(m, object, field, ...)                                \
	m(object, field) HX_CODEC_I_NEXT_5((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_6(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_6 args)
#define HX_CODEC_I_MAP_7(m, object, field, ...)                                \
	m(object, field) HX_CODEC_I_NEXT_6((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_7(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_7 args)
#define HX_CODEC_I_MAP_8(m, object, field, ...)                                \
	m(object, field) HX_CODEC_I_NEXT_7((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_8(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_8 args)
#define HX_CODEC_I_MAP_9(m, object, field, ...)                                \
	m(object, field) HX_CODEC_I_NEXT_8((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_9(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_9 args)
#define HX_CODEC_I_MAP_10(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_9((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_10(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_10 args)
#define HX_CODEC_I_MAP_11(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_10((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_11(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_11 args)
#define HX_CODEC_I_MAP_12(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_11((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_12(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_12 args)
#define HX_CODEC_I_MAP_13(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_12((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_13(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_13 args)
#define HX_CODEC_I_MAP_14(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_13((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_14(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_14 args)
#define HX_CODEC_I_MAP_15(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_14((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_15(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_15 args)
#define HX_CODEC_I_MAP_16(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_15((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_16(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_16 args)
#define HX_CODEC_I_MAP_17(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_16((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_17(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_17 args)
#define HX_CODEC_I_MAP_18(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_17((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_18(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_18 args)
#define HX_CODEC_I_MAP_19(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_18((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_19(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_19 args)
#define HX_CODEC_I_MAP_20(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_19((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_20(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_20 args)
#define HX_CODEC_I_MAP_21(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_20((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_21(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_21 args)
#define HX_CODEC_I_MAP_22(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_21((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_22(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_22 args)
#define HX_CODEC_I_MAP_23(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_22((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_23(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_23 args)
#define HX_CODEC_I_MAP_24(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_23((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_24(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_24 args)
#define HX_CODEC_I_MAP_25(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_24((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_25(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_25 args)
#define HX_CODEC_I_MAP_26(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_25((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_26(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_26 args)
#define HX_CODEC_I_MAP_27(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_26((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_27(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_27 args)
#define HX_CODEC_I_MAP_28(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_27((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_28(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_28 args)
#define HX_CODEC_I_MAP_29(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_28((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_29(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_29 args)
#define HX_CODEC_I_MAP_30(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_29((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_30(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_30 args)
#define HX_CODEC_I_MAP_31(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_30((m, object, __VA_ARGS__))
#define HX_CODEC_I_NEXT_31(args) HX_CODEC_I_EXPAND(HX_CODEC_I_MAP_31 args)
#define HX_CODEC_I_MAP_32(m, object, field, ...)                               \
	m(object, field) HX_CODEC_I_NEXT_31((m, object, __VA_ARGS__))

#define HX_CODEC_STRUCT_NAMED(type, xml_name, ...)                             \
	typedef struct type {                                                  \
		HX_CODEC_I_MAP(HX_CODEC_I_DECLARE, type, __VA_ARGS__)          \
	} type;                                                                \
	static const hx_os_codec_field HX_CODEC_I_CAT(                         \
		type, _fields)[] HX_CODEC_I_UNUSED = {                         \
		HX_CODEC_I_MAP(HX_CODEC_I_METADATA, type, __VA_ARGS__)};       \
	static const hx_os_codec_schema HX_CODEC_I_CAT(type, _schema)          \
		HX_CODEC_I_UNUSED = HX_OS_CODEC_SCHEMA(                        \
			type, HX_CODEC_I_CAT(type, _fields), xml_name);        \
	static const hx_os_codec_type HX_CODEC_I_CAT(hx_codec_i_type_, type)   \
		HX_CODEC_I_UNUSED = {&HX_CODEC_I_CAT(type, _schema), NULL,     \
			sizeof(type), 0, HX_OS_CODEC_OBJECT}
#define HX_CODEC_STRUCT(type, ...)                                             \
	HX_CODEC_STRUCT_NAMED(type, #type, __VA_ARGS__)

#endif
