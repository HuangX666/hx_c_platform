#ifndef HX_C_PLATFORM_LEXER_H
#define HX_C_PLATFORM_LEXER_H
#include "hx_string.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct hx_os_lexer hx_os_lexer;
enum {
	HX_OS_TOKEN_END,
	HX_OS_TOKEN_IDENTIFIER,
	HX_OS_TOKEN_NUMBER,
	HX_OS_TOKEN_STRING,
	HX_OS_TOKEN_PUNCTUATION,
	HX_OS_TOKEN_NEWLINE,
	HX_OS_TOKEN_SPACE,
	HX_OS_TOKEN_COMMENT
};
enum {
	HX_OS_LEXER_KEEP_SPACE = 1,
	HX_OS_LEXER_KEEP_COMMENTS = 2,
	HX_OS_LEXER_SKIP_NEWLINES = 4,
	HX_OS_LEXER_HASH_COMMENTS = 8,
	HX_OS_LEXER_C_COMMENTS = 16
};
typedef struct hx_os_token {
	hx_string_view text;
	size_t offset;
	size_t line;
	size_t column;
	int kind;
} hx_os_token;
typedef struct hx_os_lexer_options {
	uint32_t size;
	uint32_t version;
	size_t max_input_bytes;
	size_t max_token_bytes;
	size_t max_tokens;
	uint32_t flags;
} hx_os_lexer_options;
#define HX_OS_LEXER_OPTIONS_INIT                                               \
	{                                                                      \
		sizeof(hx_os_lexer_options), 1, 16 * 1024 * 1024, 1024 * 1024, \
			1024 * 1024,                                           \
			HX_OS_LEXER_HASH_COMMENTS | HX_OS_LEXER_C_COMMENTS     \
	}
HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_options_init(
	hx_os_lexer_options *options, size_t capacity);
/* Copies strict UTF-8, accepts a leading BOM, rejects embedded NUL.
 * Tokens borrow immutable input until destroy; next/peek/reset need external
 * serialization. Lines/columns are 1-based Unicode codepoints (tab=1).
 * CRLF counts as one newline. Names accept ASCII letters/underscore followed
 * by letters/digits/underscore, plus non-ASCII codepoints (not Unicode ID
 * validation). Numbers: decimal/fraction/exponent and 0x hexadecimal integer;
 * sign is a punctuation token. No preprocessor, C parser or numeric coercion.
 * Quotes support JSON escapes plus \' in either quote style. Block comments
 * do not nest. Default skips spaces/comments and returns newline tokens.
 * Limits also bound skipped tokens; hard input/token ceiling 64 MiB, token
 * count 16 Mi. Lexical failures are sticky until reset and preserve *out.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_create(hx_os_context *context,
	hx_string_view input, hx_os_lexer **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_create_with_options(
	hx_os_context *context, hx_string_view input,
	const hx_os_lexer_options *options, hx_os_lexer **out,
	hx_os_error *error);
/* EOF sets an empty END token at the end position and returns HX_OS_EOF. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_next(
	hx_os_lexer *lexer, hx_os_token *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_peek(
	const hx_os_lexer *lexer, hx_os_token *out, hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_lexer_reset(hx_os_lexer *lexer);
HX_OS_API void HX_OS_CALL hx_os_lexer_destroy(hx_os_lexer **lexer);
/* Reports current position, including the start of a malformed token. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_position(
	const hx_os_lexer *lexer, size_t *offset, size_t *line, size_t *column);
/* STRING removes quotes/decodes escapes, other tokens copy raw text.
 * Output uses its own context and is unchanged on failure.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_token_string(
	const hx_os_token *token, hx_string *out, hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
