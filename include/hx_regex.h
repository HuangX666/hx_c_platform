#ifndef HX_C_PLATFORM_REGEX_H
#define HX_C_PLATFORM_REGEX_H

#include "hx_string.h"
#include "hx_os_coord.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_regex hx_os_regex;

enum {
	HX_OS_REGEX_CASELESS = 1,
	HX_OS_REGEX_MULTILINE = 2,
	HX_OS_REGEX_DOTALL = 4,
	HX_OS_REGEX_EXTENDED = 8,
	HX_OS_REGEX_UCP = 16,
	HX_OS_REGEX_UNGREEDY = 32,
	HX_OS_REGEX_NO_AUTO_CAPTURE = 64
};

#define HX_OS_REGEX_MAX_CAPTURES 64

typedef struct hx_os_regex_options {
	uint32_t size, version, flags, max_pattern_bytes;
	size_t max_program_bytes;
	uint32_t max_nesting, max_repeat, max_lookbehind;
} hx_os_regex_options;

#define HX_OS_REGEX_OPTIONS_INIT                                               \
	{                                                                      \
		sizeof(hx_os_regex_options), 1, 0, 32768, 4 * 1024 * 1024, 64, \
			1000, 255                                              \
	}

typedef struct hx_os_regex_limits {
	uint32_t size, version;
	uint64_t max_steps;
	size_t max_stack_bytes, max_subject_bytes;
	uint32_t max_depth, max_results;
	hx_os_deadline deadline;
	hx_os_cancel_token *cancel;
} hx_os_regex_limits;

#define HX_OS_REGEX_LIMITS_INIT                                                \
	{                                                                      \
		sizeof(hx_os_regex_limits), 1, UINT64_C(10000000),             \
			8 * 1024 * 1024, 16 * 1024 * 1024, 64, 65536,          \
			HX_OS_DEADLINE_INFINITE, NULL                          \
	}

typedef struct hx_os_regex_span {
	size_t offset, size;
} hx_os_regex_span;

/* Capture 0 is the entire match; absent groups have offset=SIZE_MAX.
 * Spans borrow the original subject. Use only after successful execution.
 */
typedef struct hx_os_regex_result {
	hx_os_regex_span captures[HX_OS_REGEX_MAX_CAPTURES + 1];
	uint32_t count;
} hx_os_regex_result;

/* Self-developed C99 engine; no std::regex, PCRE2 code, JIT or workers.
 * Compiled objects are immutable/shareable; exclude destruction from calls.
 * Counted pattern/text spans allow NUL and require strict UTF-8. Newline=LF;
 * d/w/s classes are ASCII unless UCP is selected. Unicode classification,
 * caseless matching and graphemes require the optional Unicode provider.
 * Compile errors carry their byte offset in error.cause_code.
 * Program budget includes instructions and class/range data, but excludes
 * bounded temporary parser storage and allocation capacity overhead.
 * Compilation has a fixed 4,194,304-work-unit ceiling, separate from runtime
 * limits; exceeding it returns LIMIT. Reused group widths are memoized.
 */
HX_OS_API int HX_OS_CALL hx_os_regex_available(void);
HX_OS_API hx_os_status HX_OS_CALL hx_os_regex_compile(hx_os_context *context,
	hx_string_view pattern, const hx_os_regex_options *options,
	hx_os_regex **out, hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_regex_destroy(hx_os_regex **regex);
HX_OS_API size_t HX_OS_CALL hx_os_regex_capture_count(const hx_os_regex *regex);
HX_OS_API hx_os_status HX_OS_CALL hx_os_regex_capture_index(
	const hx_os_regex *regex, const char *name, size_t *index,
	hx_os_error *error);

/* Leftmost, ordered search. NOT_FOUND is completed no-match; LIMIT,
 * TIMEOUT and CANCELLED mean incomplete. Failure preserves out. start must
 * be a UTF-8 boundary <=subject.size. Cooperative deadline/cancellation,
 * not a hard real-time guarantee. Limits cover the entire operation.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_regex_find(const hx_os_regex *regex,
	hx_string_view subject, size_t start, hx_os_regex_result *out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_regex_find_with_limits(
	const hx_os_regex *regex, hx_string_view subject, size_t start,
	const hx_os_regex_limits *limits, hx_os_regex_result *out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_regex_full_match(
	const hx_os_regex *regex, hx_string_view subject,
	const hx_os_regex_limits *limits, int *matched, hx_os_error *error);
/* Compile + full match + release, using defaults. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_regex_matches(const char *pattern,
	hx_string_view subject, int *matched, hx_os_error *error);

/* Replace all: $0..$64, dollar-braced names, $$; unknown syntax is invalid.
 * Uses out's allocator; failure preserves out. Empty matches advance a code
 * point after replacement. Results <=max_subject_bytes, at most 64 MiB.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_regex_replace(const hx_os_regex *regex,
	hx_string_view subject, hx_string_view replacement,
	const hx_os_regex_limits *limits, hx_string *out, hx_os_error *error);
typedef hx_os_status(HX_OS_CALL *hx_os_regex_split_fn)(
	hx_string_view part, size_t offset, void *user);
/* Includes empty parts, excludes delimiter captures. Callback borrows input
 * synchronously, cannot throw/longjmp or destroy the regex. completed counts
 * successful deliveries; failure preserves that prefix.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_regex_split(const hx_os_regex *regex,
	hx_string_view subject, const hx_os_regex_limits *limits,
	hx_os_regex_split_fn callback, void *user, size_t *completed,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
