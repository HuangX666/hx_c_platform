#ifndef HX_C_PLATFORM_PREPROCESS_H
#define HX_C_PLATFORM_PREPROCESS_H

#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_preprocess_define {
	const char *name;
	hx_string_view replacement;
} hx_os_preprocess_define;

/**
 * \brief Synchronous resolver.
 *
 * \details Fill text and optionally replace resolved_name, initially the
 * requested name. Both strings are initialized with the output context; copy
 * into them, never destroy/move their owners or change context. Honor
 * max_bytes; the library also checks returned size and UTF-8. A resolved name
 * is a diagnostic/cycle identifier, 1..4096 UTF-8 bytes, without NUL. Inputs
 * and user are borrowed through the call. Return normally; no longjmp,
 * exceptions or asynchronous retention of these output pointers.
 */
typedef hx_os_status(HX_OS_CALL *hx_os_preprocess_include_fn)(
	const char *parent, const char *requested, size_t max_bytes,
	hx_string *text, hx_string *resolved_name, void *user,
	hx_os_error *error);

enum { HX_OS_PREPROCESS_NO_INCLUDES = 1u };

typedef struct hx_os_preprocess_options {
	uint32_t size, version;
	size_t max_input_bytes, max_total_input_bytes, max_output_bytes;
	size_t max_defines, max_macro_bytes, max_expanded_bytes;
	size_t max_expansions, max_directives;
	size_t max_include_depth, max_expansion_depth, max_condition_depth;
	const hx_os_preprocess_define *defines;
	size_t define_count;
	hx_os_preprocess_include_fn include;
	void *user;
	uint32_t flags;
} hx_os_preprocess_options;

#define HX_OS_PREPROCESS_OPTIONS_INIT                                          \
	{                                                                      \
		sizeof(hx_os_preprocess_options), 1, 16 * 1024 * 1024,         \
			64 * 1024 * 1024, 16 * 1024 * 1024, 256, 1024 * 1024,  \
			64 * 1024 * 1024, 65536, 65536, 16, 32, 64, NULL, 0,   \
			NULL, NULL, 0                                          \
	}

/**
 * \brief Optional caller-owned diagnostic.
 *
 * \details Initialize/destroy exactly like hx_string. Cleared at entry. On
 * failure, source is copied best-effort (empty on OOM), line/column are 1-based
 * when known, description is a static library string borrowed until module
 * unload. Diagnostic failure never replaces the primary status/native error.
 * Diagnostic storage must not alias input, source_name or output. No
 * process-global last-error state is used.
 */
typedef struct hx_os_preprocess_diagnostic {
	hx_string source;
	const char *description;
	size_t line, column;
} hx_os_preprocess_diagnostic;

#define HX_OS_PREPROCESS_DIAGNOSTIC_INIT                                       \
	{                                                                      \
		HX_STRING_INIT, NULL, 0, 0                                     \
	}

HX_OS_API int HX_OS_CALL hx_os_preprocess_available(void);

HX_OS_API hx_os_status HX_OS_CALL hx_os_preprocess_options_init(
	hx_os_preprocess_options *options, size_t capacity);

HX_OS_API void HX_OS_CALL hx_os_preprocess_diagnostic_init(
	hx_os_preprocess_diagnostic *diagnostic, hx_os_context *context);

HX_OS_API void HX_OS_CALL hx_os_preprocess_diagnostic_destroy(
	hx_os_preprocess_diagnostic *diagnostic);

/**
 * \brief Runtime-only UTF-8 preprocessing: object constants, define/undef,
 * conditional branches and quoted includes.
 *
 * \details Never executes source code or changes C/C++ compilation.
 * Strings/comments are not macro-expanded. Definitions are copied for this
 * call; no persistent macro/global state. Output uses out's context, is
 * transactional, and may contain added whitespace to preserve token bounds.
 * Text defaults have no file access; advanced text includes need a resolver.
 * The include flag can disable includes even when a resolver is supplied.
 *
 * Conditions support signed int64 decimal/hex and true/false literals,
 * defined(NAME), unary + - !, comparisons, && || and parentheses; undefined
 * names are zero. This is not a complete C preprocessor: no function macros,
 * #/##, line continuation, compiler includes, expression evaluation outside
 * conditions or host variable access. All condition operands are validated,
 * including unused logical operands. defined/true/false cannot be redefined as
 * macros. BOM is accepted only at the start of a complete source, not a
 * fragment. Unsupported syntax fails explicitly. Recursive macro expansion is
 * an error. Each included source must balance its own conditional directives.
 *
 * Defaults are in the initializer. Hard limits: 64 MiB per byte budget, 4096
 * defines, 1 Mi expansion/directive operations, depth 64/64/64. Root counts as
 * include depth 1. Existing lexer token limits apply too. All retained inputs,
 * macro bytes and generated output are bounded. max_expanded_bytes charges each
 * replacement's bytes every time it is processed. Temporary growth and lexer
 * copies add bounded overhead. Synchronous resolver/I/O has no hard deadline.
 * Exclude reentry into participating output/diagnostic objects.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_preprocess_text(
	const char *input, hx_string *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_preprocess_text_ex(hx_string_view input,
	const char *source_name, const hx_os_preprocess_options *options,
	hx_string *out, hx_os_preprocess_diagnostic *diagnostic,
	hx_os_error *error);

/**
 * \brief UTF-8 file entry, accepting/removing BOM.
 *
 * \details By default resolves quoted relative includes against the including
 * file's directory. Absolute include names and dot-dot components are rejected;
 * filesystem links are followed. These are bounded trusted-file operations, not
 * a filesystem security sandbox. Supply a resolver or NO_INCLUDES for a
 * different access policy. Requires FILE_UTILS; the text entry needs only
 * PREPROCESSOR, LEXER and PARAMETERS.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_preprocess_file(
	const char *path, hx_string *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_preprocess_file_ex(const char *path,
	const hx_os_preprocess_options *options, hx_string *out,
	hx_os_preprocess_diagnostic *diagnostic, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
