#include "internal.h"
#include "hx_os_text_lookup.h"
#include "hx_os_lexer.h"

typedef struct hx_os_lookup_entry {
	char *data;
	size_t key_bytes;
	size_t total_bytes;
} hx_os_lookup_entry;

struct hx_os_text_lookup {
	hx_os_context *context;
	hx_os_lookup_entry *entries;
	size_t count;
	size_t capacity;
	size_t text_bytes;
	size_t max_entries;
	size_t max_text_bytes;
};

hx_os_status hx_os_text_lookup_options_init(
	hx_os_text_lookup_options *options, size_t capacity)
{
	const hx_os_text_lookup_options defaults =
		HX_OS_TEXT_LOOKUP_OPTIONS_INIT;

	if (!options)
		return HX_OS_INVALID;

	if (capacity < sizeof(*options))
		return HX_OS_OUTPUT_FULL;

	*options = defaults;

	return HX_OS_OK;
}
#if HX_OS_ENABLE_TEXT_LOOKUP
static hx_os_status hx_os_lookup_validate(
	const char *text, size_t limit, size_t *bytes, hx_os_error *error)
{
	size_t invalid;
	hx_os_status status;

	if (!text)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*bytes = 0;

	while (*bytes < limit && text[*bytes])
		++*bytes;

	if (*bytes >= limit)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	status = hx_os_text_validate_utf8(text, *bytes, &invalid);

	return hx_os_i_error(error, status, 0, 0);
}

static size_t hx_os_lookup_index(
	const hx_os_text_lookup *lookup, const char *key)
{
	size_t low = 0, high = lookup->count;

	while (low < high) {
		size_t middle = low + (high - low) / 2;

		if (strcmp(lookup->entries[middle].data, key) < 0)
			low = middle + 1;
		else
			high = middle;
	}

	return low;
}

static hx_os_status hx_os_lookup_value(const hx_os_text_lookup *lookup,
	const char *fallback, const char **out, hx_os_error *error)
{
	size_t bytes, index;
	hx_os_status status;

	if (!lookup)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_lookup_validate(
		fallback, lookup->max_text_bytes, &bytes, error);
	if (status != HX_OS_OK)
		return status;

	index = hx_os_lookup_index(lookup, fallback);
	*out = index < lookup->count &&
			!strcmp(lookup->entries[index].data, fallback)
		? lookup->entries[index].data +
			lookup->entries[index].key_bytes + 1
		: fallback;

	return HX_OS_OK;
}
#endif
hx_os_status hx_os_text_lookup_create(
	hx_os_context *context, hx_os_text_lookup **out, hx_os_error *error)
{
	return hx_os_text_lookup_create_with_options(context, NULL, out, error);
}

hx_os_status hx_os_text_lookup_create_with_options(hx_os_context *context,
	const hx_os_text_lookup_options *options, hx_os_text_lookup **out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_TEXT_LOOKUP
	const hx_os_text_lookup_options defaults =
		HX_OS_TEXT_LOOKUP_OPTIONS_INIT;
	hx_os_text_lookup *lookup;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (!options)
		options = &defaults;

	if (options->size < sizeof(*options) || options->version != 1 ||
		!options->max_entries || options->max_text_bytes < 2 ||
		options->max_entries > SIZE_MAX / sizeof(hx_os_lookup_entry))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	lookup = hx_os_i_zero(context, sizeof(*lookup));
	if (!lookup)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	lookup->context = context;
	lookup->max_entries = options->max_entries;
	lookup->max_text_bytes = options->max_text_bytes;
	*out = lookup;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)context;
	(void)options;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_text_lookup_add(hx_os_text_lookup *lookup,
	const char *fallback, const char *translated, hx_os_error *error)
{
#if HX_OS_ENABLE_TEXT_LOOKUP
	size_t key_bytes, value_bytes, total, old_bytes, index, capacity = 0;
	int replace;
	char *text;
	hx_os_lookup_entry *entries = NULL;
	hx_os_status status;

	if (!lookup)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_lookup_validate(
		fallback, lookup->max_text_bytes, &key_bytes, error);
	if (status == HX_OS_OK)
		status = hx_os_lookup_validate(translated,
			lookup->max_text_bytes, &value_bytes, error);

	if (status != HX_OS_OK)
		return status;

	if (!hx_os_i_add_size(key_bytes, value_bytes, &total) ||
		!hx_os_i_add_size(total, 2, &total))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	index = hx_os_lookup_index(lookup, fallback);
	replace = index < lookup->count &&
		!strcmp(lookup->entries[index].data, fallback);
	old_bytes = replace ? lookup->entries[index].total_bytes : 0;
	if ((!replace && lookup->count == lookup->max_entries) ||
		total > lookup->max_text_bytes -
				(lookup->text_bytes - old_bytes))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	text = hx_os_i_alloc(lookup->context, total, 0);
	if (!text)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	memcpy(text, fallback, key_bytes + 1);
	memcpy(text + key_bytes + 1, translated, value_bytes + 1);
	if (!replace && lookup->count == lookup->capacity) {
		capacity = lookup->capacity ? lookup->capacity : 8;
		if (capacity <= lookup->max_entries / 2)
			capacity *= 2;
		else
			capacity = lookup->max_entries;

		entries = hx_os_i_alloc(
			lookup->context, capacity * sizeof(*entries), 0);
		if (!entries) {
			hx_os_memory_free(text);

			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		}

		if (lookup->count)
			memcpy(entries, lookup->entries,
				lookup->count * sizeof(*entries));

		hx_os_memory_free(lookup->entries);
		lookup->entries = entries;
		lookup->capacity = capacity;
	}

	if (replace)
		hx_os_memory_free(lookup->entries[index].data);
	else {
		memmove(lookup->entries + index + 1, lookup->entries + index,
			(lookup->count - index) * sizeof(*lookup->entries));
		++lookup->count;
	}

	lookup->entries[index].data = text;
	lookup->entries[index].key_bytes = key_bytes;
	lookup->entries[index].total_bytes = total;
	lookup->text_bytes = lookup->text_bytes - old_bytes + total;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)lookup;
	(void)fallback;
	(void)translated;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_text_lookup_get_view(const hx_os_text_lookup *lookup,
	const char *fallback, hx_string_view *out, hx_os_error *error)
{
#if HX_OS_ENABLE_TEXT_LOOKUP
	const char *text;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_lookup_value(lookup, fallback, &text, error);
	if (status != HX_OS_OK)
		return status;

	out->data = text;
	out->size = strlen(text);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)lookup;
	(void)fallback;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_text_lookup_get(const hx_os_text_lookup *lookup,
	const char *fallback, char *buffer, size_t capacity, size_t *required,
	hx_os_error *error)
{
#if HX_OS_ENABLE_TEXT_LOOKUP
	const char *text;
	hx_os_status status;

	if (!required || (!buffer && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_lookup_value(lookup, fallback, &text, error);
	if (status != HX_OS_OK)
		return status;

	*required = strlen(text) + 1;
	if (capacity < *required)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

	memcpy(buffer, text, *required);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)lookup;
	(void)fallback;
	(void)buffer;
	(void)capacity;
	(void)required;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_text_lookup_get_string(const hx_os_text_lookup *lookup,
	const char *fallback, hx_string *out, hx_os_error *error)
{
#if HX_OS_ENABLE_TEXT_LOOKUP
	const char *text;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_lookup_value(lookup, fallback, &text, error);

	return status == HX_OS_OK ? hx_string_assign_cstr(out, text, error)
				  : status;
#else
	(void)lookup;
	(void)fallback;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

#if HX_OS_ENABLE_TEXT_LOOKUP && HX_OS_ENABLE_LEXER
static hx_string_view hx_os_lookup_trim(hx_string_view text)
{
	while (text.size && (text.data[0] == ' ' || text.data[0] == '\t')) {
		++text.data;
		--text.size;
	}

	while (text.size &&
		(text.data[text.size - 1] == ' ' ||
			text.data[text.size - 1] == '\t'))
		--text.size;
	return text;
}

/**
 * \brief Share quoted-string validation/decoding with the lexer.
 *
 * \details Unquoted catalog values are plain text, including apostrophes and
 * numeric-looking words.
 */
static hx_os_status hx_os_lookup_quoted(
	hx_string_view *line, hx_string *out, hx_os_error *error)
{
	hx_os_token token;
	size_t i = 1;
	char quote = line->data[0];
	hx_os_status status;

	while (i < line->size) {
		char c = line->data[i++];

		if (c == quote)
			break;

		if (c == '\\' && i < line->size)
			++i;
	}

	memset(&token, 0, sizeof(token));
	token.kind = HX_OS_TOKEN_STRING;
	token.text.data = line->data;
	token.text.size = i;

	status = hx_os_token_string(&token, out, error);
	if (status == HX_OS_OK) {
		line->data += i;
		line->size -= i;
		*line = hx_os_lookup_trim(*line);
	}

	return status;
}

static hx_os_status hx_os_lookup_parse(
	hx_os_text_lookup *lookup, hx_string_view input, hx_os_error *error)
{
	hx_string key, value;
	size_t offset = 0, invalid;
	hx_os_status status =
		hx_os_text_validate_utf8(input.data, input.size, &invalid);

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	if (input.size && memchr(input.data, 0, input.size))
		return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);

	if (input.size >= 3 && !memcmp(input.data, "\xef\xbb\xbf", 3))
		offset = 3;

	hx_string_init(&key, lookup->context);
	hx_string_init(&value, lookup->context);

	while (offset < input.size && status == HX_OS_OK) {
		size_t begin = offset, i;
		hx_string_view line;

		while (offset < input.size && input.data[offset] != '\r' &&
			input.data[offset] != '\n')
			++offset;
		line.data = input.data + begin;
		line.size = offset - begin;
		if (offset < input.size)
			++offset;

		line = hx_os_lookup_trim(line);
		if (!line.size || line.data[0] == '#')
			continue;

		if (line.data[0] == '[') {
			status = HX_OS_SYNTAX;
			break;
		}

		if (line.data[0] == '"' || line.data[0] == '\'')
			status = hx_os_lookup_quoted(&line, &key, error);
		else {
			hx_string_view raw = line;

			for (i = 0; i < line.size && line.data[i] != '=' &&
				line.data[i] != '#';
				++i) {
			}

			raw.size = i;
			raw = hx_os_lookup_trim(raw);

			status = hx_string_assign(&key, raw, error);
			line.data += i;
			line.size -= i;
		}

		if (status != HX_OS_OK)
			break;

		if (!key.size || memchr(key.data, 0, key.size) || !line.size ||
			line.data[0] != '=') {
			status = HX_OS_SYNTAX;
			break;
		}

		++line.data;
		--line.size;
		line = hx_os_lookup_trim(line);
		if (line.size &&
			(line.data[0] == '"' || line.data[0] == '\'')) {
			status = hx_os_lookup_quoted(&line, &value, error);
			if (status == HX_OS_OK && line.size &&
				line.data[0] != '#')
				status = HX_OS_SYNTAX;
		}
		else {
			for (i = 0; i < line.size && line.data[i] != '#'; ++i) {
			}

			line.size = i;
			line = hx_os_lookup_trim(line);

			status = hx_string_assign(&value, line, error);
		}

		if (status == HX_OS_OK && value.size &&
			memchr(value.data, 0, value.size))
			status = HX_OS_SYNTAX;
		if (status == HX_OS_OK)
			status = hx_os_text_lookup_add(lookup,
				hx_string_c_str(&key), hx_string_c_str(&value),
				error);
	}

	hx_string_destroy(&key);
	hx_string_destroy(&value);

	return hx_os_i_error(error, status, 0, 0);
}
#endif
hx_os_status hx_os_text_lookup_add_text(
	hx_os_text_lookup *lookup, hx_string_view text, hx_os_error *error)
{
#if HX_OS_ENABLE_TEXT_LOOKUP && HX_OS_ENABLE_LEXER
	hx_os_text_lookup_options options = HX_OS_TEXT_LOOKUP_OPTIONS_INIT;
	hx_os_text_lookup *candidate = NULL;
	hx_os_status status;
	size_t i;

	if (!lookup)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (text.size > lookup->max_text_bytes)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	options.max_entries = lookup->max_entries;
	options.max_text_bytes = lookup->max_text_bytes;

	status = hx_os_text_lookup_create_with_options(
		lookup->context, &options, &candidate, error);

	for (i = 0; status == HX_OS_OK && i < lookup->count; ++i)
		status = hx_os_text_lookup_add(candidate,
			lookup->entries[i].data,
			lookup->entries[i].data + lookup->entries[i].key_bytes +
				1,
			error);

	if (status == HX_OS_OK)
		status = hx_os_lookup_parse(candidate, text, error);

	if (status == HX_OS_OK) {
		hx_os_text_lookup old = *lookup;

		*lookup = *candidate;
		*candidate = old;
	}

	hx_os_text_lookup_destroy(&candidate);

	return status;
#else
	(void)lookup;
	(void)text;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_text_lookup_add_file(
	hx_os_text_lookup *lookup, const char *path, hx_os_error *error)
{
#if HX_OS_ENABLE_TEXT_LOOKUP && HX_OS_ENABLE_LEXER && HX_OS_ENABLE_FILE_UTILS
	hx_string text;
	hx_os_status status;

	if (!lookup)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	hx_string_init(&text, lookup->context);

	status = hx_os_file_load_utf8(
		lookup->context, path, lookup->max_text_bytes, &text, 1, error);
	if (status == HX_OS_OK)
		status = hx_os_text_lookup_add_text(
			lookup, hx_string_view_of(&text), error);

	hx_string_destroy(&text);

	return status;
#else
	(void)lookup;
	(void)path;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_text_lookup_load(hx_os_context *context, const char *path,
	hx_os_text_lookup **out, hx_os_error *error)
{
	hx_os_text_lookup *lookup = NULL;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;

	status = hx_os_text_lookup_create(context, &lookup, error);
	if (status == HX_OS_OK)
		status = hx_os_text_lookup_add_file(lookup, path, error);

	if (status == HX_OS_OK)
		*out = lookup;
	else
		hx_os_text_lookup_destroy(&lookup);

	return status;
}

void hx_os_text_lookup_destroy(hx_os_text_lookup **lookup)
{
	size_t i;

	if (!lookup || !*lookup)
		return;

	for (i = 0; i < (*lookup)->count; ++i)
		hx_os_memory_free((*lookup)->entries[i].data);

	hx_os_memory_free((*lookup)->entries);
	hx_os_memory_free(*lookup);
	*lookup = NULL;
}
