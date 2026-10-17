#include "regex_private.h"

hx_os_status hx_os_i_regex_error(
	hx_os_error *error, hx_os_status status, int code, size_t offset)
{
	hx_os_i_error(error, status, HX_OS_ERROR_REGEX, code);
	if (error)
#if SIZE_MAX > INT64_MAX
		error->cause_code =
			offset > INT64_MAX ? INT64_MAX : (int64_t)offset;
#else
		error->cause_code = (int64_t)offset;
#endif
	return status;
}

const char *hx_os_i_regex_message(int code)
{
	static const char *const messages[] = {"Regular expression error",
		"Invalid or unterminated group",
		"Invalid or unterminated character class",
		"Invalid escape or UTF-8 encoding",
		"Invalid repetition or repetition limit exceeded",
		"Unknown, invalid or duplicate capture reference",
		"Unsupported regular expression syntax or Unicode capability",
		"Regular expression nesting/recursion limit exceeded",
		"Regular expression memory/input limit exceeded",
		"Regular expression computation budget exceeded",
		"Lookbehind must have a bounded permitted length",
		"Invalid replacement capture reference"};

	return code >= 0 &&
			(size_t)code < sizeof(messages) / sizeof(messages[0])
		? messages[code]
		: messages[0];
}

int hx_os_regex_available(void)
{
	return HX_OS_ENABLE_REGEX;
}

hx_os_status hx_os_i_regex_compile_work(
	hx_os_regex *regex, uint32_t count, size_t offset, hx_os_error *error)
{
	if (count > HX_OS_RX_COMPILE_WORK - regex->compile_work)
		return hx_os_i_regex_error(
			error, HX_OS_LIMIT, HX_OS_RX_STEP_LIMIT, offset);
	regex->compile_work += count;
	return HX_OS_OK;
}

hx_os_status hx_os_i_regex_append(hx_os_regex *regex, hx_bytes *buffer,
	hx_os_bytes_view bytes, size_t offset, hx_os_error *error)
{
	size_t used =
		regex->code.size + regex->ranges.size + regex->classes.size;

	if (bytes.size > regex->options.max_program_bytes - used)
		return hx_os_i_regex_error(
			error, HX_OS_LIMIT, HX_OS_RX_MEMORY_LIMIT, offset);
	return hx_bytes_append(buffer, bytes, error);
}

hx_os_status hx_os_regex_compile(hx_os_context *context, hx_string_view pattern,
	const hx_os_regex_options *options, hx_os_regex **out,
	hx_os_error *error)
{
	const hx_os_regex_options defaults = HX_OS_REGEX_OPTIONS_INIT;
	hx_os_regex_options selected;
	hx_os_regex *regex;
	hx_os_status status;
	size_t bad;
	uint32_t root;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!HX_OS_ENABLE_REGEX)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (options &&
		(options->size < sizeof(*options) || options->version != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	selected = options ? *options : defaults;
	if (selected.flags & ~UINT32_C(127) || !selected.max_pattern_bytes ||
		selected.max_pattern_bytes > 32768 ||
		selected.max_program_bytes < 1024 ||
		selected.max_program_bytes > HX_BYTES_MAX_SIZE ||
		!selected.max_nesting || selected.max_nesting > 128 ||
		!selected.max_repeat || selected.max_repeat > 65535 ||
		selected.max_lookbehind > 1024 ||
		(!pattern.data && pattern.size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (pattern.size > selected.max_pattern_bytes)
		return hx_os_i_regex_error(
			error, HX_OS_LIMIT, HX_OS_RX_MEMORY_LIMIT, 0);
	status = hx_os_text_validate_utf8(pattern.data, pattern.size, &bad);
	if (status != HX_OS_OK)
		return hx_os_i_regex_error(
			error, status, HX_OS_RX_BAD_ESCAPE, bad);
	regex = hx_os_i_zero(context, sizeof(*regex));
	if (!regex)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	regex->context = context;
	regex->options = selected;
	hx_string_init(&regex->pattern, context);
	hx_bytes_init(&regex->nodes, context);
	hx_bytes_init(&regex->ranges, context);
	hx_bytes_init(&regex->classes, context);
	hx_bytes_init(&regex->code, context);
	status = hx_string_assign(&regex->pattern, pattern, error);
	if (status == HX_OS_OK)
		status = hx_os_i_regex_parse(regex, &root, error);
	if (status == HX_OS_OK)
		status = hx_os_i_regex_compile(regex, root, error);
	hx_bytes_destroy(&regex->nodes);
	if (status != HX_OS_OK) {
		hx_os_regex_destroy(&regex);
		return status;
	}
	*out = regex;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_regex_destroy(hx_os_regex **regex)
{
	hx_os_regex *value = *regex;

	*regex = NULL;
	if (value) {
		hx_string_destroy(&value->pattern);
		hx_bytes_destroy(&value->nodes);
		hx_bytes_destroy(&value->ranges);
		hx_bytes_destroy(&value->classes);
		hx_bytes_destroy(&value->code);
		hx_os_memory_free(value);
	}
}

size_t hx_os_regex_capture_count(const hx_os_regex *regex)
{
	return regex ? regex->group_count : 0;
}

hx_os_status hx_os_regex_capture_index(const hx_os_regex *regex,
	const char *name, size_t *index, hx_os_error *error)
{
	size_t i, bytes;

	if (!HX_OS_ENABLE_REGEX)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!regex || !name || !*name || !index)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	bytes = strlen(name);
	for (i = 1; i <= regex->group_count; ++i)
		if (regex->groups[i].name_length == bytes &&
			!memcmp(regex->pattern.data +
					regex->groups[i].name_offset,
				name, bytes)) {
			*index = i;
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		}
	return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
}

hx_os_status hx_os_i_regex_budget(const hx_os_regex_limits *limits,
	hx_os_rx_budget *out, hx_os_error *error)
{
	const hx_os_regex_limits defaults = HX_OS_REGEX_LIMITS_INIT;

	if (limits && (limits->size < sizeof(*limits) || limits->version != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	memset(out, 0, sizeof(*out));
	out->limits = limits ? *limits : defaults;
	if (!out->limits.max_steps || out->limits.max_stack_bytes < 1024 ||
		out->limits.max_stack_bytes > HX_BYTES_MAX_SIZE ||
		!out->limits.max_subject_bytes ||
		out->limits.max_subject_bytes > HX_BYTES_MAX_SIZE ||
		!out->limits.max_depth || out->limits.max_depth > 128 ||
		!out->limits.max_results || out->limits.max_results > 1000000)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return HX_OS_OK;
}

hx_os_status hx_os_i_regex_step(
	hx_os_rx_budget *budget, uint64_t steps, hx_os_error *error)
{
	uint64_t previous = budget->steps;

	if (steps > budget->limits.max_steps - budget->steps)
		return hx_os_i_regex_error(
			error, HX_OS_LIMIT, HX_OS_RX_STEP_LIMIT, 0);
	budget->steps += steps;
	if (!steps || !previous || (previous >> 10) != (budget->steps >> 10)) {
		if (hx_os_cancel_requested(budget->limits.cancel))
			return hx_os_i_error(error, HX_OS_CANCELLED, 0, 0);
		if (budget->limits.deadline != HX_OS_DEADLINE_INFINITE) {
			uint64_t now;
			hx_os_status status = hx_os_monotonic_read(&now, error);

			if (status != HX_OS_OK)
				return status;
			if (budget->limits.deadline == HX_OS_DEADLINE_NOW ||
				now >= budget->limits.deadline)
				return hx_os_i_error(
					error, HX_OS_TIMEOUT, 0, 0);
		}
	}
	return HX_OS_OK;
}

hx_os_status hx_os_regex_find_with_limits(const hx_os_regex *regex,
	hx_string_view subject, size_t start, const hx_os_regex_limits *limits,
	hx_os_regex_result *out, hx_os_error *error)
{
	hx_os_rx_budget budget;
	hx_os_status status;

	if (!HX_OS_ENABLE_REGEX)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	status = hx_os_i_regex_budget(limits, &budget, error);
	if (status != HX_OS_OK)
		return status;
	return hx_os_i_regex_run(regex, subject, start, 0, &budget, out, error);
}

hx_os_status hx_os_regex_find(const hx_os_regex *regex, hx_string_view subject,
	size_t start, hx_os_regex_result *out, hx_os_error *error)
{
	return hx_os_regex_find_with_limits(
		regex, subject, start, NULL, out, error);
}

hx_os_status hx_os_regex_full_match(const hx_os_regex *regex,
	hx_string_view subject, const hx_os_regex_limits *limits, int *matched,
	hx_os_error *error)
{
	hx_os_rx_budget budget;
	hx_os_regex_result result;
	hx_os_status status;

	if (!HX_OS_ENABLE_REGEX)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!matched)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_regex_budget(limits, &budget, error);
	if (status == HX_OS_OK)
		status = hx_os_i_regex_run(
			regex, subject, 0, 1, &budget, &result, error);
	if (status == HX_OS_OK || status == HX_OS_NOT_FOUND) {
		*matched = status == HX_OS_OK;
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
	return status;
}

hx_os_status hx_os_regex_matches(const char *pattern, hx_string_view subject,
	int *matched, hx_os_error *error)
{
	hx_os_regex *regex = NULL;
	hx_os_status status;

	if (!pattern || !matched)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_regex_compile(
		NULL, hx_string_view_from_cstr(pattern), NULL, &regex, error);
	if (status == HX_OS_OK)
		status = hx_os_regex_full_match(
			regex, subject, NULL, matched, error);
	hx_os_regex_destroy(&regex);
	return status;
}

static size_t hx_os_rx_advance(hx_string_view text, size_t position)
{
	uint32_t point;
	size_t bytes = 0;

	if (position < text.size)
		(void)hx_os_i_utf_decode(HX_OS_UTF8,
			(const unsigned char *)text.data + position,
			text.size - position, &point, &bytes);
	return position + bytes;
}

static hx_os_status hx_os_rx_append(hx_string *out, hx_string_view input,
	size_t maximum, hx_os_error *error)
{
	if (input.size > maximum - out->size)
		return hx_os_i_regex_error(
			error, HX_OS_LIMIT, HX_OS_RX_MEMORY_LIMIT, 0);
	return hx_string_append(out, input, error);
}

static hx_os_status hx_os_rx_replacement(const hx_os_regex *regex,
	hx_string_view subject, hx_string_view replacement,
	const hx_os_regex_result *match, hx_string *out, size_t maximum,
	hx_os_error *error)
{
	size_t position = 0, literal = 0;

	while (position < replacement.size) {
		size_t index, start;
		hx_os_status status;

		if (replacement.data[position] != '$') {
			++position;
			continue;
		}
		status = hx_os_rx_append(out,
			(hx_string_view){
				replacement.data + literal, position - literal},
			maximum, error);
		if (status != HX_OS_OK)
			return status;
		start = ++position;
		if (position == replacement.size)
			return hx_os_i_regex_error(error, HX_OS_SYNTAX,
				HX_OS_RX_BAD_REPLACEMENT, position - 1);
		if (replacement.data[position] == '$') {
			status = hx_os_rx_append(
				out, (hx_string_view){"$", 1}, maximum, error);
			++position;
		}
		else {
			index = SIZE_MAX;
			if (replacement.data[position] == '{') {
				const char *name;
				size_t bytes, group;

				start = ++position;
				while (position < replacement.size &&
					replacement.data[position] != '}')
					++position;
				if (position == replacement.size)
					return hx_os_i_regex_error(error,
						HX_OS_SYNTAX,
						HX_OS_RX_BAD_REPLACEMENT,
						start);
				bytes = position - start;
				name = replacement.data + start;
				for (group = 1; group <= regex->group_count;
					++group) {
					const hx_os_rx_group *info =
						regex->groups + group;
					const char *group_name =
						regex->pattern.data +
						info->name_offset;

					if (info->name_length == bytes &&
						!memcmp(name, group_name,
							bytes)) {
						index = group;
						break;
					}
				}
				++position;
			}
			else if (replacement.data[position] >= '0' &&
				replacement.data[position] <= '9') {
				index = 0;
				while (position < replacement.size &&
					replacement.data[position] >= '0' &&
					replacement.data[position] <= '9') {
					index = index * 10 +
						(unsigned)(replacement.data
								   [position] -
							'0');
					++position;
					if (index > HX_OS_REGEX_MAX_CAPTURES)
						break;
				}
			}
			if (index >= match->count)
				return hx_os_i_regex_error(error, HX_OS_SYNTAX,
					HX_OS_RX_BAD_REPLACEMENT, start);
			if (match->captures[index].offset != SIZE_MAX)
				status = hx_os_rx_append(out,
					(hx_string_view){subject.data +
							match->captures[index]
								.offset,
						match->captures[index].size},
					maximum, error);
			else
				status = HX_OS_OK;
		}
		if (status != HX_OS_OK)
			return status;
		literal = position;
	}
	return hx_os_rx_append(out,
		(hx_string_view){
			replacement.data ? replacement.data + literal : NULL,
			replacement.size - literal},
		maximum, error);
}

hx_os_status hx_os_regex_replace(const hx_os_regex *regex,
	hx_string_view subject, hx_string_view replacement,
	const hx_os_regex_limits *limits, hx_string *out, hx_os_error *error)
{
	hx_os_rx_budget budget;
	hx_string result;
	hx_os_regex_result match;
	hx_os_status status;
	size_t start = 0, previous = 0, count = 0, bad;

	if (!HX_OS_ENABLE_REGEX)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!out || !regex)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_regex_budget(limits, &budget, error);
	if (status != HX_OS_OK)
		return status;
	if (replacement.size > budget.limits.max_subject_bytes)
		return hx_os_i_regex_error(
			error, HX_OS_LIMIT, HX_OS_RX_MEMORY_LIMIT, 0);
	status = hx_os_text_validate_utf8(
		replacement.data, replacement.size, &bad);
	if (status != HX_OS_OK)
		return hx_os_i_regex_error(
			error, status, HX_OS_RX_BAD_ESCAPE, bad);
	hx_string_init(&result, out->context);
	for (;;) {
		size_t end;

		status = hx_os_i_regex_run(
			regex, subject, start, 0, &budget, &match, error);
		if (status == HX_OS_NOT_FOUND) {
			status = hx_os_rx_append(&result,
				(hx_string_view){subject.data
						? subject.data + previous
						: NULL,
					subject.size - previous},
				budget.limits.max_subject_bytes, error);
			break;
		}
		if (status != HX_OS_OK)
			break;
		if (++count > budget.limits.max_results) {
			status = hx_os_i_regex_error(error, HX_OS_LIMIT,
				HX_OS_RX_MEMORY_LIMIT, start);
			break;
		}
		status = hx_os_rx_append(&result,
			(hx_string_view){
				subject.data ? subject.data + previous : NULL,
				match.captures[0].offset - previous},
			budget.limits.max_subject_bytes, error);
		if (status == HX_OS_OK)
			status = hx_os_rx_replacement(regex, subject,
				replacement, &match, &result,
				budget.limits.max_subject_bytes, error);
		if (status != HX_OS_OK)
			break;
		end = match.captures[0].offset + match.captures[0].size;
		previous = end;
		start = end;
		if (!match.captures[0].size) {
			if (end == subject.size)
				break;
			start = hx_os_rx_advance(subject, end);
		}
	}
	if (status == HX_OS_OK)
		hx_string_move(out, &result);
	hx_string_destroy(&result);
	return status;
}

hx_os_status hx_os_regex_split(const hx_os_regex *regex, hx_string_view subject,
	const hx_os_regex_limits *limits, hx_os_regex_split_fn callback,
	void *user, size_t *completed, hx_os_error *error)
{
	hx_os_rx_budget budget;
	hx_os_regex_result match;
	size_t start = 0, previous = 0;
	hx_os_status status;

	if (completed)
		*completed = 0;
	if (!HX_OS_ENABLE_REGEX)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!regex || !callback || !completed)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_regex_budget(limits, &budget, error);
	if (status != HX_OS_OK)
		return status;
	for (;;) {
		size_t end;
		hx_string_view part;

		status = hx_os_i_regex_run(
			regex, subject, start, 0, &budget, &match, error);
		if (status != HX_OS_OK && status != HX_OS_NOT_FOUND)
			return status;
		if (*completed == budget.limits.max_results)
			return hx_os_i_regex_error(error, HX_OS_LIMIT,
				HX_OS_RX_MEMORY_LIMIT, start);
		part.data = subject.data ? subject.data + previous : NULL;
		part.size = status == HX_OS_NOT_FOUND
			? subject.size - previous
			: match.captures[0].offset - previous;
		{
			hx_os_status delivered = callback(part, previous, user);

			if (delivered != HX_OS_OK)
				return hx_os_i_error(error, delivered, 0, 0);
		}
		++*completed;
		if (status == HX_OS_NOT_FOUND)
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		end = match.captures[0].offset + match.captures[0].size;
		previous = end;
		start = end;
		if (!match.captures[0].size) {
			if (end == subject.size) {
				if (*completed == budget.limits.max_results)
					return hx_os_i_regex_error(error,
						HX_OS_LIMIT,
						HX_OS_RX_MEMORY_LIMIT, start);
				status = callback(
					(hx_string_view){subject.data
							? subject.data + end
							: NULL,
						0},
					end, user);
				if (status == HX_OS_OK)
					++*completed;
				return hx_os_i_error(error, status, 0, 0);
			}
			start = hx_os_rx_advance(subject, end);
		}
	}
}
