#ifndef HX_C_PLATFORM_PREPROCESS_PRIVATE_H
#define HX_C_PLATFORM_PREPROCESS_PRIVATE_H

#include "internal.h"
#include "hx_os_preprocess.h"
#include "hx_os_lexer.h"
#include "hx_os_params.h"

#define HX_OS_PP_SOURCE_BOM UINT32_C(0x80000000)

#define HX_OS_HAVE_PREPROCESSOR                                                \
	(HX_OS_ENABLE_PREPROCESSOR && HX_OS_ENABLE_LEXER &&                    \
		HX_OS_ENABLE_PARAMETERS)

typedef struct hx_os_pp_condition {
	unsigned char parent_active, active, taken, had_else;
} hx_os_pp_condition;

typedef struct hx_os_pp_source {
	hx_string name;
	hx_os_lexer *lexer;
	hx_os_pp_condition conditions[64];
	size_t condition_count;
	int line_start;
} hx_os_pp_source;

typedef struct hx_os_pp_macro_frame {
	hx_os_lexer *lexer;
	char name[128];
} hx_os_pp_macro_frame;

typedef struct hx_os_pp_state {
	hx_os_preprocess_options options;
	hx_os_context *context;
	hx_os_params *macros;
	hx_string output;
	hx_os_preprocess_diagnostic *diagnostic;
	hx_os_pp_source *sources;
	hx_os_pp_macro_frame *expansion;
	size_t source_count, source_capacity, expansion_capacity;
	size_t total_input, expanded_bytes, expansions, directives;
	const char *root_name;
	const hx_os_token *origin;
} hx_os_pp_state;

hx_os_status hx_os_i_pp_fail(hx_os_pp_state *state, hx_os_status status,
	const char *description, const hx_os_token *token, hx_os_error *error);

hx_os_status hx_os_i_pp_lexer(hx_os_pp_state *state, hx_string_view input,
	uint32_t flags, hx_os_lexer **out, hx_os_error *error);

hx_os_status hx_os_i_pp_macro(hx_os_pp_state *state, const hx_os_token *token,
	char name[128], hx_string_view *replacement, hx_os_error *error);

hx_os_status hx_os_i_pp_charge(hx_os_pp_state *state, size_t bytes,
	const hx_os_token *token, hx_os_error *error);

hx_os_status hx_os_i_pp_evaluate(hx_os_pp_state *state, hx_string_view input,
	int64_t *value, hx_os_error *error);

hx_os_status hx_os_i_pp_run(hx_string_view input, const char *source_name,
	const hx_os_preprocess_options *options, hx_string *out,
	hx_os_preprocess_diagnostic *diagnostic, hx_os_error *error);

#endif
