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

/**
 * \brief Copies strict UTF-8, accepts a leading BOM, rejects embedded NUL.
 *
 * \details Tokens borrow immutable input until destroy; next/peek/reset need
 * external serialization. Lines/columns are 1-based Unicode codepoints (tab=1).
 * CRLF counts as one newline. Names accept ASCII letters/underscore followed by
 * letters/digits/underscore, plus non-ASCII codepoints (not Unicode ID
 * validation). Numbers: decimal/fraction/exponent and 0x hexadecimal integer;
 * sign is a punctuation token. No preprocessor, C parser or numeric coercion.
 * Quotes support JSON escapes plus \' in either quote style. Block comments do
 * not nest. Default skips spaces/comments and returns newline tokens. Limits
 * also bound skipped tokens; hard input/token ceiling 64 MiB, token count 16
 * Mi. Lexical failures are sticky until reset and preserve *out.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_create(hx_os_context *context,
	hx_string_view input, hx_os_lexer **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_create_with_options(
	hx_os_context *context, hx_string_view input,
	const hx_os_lexer_options *options, hx_os_lexer **out,
	hx_os_error *error);

/**
 * \brief EOF sets an empty END token at the end position and returns HX_OS_EOF.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_next(
	hx_os_lexer *lexer, hx_os_token *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_peek(
	const hx_os_lexer *lexer, hx_os_token *out, hx_os_error *error);

HX_OS_API void HX_OS_CALL hx_os_lexer_reset(hx_os_lexer *lexer);

HX_OS_API void HX_OS_CALL hx_os_lexer_destroy(hx_os_lexer **lexer);

/**
 * \brief Reports current position, including the start of a malformed token.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_position(
	const hx_os_lexer *lexer, size_t *offset, size_t *line, size_t *column);

/**
 * \brief STRING removes quotes/decodes escapes, other tokens copy raw text.
 *
 * \details Output uses its own context and is unchanged on failure.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_token_string(
	const hx_os_token *token, hx_string *out, hx_os_error *error);

/**
 * \brief Runtime parsing helpers respect the lexer's whitespace/comment
 * options.
 *
 * \details Comparisons use exact raw token bytes, including quotes on STRING.
 * token_is allocates nothing; NULL token/text is false.
 */
HX_OS_API int HX_OS_CALL hx_os_token_is(
	const hx_os_token *token, const char *text);

/**
 * \brief Accept consumes one matching token; otherwise the cursor stays
 * unchanged.
 *
 * \details Expected text is nonempty. EOF is successful no-match; failure
 * preserves matched. Expect reports SYNTAX on mismatch, TRUNCATED_INPUT on EOF.
 * Optional out borrows the lexer, unchanged on failure. expect_kind(END)
 * succeeds at EOF, returning END without further progress.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_accept(
	hx_os_lexer *lexer, const char *text, int *matched, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_expect(hx_os_lexer *lexer,
	const char *text, hx_os_token *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_expect_kind(
	hx_os_lexer *lexer, int kind, hx_os_token *out, hx_os_error *error);

/**
 * \brief Decode and consume the next STRING token using out's context.
 *
 * \details Failure preserves output/cursor. Allocators must not reenter, mutate
 * or destroy the participating lexer/string during this operation.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_read_string(
	hx_os_lexer *lexer, hx_string *out, hx_os_error *error);

/**
 * \brief Consume a complete (), [] or {} group, including its opening token.
 *
 * \details Nested types must match; brackets inside strings/comments do not
 * count. Default depth limit 64, explicit limit 1..256. Fixed bounded stack, no
 * allocation. Failure can consume a prefix: unmatched closing/excessive opening
 * tokens remain next; a missing close is TRUNCATED_INPUT. Reset/recreate the
 * lexer to restart a failed group skip from the beginning.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_skip_group(
	hx_os_lexer *lexer, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_skip_group_with_limit(
	hx_os_lexer *lexer, size_t max_depth, hx_os_error *error);

/**
 * \brief Consume through the next exact raw text, including that token.
 *
 * \details Flat token scan, not bracket-aware recovery. Missing target returns
 * EOF; lexical/limit failures may have consumed a prefix. Empty expected text
 * is invalid.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_lexer_skip_until(
	hx_os_lexer *lexer, const char *text, hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
