#ifndef HX_C_PLATFORM_REGEX_PRIVATE_H
#define HX_C_PLATFORM_REGEX_PRIVATE_H

#include "internal.h"
#include "hx_regex.h"
#include "hx_bytes.h"

#define HX_OS_RX_NONE UINT32_MAX
#define HX_OS_RX_INFINITY UINT32_MAX
#define HX_OS_RX_SLOTS 256
#define HX_OS_RX_COMPILE_WORK UINT32_C(4194304)
#define HX_OS_RX_NAMED UINT32_C(0x10000)
#define HX_OS_RX_NEGATE UINT32_C(0x20000)
#define HX_OS_RX_BEHIND UINT32_C(0x40000)
#define HX_OS_RX_LAZY UINT32_C(0x80000)
#define HX_OS_RX_RECURSION UINT32_C(0x1000000)
#define HX_OS_RX_UNICODE_PROPERTY UINT32_C(0x80000000)

enum hx_os_rx_type {
	HX_OS_RX_EMPTY,
	HX_OS_RX_CHAR,
	HX_OS_RX_DOT,
	HX_OS_RX_CLASS,
	HX_OS_RX_CONCAT,
	HX_OS_RX_ALT,
	HX_OS_RX_REPEAT,
	HX_OS_RX_GROUP,
	HX_OS_RX_BACKREF,
	HX_OS_RX_ASSERT,
	HX_OS_RX_ATOMIC,
	HX_OS_RX_CALL,
	HX_OS_RX_CONDITION,
	HX_OS_RX_ANCHOR,
	HX_OS_RX_BOUNDARY,
	HX_OS_RX_NEWLINE,
	HX_OS_RX_GRAPHEME,
	HX_OS_RX_RESET,
	HX_OS_RX_FAIL,
	HX_OS_RX_ACCEPT,
	HX_OS_RX_COMMIT,
	HX_OS_RX_PRUNE,
	HX_OS_RX_SKIP,
	HX_OS_RX_THEN,
	HX_OS_RX_MARK,
	HX_OS_RX_MATCH,
	HX_OS_RX_SPLIT,
	HX_OS_RX_SAVE,
	HX_OS_RX_GROUP_END,
	HX_OS_RX_GUARD,
	HX_OS_RX_ASSERT_END,
	HX_OS_RX_ATOMIC_END,
	HX_OS_RX_JUMP,
	HX_OS_RX_REPEAT_SIMPLE
};

enum hx_os_rx_error {
	HX_OS_RX_BAD_GROUP = 1,
	HX_OS_RX_BAD_CLASS,
	HX_OS_RX_BAD_ESCAPE,
	HX_OS_RX_BAD_REPEAT,
	HX_OS_RX_BAD_REFERENCE,
	HX_OS_RX_UNSUPPORTED,
	HX_OS_RX_DEPTH_LIMIT,
	HX_OS_RX_MEMORY_LIMIT,
	HX_OS_RX_STEP_LIMIT,
	HX_OS_RX_BEHIND_LIMIT,
	HX_OS_RX_BAD_REPLACEMENT
};

enum hx_os_rx_property {
	HX_OS_RX_DIGIT = 1,
	HX_OS_RX_WORD,
	HX_OS_RX_SPACE,
	HX_OS_RX_HSPACE,
	HX_OS_RX_VSPACE,
	HX_OS_RX_ALPHA,
	HX_OS_RX_ALNUM,
	HX_OS_RX_ASCII,
	HX_OS_RX_ANY,
	HX_OS_RX_LOWER,
	HX_OS_RX_UPPER,
	HX_OS_RX_XDIGIT,
	HX_OS_RX_BLANK,
	HX_OS_RX_CNTRL,
	HX_OS_RX_GRAPH,
	HX_OS_RX_PRINT,
	HX_OS_RX_PUNCT,
	HX_OS_RX_CASED,
	HX_OS_RX_CATEGORY = 32
};

typedef struct hx_os_rx_node {
	uint32_t type, left, right, value, flags, minimum, maximum, offset;
} hx_os_rx_node;

typedef struct hx_os_rx_range {
	uint32_t first, last, property, negate;
} hx_os_rx_range;

typedef struct hx_os_rx_class {
	uint32_t first, count, negate;
} hx_os_rx_class;

typedef struct hx_os_rx_instruction {
	uint32_t type, x, y, value, flags, minimum, maximum;
} hx_os_rx_instruction;

typedef struct hx_os_rx_group {
	uint32_t name_offset, name_length, node, entry;
} hx_os_rx_group;

typedef struct hx_os_rx_mark {
	uint32_t offset, length, slot;
} hx_os_rx_mark;

struct hx_os_regex {
	hx_os_context *context;
	hx_string pattern;
	hx_bytes nodes, ranges, classes, code;
	hx_os_regex_options options;
	hx_os_rx_group groups[HX_OS_REGEX_MAX_CAPTURES + 1];
	hx_os_rx_mark marks[64];
	uint32_t group_count, mark_count, slots, entry;
	uint64_t step_limit;
	uint32_t notempty, compile_work;
};

typedef struct hx_os_rx_budget {
	hx_os_regex_limits limits;
	uint64_t steps;
	const char *validated_data;
	size_t validated_size;
	int validated;
} hx_os_rx_budget;

hx_os_status hx_os_i_regex_error(
	hx_os_error *error, hx_os_status status, int code, size_t offset);
const char *hx_os_i_regex_message(int code);
hx_os_status hx_os_i_regex_compile_work(
	hx_os_regex *regex, uint32_t count, size_t offset, hx_os_error *error);
hx_os_status hx_os_i_regex_append(hx_os_regex *regex, hx_bytes *buffer,
	hx_os_bytes_view bytes, size_t offset, hx_os_error *error);
hx_os_status hx_os_i_regex_parse(
	hx_os_regex *regex, uint32_t *root, hx_os_error *error);
hx_os_status hx_os_i_regex_compile(
	hx_os_regex *regex, uint32_t root, hx_os_error *error);
hx_os_status hx_os_i_regex_budget(const hx_os_regex_limits *limits,
	hx_os_rx_budget *out, hx_os_error *error);
hx_os_status hx_os_i_regex_step(
	hx_os_rx_budget *budget, uint64_t steps, hx_os_error *error);
hx_os_status hx_os_i_regex_run(const hx_os_regex *regex, hx_string_view subject,
	size_t start, int full, hx_os_rx_budget *budget,
	hx_os_regex_result *out, hx_os_error *error);
uint32_t hx_os_i_regex_fold(uint32_t point);
int hx_os_i_regex_property(uint32_t point, uint32_t property, int unicode);

#endif
