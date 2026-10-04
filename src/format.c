#include "format_private.h"

hx_os_status hx_format_options_init(hx_format_options *options, size_t capacity)
{
	const hx_format_options defaults = HX_FORMAT_OPTIONS_INIT;

	if (!options || capacity < sizeof(defaults))
		return HX_OS_INVALID;

	*options = defaults;

	return HX_OS_OK;
}

#if HX_OS_ENABLE_FORMAT
hx_os_status hx_os_i_format_append(hx_string *out, const char *data,
	size_t bytes, size_t limit, hx_os_error *error)
{
	hx_string_view view = {data, bytes};

	if (out->size > limit || bytes > limit - out->size)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	return hx_string_append(out, view, error);
}

static int hx_os_format_digit(char c)
{
	return c >= '0' && c <= '9';
}

static hx_os_status hx_os_format_index(
	hx_os_format_state *state, hx_string_view id, const hx_format_arg **out)
{
	size_t i, index = 0;

	if (!id.size) {
		if (state->indexing == 2)
			return HX_OS_SYNTAX;

		state->indexing = 1;
		index = state->next++;
	}
	else if (hx_os_format_digit(id.data[0])) {
		if (state->indexing == 1)
			return HX_OS_SYNTAX;

		state->indexing = 2;

		for (i = 0; i < id.size; ++i) {
			unsigned digit = (unsigned)(id.data[i] - '0');

			if (digit > 9)
				return HX_OS_SYNTAX;

			if (index > (SIZE_MAX - digit) / 10)
				return HX_OS_RANGE;

			index = index * 10 + digit;
		}
	}
	else {
		for (i = 0; i < state->count; ++i) {
			const char *name = state->args[i].name;

			if (name && strlen(name) == id.size &&
				!memcmp(name, id.data, id.size)) {
				*out = &state->args[i];

				return HX_OS_OK;
			}
		}

		return HX_OS_NOT_FOUND;
	}

	if (index >= state->count)
		return HX_OS_RANGE;

	*out = &state->args[index];

	return HX_OS_OK;
}

static hx_os_status hx_os_format_count(hx_os_format_state *state,
	hx_string_view text, size_t *position, size_t *out)
{
	size_t value = 0, p = *position, start;

	if (p < text.size && text.data[p] == '{') {
		const hx_format_arg *arg;
		hx_string_view id;
		hx_os_status status;

		start = ++p;

		while (p < text.size && text.data[p] != '}')
			++p;

		if (p == text.size)
			return HX_OS_SYNTAX;

		id.data = text.data + start;
		id.size = p - start;

		status = hx_os_format_index(state, id, &arg);
		if (status != HX_OS_OK)
			return status;

		if (arg->kind == HX_FORMAT_U64) {
			if (arg->value.natural > SIZE_MAX)
				return HX_OS_LIMIT;

			value = (size_t)arg->value.natural;
		}
		else if (arg->kind == HX_FORMAT_I64) {
			if (arg->value.integer < 0 ||
				(uint64_t)arg->value.integer > SIZE_MAX)
				return HX_OS_RANGE;
			value = (size_t)arg->value.integer;
		}
		else
			return HX_OS_TYPE_MISMATCH;

		++p;
	}
	else {
		start = p;

		while (p < text.size && hx_os_format_digit(text.data[p])) {
			unsigned digit = (unsigned)(text.data[p++] - '0');

			if (value > (SIZE_MAX - digit) / 10)
				return HX_OS_LIMIT;

			value = value * 10 + digit;
		}

		if (p == start)
			return HX_OS_SYNTAX;
	}

	if (value > state->limit)
		return HX_OS_LIMIT;

	*position = p;
	*out = value;

	return HX_OS_OK;
}

static int hx_os_format_align(char c)
{
	return c == '<' || c == '>' || c == '^' || c == '=';
}

static hx_os_status hx_os_format_spec_parse(
	hx_os_format_state *state, hx_string_view text, hx_os_format_spec *out)
{
	size_t p = 0, used;
	uint32_t point;
	hx_os_status status;

	memset(out, 0, sizeof(*out));
	out->fill = hx_string_view_from_cstr(" ");
	if (!text.size)
		return HX_OS_OK;

	if (memchr(text.data, 0, text.size))
		return HX_OS_SYNTAX;

	status = hx_os_i_utf_decode(HX_OS_UTF8,
		(const unsigned char *)text.data, text.size, &point, &used);
	if (status != HX_OS_OK)
		return status;

	if (used < text.size && hx_os_format_align(text.data[used])) {
		if (point == '{' || point == '}')
			return HX_OS_SYNTAX;

		out->fill.data = text.data;
		out->fill.size = used;
		out->align = text.data[used];
		p = used + 1;
	}
	else if (hx_os_format_align(text.data[0]))
		out->align = text.data[p++];

	if (p < text.size && strchr("+- ", text.data[p]))
		out->sign = text.data[p++];

	if (p < text.size && text.data[p] == '#') {
		out->alternate = 1;
		++p;
	}

	if (p < text.size && text.data[p] == '0') {
		out->zero = 1;
		++p;
	}

	if (p < text.size &&
		(hx_os_format_digit(text.data[p]) || text.data[p] == '{')) {
		status = hx_os_format_count(state, text, &p, &out->width);
		if (status != HX_OS_OK)
			return status;
	}

	if (p < text.size && (text.data[p] == ',' || text.data[p] == '_'))
		out->grouping = text.data[p++];

	if (p < text.size && text.data[p] == '.') {
		++p;
		out->has_precision = 1;

		status = hx_os_format_count(state, text, &p, &out->precision);
		if (status != HX_OS_OK)
			return status;
	}

	if (p < text.size)
		out->type = text.data[p++];

	return p == text.size ? HX_OS_OK : HX_OS_SYNTAX;
}

static hx_os_status hx_os_format_field(hx_os_format_state *state,
	hx_string_view field, hx_string *out, hx_os_error *error)
{
	const hx_format_arg *arg;
	hx_format_arg converted;
	hx_os_format_spec specification;
	hx_string temporary;
	hx_string_view id, spec;
	size_t p = 0;
	char conversion = 0;
	hx_os_status status;

	while (p < field.size && field.data[p] != ':' && field.data[p] != '!')
		++p;

	id.data = field.data;
	id.size = p;

	status = hx_os_format_index(state, id, &arg);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	if (p < field.size && field.data[p] == '!') {
		if (++p == field.size ||
			(field.data[p] != 's' && field.data[p] != 'r'))
			return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
		conversion = field.data[p++];
	}

	if (p < field.size && field.data[p++] != ':')
		return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);

	spec.data = field.data + p;
	spec.size = field.size - p;
	if (arg->kind == HX_FORMAT_CUSTOM && !conversion) {
		if (!arg->value.custom.format || !arg->value.custom.object)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

		hx_string_init(&temporary, out->context);

		status = arg->value.custom.format(arg->value.custom.object,
			spec, &temporary, state->limit - out->size, error);
		if (status == HX_OS_OK)
			status = hx_os_i_format_append(out,
				hx_string_c_str(&temporary), temporary.size,
				state->limit, error);

		hx_string_destroy(&temporary);

		return status;
	}

	status = hx_os_format_spec_parse(state, spec, &specification);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	if (!conversion)
		return hx_os_i_format_value(
			out, arg, &specification, state->limit, error);

	hx_string_init(&temporary, out->context);
	if (arg->kind == HX_FORMAT_CUSTOM) {
		hx_string_view empty = {"", 0};

		status = arg->value.custom.format && arg->value.custom.object
			? arg->value.custom.format(arg->value.custom.object,
				  empty, &temporary, state->limit, error)
			: HX_OS_INVALID;
	}
	else {
		hx_os_format_spec plain;

		(void)hx_os_format_spec_parse(state,
			hx_string_view_from_cstr(
				conversion == 'r' && arg->kind == HX_FORMAT_TEXT
					? "?"
					: ""),
			&plain);

		status = hx_os_i_format_value(
			&temporary, arg, &plain, state->limit, error);
	}

	converted = hx_fmt_view(hx_string_view_of(&temporary));
	if (status == HX_OS_OK)
		status = hx_os_i_format_value(
			out, &converted, &specification, state->limit, error);

	hx_string_destroy(&temporary);

	return status;
}
#endif

hx_os_status hx_format_with_options(hx_string *out, hx_string_view format,
	const hx_format_arg *arguments, size_t count,
	const hx_format_options *options, hx_os_error *error)
{
#if HX_OS_ENABLE_FORMAT
	const hx_format_options defaults = HX_FORMAT_OPTIONS_INIT;
	hx_os_format_state state;
	hx_string result;
	hx_os_status status;
	size_t p = 0, start, invalid, i, j;

	if (!options)
		options = &defaults;

	if (!out || (!format.data && format.size) || (!arguments && count) ||
		options->size != sizeof(*options) || options->version != 1 ||
		!options->max_arguments || !options->max_output_bytes ||
		options->max_output_bytes == SIZE_MAX)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
#if SIZE_MAX > INT64_MAX
	if (options->max_output_bytes > INT64_MAX)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
#endif
	if (count > options->max_arguments)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	for (i = 0; i < count; ++i)
		if (arguments[i].name)
			for (j = 0; j < i; ++j)
				if (arguments[j].name &&
					!strcmp(arguments[i].name,
						arguments[j].name))
					return hx_os_i_error(
						error, HX_OS_EXISTS, 0, 0);

	status = hx_os_text_validate_utf8(format.data, format.size, &invalid);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	state.args = arguments;
	state.count = count;
	state.next = 0;
	state.indexing = 0;
	state.limit = options->max_output_bytes;
	hx_string_init(&result, out->context);
	hx_os_error_clear(error);

	while (p < format.size && status == HX_OS_OK) {
		start = p;
		if (format.data[p] != '{' && format.data[p] != '}') {
			while (p < format.size && format.data[p] != '{' &&
				format.data[p] != '}')
				++p;

			status = hx_os_i_format_append(&result,
				format.data + start, p - start, state.limit,
				error);
		}
		else if (p + 1 < format.size &&
			format.data[p + 1] == format.data[p]) {
			status = hx_os_i_format_append(&result, format.data + p,
				1, state.limit, error);
			p += 2;
		}
		else if (format.data[p] == '}')
			status = HX_OS_SYNTAX;
		else {
			hx_string_view field;
			int depth = 0;

			field.data = format.data + ++p;

			while (p < format.size) {
				if (format.data[p] == '{' && ++depth > 1)
					break;

				if (format.data[p] == '}' && depth-- == 0)
					break;

				++p;
			}

			if (p == format.size || depth != -1)
				status = HX_OS_SYNTAX;
			else {
				field.size =
					(size_t)(format.data + p - field.data);

				status = hx_os_format_field(
					&state, field, &result, error);
				++p;
			}
		}

		if (status != HX_OS_OK && error && !error->domain)
			error->cause_code = (int64_t)start;
	}

	if (status == HX_OS_OK)
		status = hx_os_text_validate_utf8(
			hx_string_c_str(&result), result.size, &invalid);

	if (status == HX_OS_OK)
		hx_string_move(out, &result);

	hx_string_destroy(&result);
	if (error)
		error->status = status;

	return status;
#else
	(void)out;
	(void)format;
	(void)arguments;
	(void)count;
	(void)options;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_format(hx_string *out, hx_string_view format,
	const hx_format_arg *arguments, size_t count, hx_os_error *error)
{
	return hx_format_with_options(
		out, format, arguments, count, NULL, error);
}

hx_os_status hx_format_buffer(char *buffer, size_t capacity, size_t *required,
	hx_string_view format, const hx_format_arg *arguments, size_t count,
	hx_os_error *error)
{
	hx_string text = HX_STRING_INIT;
	hx_os_status status;

	if (!required || (!buffer && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_format(&text, format, arguments, count, error);
	if (status == HX_OS_OK) {
		*required = text.size + 1;
		if (capacity < *required)
			status = hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);
		else
			memcpy(buffer, hx_string_c_str(&text), *required);
	}

	hx_string_destroy(&text);

	return status;
}

hx_os_status hx_print_args(int stream, int newline, hx_string_view format,
	const hx_format_arg *arguments, size_t count, hx_os_error *error)
{
	hx_string text = HX_STRING_INIT;
	hx_os_status status;

	if (stream != HX_PRINT_STDOUT && stream != HX_PRINT_STDERR)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_format(&text, format, arguments, count, error);
#if HX_OS_ENABLE_FORMAT
	if (status == HX_OS_OK && newline)
		status = hx_os_i_format_append(
			&text, "\n", 1, 16u * 1024u * 1024u, error);

	if (status == HX_OS_OK)
		status = hx_os_i_format_print(
			stream, hx_string_c_str(&text), text.size, error);
#else
	(void)newline;
#endif
	hx_string_destroy(&text);

	return status;
}
