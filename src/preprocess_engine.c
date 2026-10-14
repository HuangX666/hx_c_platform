#include "preprocess_private.h"

hx_os_status hx_os_i_pp_fail(hx_os_pp_state *state, hx_os_status status,
	const char *description, const hx_os_token *token, hx_os_error *error)
{
	if (state->diagnostic && !state->diagnostic->description) {
		const char *source = state->source_count
			? hx_string_c_str(
				  &state->sources[state->source_count - 1].name)
			: state->root_name;

		if (state->origin)
			token = state->origin;
		state->diagnostic->description = description;
		state->diagnostic->line = token ? token->line : 0;
		state->diagnostic->column = token ? token->column : 0;
		if (source)
			(void)hx_string_assign_cstr(
				&state->diagnostic->source, source, NULL);
	}
	if (!error || error->status != status)
		return hx_os_i_error(error, status, 0, 0);

	return status;
}

hx_os_status hx_os_i_pp_lexer(hx_os_pp_state *state, hx_string_view input,
	uint32_t flags, hx_os_lexer **out, hx_os_error *error)
{
	hx_os_lexer_options options = HX_OS_LEXER_OPTIONS_INIT;

	if (!(flags & HX_OS_PP_SOURCE_BOM) && input.size >= 3 && input.data &&
		!memcmp(input.data, "\xef\xbb\xbf", 3))
		return hx_os_i_pp_fail(state, HX_OS_SYNTAX,
			"BOM is only permitted at the start of a source", NULL,
			error);
	options.max_input_bytes = 64u * 1024u * 1024u;
	options.flags = flags & ~HX_OS_PP_SOURCE_BOM;

	return hx_os_lexer_create_with_options(
		state->context, input, &options, out, error);
}

hx_os_status hx_os_i_pp_macro(hx_os_pp_state *state, const hx_os_token *token,
	char name[128], hx_string_view *replacement, hx_os_error *error)
{
	if (token->kind != HX_OS_TOKEN_IDENTIFIER || token->text.size > 127)
		return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
	memcpy(name, token->text.data, token->text.size);
	name[token->text.size] = 0;

	return hx_os_params_get_string(state->macros, name, replacement, error);
}

hx_os_status hx_os_i_pp_charge(hx_os_pp_state *state, size_t bytes,
	const hx_os_token *token, hx_os_error *error)
{
	if (state->expansions == state->options.max_expansions ||
		bytes > state->options.max_expanded_bytes -
				state->expanded_bytes)
		return hx_os_i_pp_fail(state, HX_OS_LIMIT,
			"macro expansion budget exhausted", token, error);
	++state->expansions;
	state->expanded_bytes += bytes;

	return HX_OS_OK;
}

static hx_os_status hx_os_pp_append(hx_os_pp_state *state, hx_string *out,
	hx_string_view text, hx_os_error *error)
{
	hx_os_status status;

	if (text.size > state->options.max_output_bytes - out->size)
		return hx_os_i_pp_fail(state, HX_OS_LIMIT,
			"preprocessed output exceeds its limit", NULL, error);
	status = hx_string_append(out, text, error);
	if (status != HX_OS_OK)
		return hx_os_i_pp_fail(state, status,
			"cannot allocate preprocessed output", NULL, error);

	return status;
}

static hx_os_status hx_os_pp_separator(hx_os_pp_state *state, hx_string *out,
	char separator, hx_os_error *error)
{
	hx_string_view text = {&separator, 1};

	return hx_os_pp_append(state, out, text, error);
}

/**
 * \brief Called only when a push would exceed capacity.
 *
 * \details Ownership of byte-relocatable entries transfers on success; failure
 * leaves the old array intact.
 */
static void *hx_os_pp_grow(hx_os_pp_state *state, void *old, size_t *capacity,
	size_t maximum, size_t item_bytes)
{
	size_t grown = *capacity ? *capacity * 2 : 2;
	void *memory;

	if (grown > maximum)
		grown = maximum;
	memory = hx_os_i_alloc(state->context, grown * item_bytes, 0);
	if (!memory)
		return NULL;
	if (*capacity)
		memcpy(memory, old, *capacity * item_bytes);
	hx_os_memory_free(old);
	*capacity = grown;

	return memory;
}

static void hx_os_pp_pop_source(hx_os_pp_state *state)
{
	hx_os_pp_source *source = &state->sources[--state->source_count];

	hx_os_lexer_destroy(&source->lexer);
	hx_string_destroy(&source->name);
}

static hx_os_status hx_os_pp_push_source(hx_os_pp_state *state,
	const char *name, hx_string_view text, hx_os_error *error)
{
	hx_os_pp_source *source;
	hx_os_status status;
	size_t i;
	const hx_os_token *origin = state->origin;

	if (state->source_count == state->options.max_include_depth ||
		text.size > state->options.max_input_bytes ||
		text.size > state->options.max_total_input_bytes -
				state->total_input)
		return hx_os_i_pp_fail(state, HX_OS_LIMIT,
			"include depth or input budget exhausted", NULL, error);
	for (i = 0; i < state->source_count; ++i)
		if (!strcmp(state->sources[i].name.data, name))
			return hx_os_i_pp_fail(state, HX_OS_SYNTAX,
				"recursive include identifier", NULL, error);
	if (state->source_count == state->source_capacity) {
		void *grown = hx_os_pp_grow(state, state->sources,
			&state->source_capacity,
			state->options.max_include_depth, sizeof(*source));

		if (!grown)
			return hx_os_i_pp_fail(state, HX_OS_NO_MEMORY,
				"cannot grow include stack", NULL, error);
		state->sources = grown;
	}
	source = &state->sources[state->source_count];
	memset(source, 0, sizeof(*source));
	hx_string_init(&source->name, state->context);
	status = hx_string_assign_cstr(&source->name, name, error);
	if (status != HX_OS_OK) {
		hx_string_destroy(&source->name);

		return hx_os_i_pp_fail(state, status,
			"cannot copy source identifier", NULL, error);
	}
	++state->source_count;
	state->origin = NULL;
	status = hx_os_i_pp_lexer(state, text,
		HX_OS_LEXER_KEEP_SPACE | HX_OS_LEXER_KEEP_COMMENTS |
			HX_OS_LEXER_C_COMMENTS | HX_OS_PP_SOURCE_BOM,
		&source->lexer, error);
	if (status != HX_OS_OK) {
		(void)hx_os_i_pp_fail(state, status,
			"invalid or oversized source text", NULL, error);
		hx_os_pp_pop_source(state);
	}
	else {
		source->line_start = 1;
		state->total_input += text.size;
	}
	state->origin = origin;

	return status;
}

static hx_os_status hx_os_pp_push_macro(hx_os_pp_state *state, size_t depth,
	const char *name, hx_string_view text, hx_os_error *error)
{
	hx_os_pp_macro_frame *frame;
	hx_os_status status;
	size_t i;

	if (depth >= state->options.max_expansion_depth + 1)
		return hx_os_i_pp_fail(state, HX_OS_LIMIT,
			"macro nesting limit exhausted", NULL, error);
	for (i = 0; name[0] && i < depth; ++i)
		if (!strcmp(state->expansion[i].name, name))
			return hx_os_i_pp_fail(state, HX_OS_SYNTAX,
				"recursive macro expansion", NULL, error);
	if (name[0]) {
		status = hx_os_i_pp_charge(state, text.size, NULL, error);
		if (status != HX_OS_OK)
			return status;
	}
	if (depth == state->expansion_capacity) {
		void *grown = hx_os_pp_grow(state, state->expansion,
			&state->expansion_capacity,
			state->options.max_expansion_depth + 1, sizeof(*frame));

		if (!grown)
			return hx_os_i_pp_fail(state, HX_OS_NO_MEMORY,
				"cannot grow macro stack", NULL, error);
		state->expansion = grown;
	}
	frame = &state->expansion[depth];
	memset(frame, 0, sizeof(*frame));
	memcpy(frame->name, name, strlen(name) + 1);

	return hx_os_i_pp_lexer(state, text,
		HX_OS_LEXER_KEEP_SPACE | HX_OS_LEXER_KEEP_COMMENTS |
			HX_OS_LEXER_C_COMMENTS,
		&frame->lexer, error);
}

/**
 * \brief All conditions are expanded as text before evaluation, preserving
 * operator precedence.
 *
 * \details The defined operand is exempt even across replacement frames.
 */
static hx_os_status hx_os_pp_expand(hx_os_pp_state *state, hx_string_view input,
	const char *initial_name, int condition, hx_string *out,
	hx_os_error *error)
{
	size_t depth = 0;
	int defined_state = 0;
	hx_os_status status = hx_os_pp_push_macro(
		state, 0, initial_name ? initial_name : "", input, error);

	if (status != HX_OS_OK)
		return hx_os_i_pp_fail(state, status,
			"cannot create macro lexer", NULL, error);
	depth = 1;
	if (initial_name)
		status = hx_os_pp_separator(state, out, ' ', error);
	while (status == HX_OS_OK && depth) {
		hx_os_token token;
		hx_os_pp_macro_frame *frame = &state->expansion[depth - 1];
		char name[128];
		hx_string_view replacement;

		status = hx_os_lexer_next(frame->lexer, &token, error);
		if (status == HX_OS_EOF) {
			status = frame->name[0]
				? hx_os_pp_separator(state, out, ' ', error)
				: HX_OS_OK;
			hx_os_lexer_destroy(&frame->lexer);
			--depth;
			continue;
		}
		if (status != HX_OS_OK)
			break;
		if (condition && token.kind != HX_OS_TOKEN_SPACE &&
			token.kind != HX_OS_TOKEN_COMMENT &&
			token.kind != HX_OS_TOKEN_NEWLINE) {
			if (defined_state) {
				if (defined_state == 1 &&
					hx_os_token_is(&token, "("))
					defined_state = 2;
				else if ((defined_state == 1 ||
						 defined_state == 2) &&
					token.kind == HX_OS_TOKEN_IDENTIFIER)
					defined_state =
						defined_state == 1 ? 0 : 3;
				else if (defined_state == 3 &&
					hx_os_token_is(&token, ")"))
					defined_state = 0;
				else {
					status = hx_os_i_pp_fail(state,
						HX_OS_SYNTAX,
						"invalid defined operand",
						&token, error);
					break;
				}
				status = hx_os_pp_append(
					state, out, token.text, error);
				continue;
			}
			if (hx_os_token_is(&token, "defined")) {
				defined_state = 1;
				status = hx_os_pp_append(
					state, out, token.text, error);
				continue;
			}
		}

		status = hx_os_i_pp_macro(
			state, &token, name, &replacement, error);
		if (status == HX_OS_OK) {
			size_t macro_depth = depth - (initial_name ? 0 : 1);

			if (macro_depth == state->options.max_expansion_depth) {
				status = hx_os_i_pp_fail(state, HX_OS_LIMIT,
					"macro nesting limit exhausted", &token,
					error);
				break;
			}
			status = hx_os_pp_separator(state, out, ' ', error);
			if (status == HX_OS_OK)
				status = hx_os_pp_push_macro(
					state, depth, name, replacement, error);
			if (status == HX_OS_OK)
				++depth;
		}
		else if (status == HX_OS_NOT_FOUND)
			status = hx_os_pp_append(state, out, token.text, error);
	}
	while (depth)
		hx_os_lexer_destroy(&state->expansion[--depth].lexer);
	if (status == HX_OS_OK && defined_state)
		status = hx_os_i_pp_fail(state, HX_OS_SYNTAX,
			"incomplete defined operand", NULL, error);
	if (status != HX_OS_OK)
		return hx_os_i_pp_fail(state, status,
			"cannot expand preprocessing tokens", NULL, error);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_pp_definition(hx_os_pp_state *state, const char *name,
	hx_string_view replacement, hx_os_error *error)
{
	hx_os_lexer *lexer = NULL;
	hx_os_token token;
	size_t name_bytes;
	hx_os_status status = hx_os_i_parameter_name(name, &name_bytes, error);

	if (status != HX_OS_OK)
		return status;
	if (!strcmp(name, "defined") || !strcmp(name, "true") ||
		!strcmp(name, "false"))
		return hx_os_i_pp_fail(state, HX_OS_SYNTAX,
			"reserved conditional keyword cannot be a macro", NULL,
			error);
	status = hx_os_i_pp_lexer(
		state, hx_string_view_from_cstr(name), 0, &lexer, error);
	if (status == HX_OS_OK)
		status = hx_os_lexer_next(lexer, &token, error);
	if (status == HX_OS_OK &&
		(token.kind != HX_OS_TOKEN_IDENTIFIER || token.offset ||
			token.text.size != name_bytes))
		status = hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
	hx_os_lexer_destroy(&lexer);
	if (status != HX_OS_OK)
		return hx_os_i_pp_fail(
			state, status, "invalid macro identifier", NULL, error);
	if (replacement.size > state->options.max_macro_bytes)
		return hx_os_i_pp_fail(state, HX_OS_LIMIT,
			"macro storage limit exceeded", NULL, error);
	status = hx_os_i_pp_lexer(
		state, replacement, HX_OS_LEXER_C_COMMENTS, &lexer, error);
	while (status == HX_OS_OK) {
		status = hx_os_lexer_next(lexer, &token, error);
		if (status == HX_OS_OK &&
			(token.kind == HX_OS_TOKEN_NEWLINE ||
				hx_os_token_is(&token, "#") ||
				hx_os_token_is(&token, "\\")))
			status =
				hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	}
	hx_os_lexer_destroy(&lexer);
	if (status != HX_OS_EOF)
		return hx_os_i_pp_fail(state, status,
			"invalid macro replacement or unsupported macro "
			"operator",
			NULL, error);

	return hx_os_params_set_string(state->macros, name, replacement, error);
}

static int hx_os_pp_active(const hx_os_pp_source *source)
{
	return !source->condition_count ||
		source->conditions[source->condition_count - 1].active;
}

static hx_os_status hx_os_pp_line(
	hx_os_lexer *lexer, hx_string_view *line, hx_os_error *error)
{
	hx_os_token token;
	const char *begin = NULL, *end = NULL;
	hx_os_status status;

	for (;;) {
		status = hx_os_lexer_next(lexer, &token, error);
		if (status != HX_OS_OK && status != HX_OS_EOF)
			return status;
		if (status == HX_OS_EOF || token.kind == HX_OS_TOKEN_NEWLINE)
			break;
		if (!begin)
			begin = token.text.data;
		end = token.text.data + token.text.size;
	}
	line->data = begin;
	line->size = begin ? (size_t)(end - begin) : 0;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_pp_single_name(
	hx_os_lexer *lexer, char name[128], hx_os_error *error)
{
	hx_os_token token;
	hx_os_status status = hx_os_lexer_expect_kind(
		lexer, HX_OS_TOKEN_IDENTIFIER, &token, error);

	if (status != HX_OS_OK)
		return status;
	if (token.text.size > 127)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	memcpy(name, token.text.data, token.text.size);
	name[token.text.size] = 0;

	return hx_os_lexer_expect_kind(lexer, HX_OS_TOKEN_END, NULL, error);
}

static hx_os_status hx_os_pp_condition_value(hx_os_pp_state *state,
	hx_string_view expression, int *truth, hx_os_error *error)
{
	hx_string expanded;
	int64_t value = 0;
	hx_os_status status;

	hx_string_init(&expanded, state->context);
	status = hx_os_pp_expand(state, expression, NULL, 1, &expanded, error);
	if (status == HX_OS_OK)
		status = hx_os_i_pp_evaluate(
			state, hx_string_view_of(&expanded), &value, error);
	hx_string_destroy(&expanded);
	if (status == HX_OS_OK)
		*truth = value != 0;

	return status;
}

static hx_os_status hx_os_pp_include(
	hx_os_pp_state *state, hx_os_lexer *lexer, hx_os_error *error)
{
	hx_string requested, text, resolved;
	hx_os_status status;
	hx_os_error callback_error = {HX_OS_OK, HX_OS_ERROR_NONE, 0, 0};
	size_t maximum, invalid;

	if ((state->options.flags & HX_OS_PREPROCESS_NO_INCLUDES) ||
		!state->options.include)
		return hx_os_i_pp_fail(state, HX_OS_NOT_SUPPORTED,
			"includes are disabled", NULL, error);
	if (state->source_count == state->options.max_include_depth)
		return hx_os_i_pp_fail(state, HX_OS_LIMIT,
			"include depth exhausted", NULL, error);
	hx_string_init(&requested, state->context);
	hx_string_init(&text, state->context);
	hx_string_init(&resolved, state->context);
	status = hx_os_lexer_read_string(lexer, &requested, error);
	if (status == HX_OS_OK)
		status = hx_os_lexer_expect_kind(
			lexer, HX_OS_TOKEN_END, NULL, error);
	if (status == HX_OS_OK &&
		(!requested.size || requested.size > 4096 ||
			memchr(requested.data, 0, requested.size)))
		status = hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (status == HX_OS_OK)
		status = hx_string_assign(
			&resolved, hx_string_view_of(&requested), error);
	maximum = state->options.max_total_input_bytes - state->total_input;
	if (maximum > state->options.max_input_bytes)
		maximum = state->options.max_input_bytes;
	if (status == HX_OS_OK) {
		status = state->options.include(
			state->sources[state->source_count - 1].name.data,
			requested.data, maximum, &text, &resolved,
			state->options.user, &callback_error);
		if (status != HX_OS_OK) {
			if (callback_error.status != status)
				(void)hx_os_i_error(
					&callback_error, status, 0, 0);
			if (error)
				*error = callback_error;
		}
	}
	if (status == HX_OS_OK &&
		(!resolved.size || resolved.size > 4096 ||
			memchr(resolved.data, 0, resolved.size)))
		status = hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (status == HX_OS_OK)
		status = hx_os_i_error(error,
			hx_os_text_validate_utf8(
				resolved.data, resolved.size, &invalid),
			0, 0);
	if (status == HX_OS_OK)
		status = hx_os_pp_push_source(
			state, resolved.data, hx_string_view_of(&text), error);
	if (status != HX_OS_OK)
		(void)hx_os_i_pp_fail(state, status,
			"cannot include requested source", NULL, error);
	hx_string_destroy(&requested);
	hx_string_destroy(&text);
	hx_string_destroy(&resolved);

	return status;
}

static hx_os_status hx_os_pp_directive(
	hx_os_pp_state *state, hx_string_view line, hx_os_error *error)
{
	hx_os_lexer *lexer = NULL;
	hx_os_token command, name_token;
	hx_os_pp_source *source = &state->sources[state->source_count - 1];
	hx_string_view remainder;
	hx_os_status status;
	char name[128];
	int truth = 0, active = hx_os_pp_active(source);

	if (state->directives == state->options.max_directives)
		return hx_os_i_pp_fail(state, HX_OS_LIMIT,
			"directive limit exhausted", NULL, error);
	++state->directives;
	status = hx_os_i_pp_lexer(
		state, line, HX_OS_LEXER_C_COMMENTS, &lexer, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_lexer_next(lexer, &command, error);
	if (status == HX_OS_EOF) {
		status = hx_os_i_error(error, HX_OS_OK, 0, 0);
		goto done;
	}
	if (status != HX_OS_OK)
		goto done;
	remainder.data = line.data + command.offset + command.text.size;
	remainder.size = line.size - command.offset - command.text.size;

	if (hx_os_token_is(&command, "if") ||
		hx_os_token_is(&command, "ifdef") ||
		hx_os_token_is(&command, "ifndef")) {
		hx_os_pp_condition *branch;

		if (source->condition_count ==
			state->options.max_condition_depth) {
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
			goto done;
		}
		if (hx_os_token_is(&command, "if")) {
			if (active)
				status = hx_os_pp_condition_value(
					state, remainder, &truth, error);
		}
		else {
			hx_string_view ignored;

			status = hx_os_pp_single_name(lexer, name, error);
			if (status == HX_OS_OK) {
				status = hx_os_params_get_string(
					state->macros, name, &ignored, error);
				truth = status == HX_OS_OK;
				if (status == HX_OS_NOT_FOUND)
					status = hx_os_i_error(
						error, HX_OS_OK, 0, 0);
				if (hx_os_token_is(&command, "ifndef"))
					truth = !truth;
			}
		}
		if (status != HX_OS_OK)
			goto done;
		branch = &source->conditions[source->condition_count++];
		branch->parent_active = (unsigned char)active;
		branch->active = (unsigned char)(active && truth);
		branch->taken = branch->active;
		branch->had_else = 0;
	}
	else if (hx_os_token_is(&command, "elif") ||
		hx_os_token_is(&command, "else")) {
		hx_os_pp_condition *branch;

		if (!source->condition_count) {
			status = hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
			goto done;
		}
		branch = &source->conditions[source->condition_count - 1];
		if (branch->had_else) {
			status = hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
			goto done;
		}
		if (hx_os_token_is(&command, "else")) {
			status = hx_os_lexer_expect_kind(
				lexer, HX_OS_TOKEN_END, NULL, error);
			truth = 1;
			branch->had_else = 1;
		}
		else if (branch->parent_active && !branch->taken)
			status = hx_os_pp_condition_value(
				state, remainder, &truth, error);
		if (status != HX_OS_OK)
			goto done;
		branch->active = (unsigned char)(branch->parent_active &&
			!branch->taken && truth);
		branch->taken |= branch->active;
	}
	else if (hx_os_token_is(&command, "endif")) {
		status = hx_os_lexer_expect_kind(
			lexer, HX_OS_TOKEN_END, NULL, error);
		if (status == HX_OS_OK && !source->condition_count)
			status = hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
		if (status == HX_OS_OK)
			--source->condition_count;
	}
	else if (!active)
		status = HX_OS_OK;
	else if (hx_os_token_is(&command, "define")) {
		status = hx_os_lexer_expect_kind(
			lexer, HX_OS_TOKEN_IDENTIFIER, &name_token, error);
		if (status != HX_OS_OK)
			goto done;
		if (name_token.text.size > 127) {
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
			goto done;
		}
		memcpy(name, name_token.text.data, name_token.text.size);
		name[name_token.text.size] = 0;
		remainder.data =
			line.data + name_token.offset + name_token.text.size;
		remainder.size =
			line.size - name_token.offset - name_token.text.size;
		if (remainder.size && remainder.data[0] == '(')
			status = hx_os_i_pp_fail(state, HX_OS_NOT_SUPPORTED,
				"function macros are not supported", NULL,
				error);
		else
			status = hx_os_pp_definition(
				state, name, remainder, error);
	}
	else if (hx_os_token_is(&command, "undef")) {
		status = hx_os_pp_single_name(lexer, name, error);
		if (status == HX_OS_OK) {
			status =
				hx_os_params_remove(state->macros, name, error);
			if (status == HX_OS_NOT_FOUND)
				status = hx_os_i_error(error, HX_OS_OK, 0, 0);
		}
	}
	else if (hx_os_token_is(&command, "include"))
		status = hx_os_pp_include(state, lexer, error);
	else
		status = hx_os_i_pp_fail(state, HX_OS_NOT_SUPPORTED,
			"unsupported preprocessing directive", NULL, error);

done:
	hx_os_lexer_destroy(&lexer);
	if (status != HX_OS_OK)
		return hx_os_i_pp_fail(state, status,
			"invalid preprocessing directive", NULL, error);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_pp_process(hx_os_pp_state *state, hx_os_error *error)
{
	while (state->source_count) {
		hx_os_pp_source *source =
			&state->sources[state->source_count - 1];
		hx_os_token token;
		hx_os_status status =
			hx_os_lexer_next(source->lexer, &token, error);

		if (status == HX_OS_EOF) {
			if (source->condition_count)
				return hx_os_i_pp_fail(state,
					HX_OS_TRUNCATED_INPUT,
					"unterminated conditional directive",
					&token, error);
			if (state->source_count > 1) {
				status = hx_os_pp_separator(
					state, &state->output, '\n', error);
				if (status != HX_OS_OK)
					return status;
			}
			hx_os_pp_pop_source(state);
			continue;
		}
		if (status != HX_OS_OK)
			return hx_os_i_pp_fail(state, status,
				"invalid source token", NULL, error);
		if (token.kind == HX_OS_TOKEN_NEWLINE) {
			source->line_start = 1;
			status = hx_os_pp_append(
				state, &state->output, token.text, error);
		}
		else if (token.kind == HX_OS_TOKEN_SPACE ||
			token.kind == HX_OS_TOKEN_COMMENT) {
			if (memchr(token.text.data, '\n', token.text.size) ||
				memchr(token.text.data, '\r', token.text.size))
				source->line_start = 1;
			if (hx_os_pp_active(source))
				status = hx_os_pp_append(state, &state->output,
					token.text, error);
		}
		else if (source->line_start && hx_os_token_is(&token, "#")) {
			hx_string_view line;

			state->origin = &token;
			source->line_start = 1;
			status = hx_os_pp_line(source->lexer, &line, error);
			if (status == HX_OS_OK)
				status = hx_os_pp_separator(
					state, &state->output, '\n', error);
			if (status == HX_OS_OK)
				status = hx_os_pp_directive(state, line, error);
			if (status != HX_OS_OK)
				(void)hx_os_i_pp_fail(state, status,
					"invalid directive text", &token,
					error);
			state->origin = NULL;
		}
		else {
			int active = hx_os_pp_active(source);

			source->line_start = 0;
			if (active) {
				hx_string_view replacement;
				char name[128];

				state->origin = &token;
				status = hx_os_i_pp_macro(state, &token, name,
					&replacement, error);
				if (status == HX_OS_OK)
					status = hx_os_pp_expand(state,
						replacement, name, 0,
						&state->output, error);
				else if (status == HX_OS_NOT_FOUND)
					status = hx_os_pp_append(state,
						&state->output, token.text,
						error);
				state->origin = NULL;
			}
		}
		if (status != HX_OS_OK)
			return status;
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_pp_run(hx_string_view input, const char *source_name,
	const hx_os_preprocess_options *options, hx_string *out,
	hx_os_preprocess_diagnostic *diagnostic, hx_os_error *error)
{
	hx_os_pp_state *state;
	hx_os_params_options macro_options = HX_OS_PARAMS_OPTIONS_INIT;
	hx_os_status status;
	size_t i;

	state = hx_os_i_zero(out->context, sizeof(*state));
	if (!state)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	state->options = *options;
	state->context = out->context;
	state->root_name = source_name;
	state->diagnostic = diagnostic;
	hx_string_init(&state->output, out->context);
	macro_options.max_entries = options->max_defines;
	macro_options.max_bytes = options->max_macro_bytes;
	status = hx_os_params_create_with_options(
		state->context, &macro_options, &state->macros, error);
	for (i = 0; status == HX_OS_OK && i < options->define_count; ++i)
		status = hx_os_pp_definition(state, options->defines[i].name,
			options->defines[i].replacement, error);
	if (status == HX_OS_OK)
		status = hx_os_pp_push_source(state, source_name, input, error);
	if (status == HX_OS_OK)
		status = hx_os_pp_process(state, error);
	if (status != HX_OS_OK)
		(void)hx_os_i_pp_fail(
			state, status, "preprocessing failed", NULL, error);
	if (status == HX_OS_OK)
		hx_string_move(out, &state->output);
	while (state->source_count)
		hx_os_pp_pop_source(state);
	(void)hx_os_params_destroy(&state->macros, NULL);
	hx_os_memory_free(state->sources);
	hx_os_memory_free(state->expansion);
	hx_string_destroy(&state->output);
	hx_os_memory_free(state);

	return status;
}
