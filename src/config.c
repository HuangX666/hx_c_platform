#include "config_private.h"

#include <locale.h>
#include <stdio.h>

struct hx_os_cfg_block {
	hx_os_cfg_block *next;
	hx_os_i_alignment alignment;
};

void hx_os_config_options_init(hx_os_config_options *options)
{
	if (!options)
		return;
	memset(options, 0, sizeof(*options));
	options->size = sizeof(*options);
	options->version = HX_OS_ABI_VERSION;
	options->max_input_bytes = 1024 * 1024;
	options->max_memory_bytes = 16 * 1024 * 1024;
	options->max_nodes = 65536;
	options->max_depth = 64;
	options->max_work = 16 * 1024 * 1024;
}

static void hx_os_cfg_error_init(hx_os_config_error *error, const char *source)
{
	size_t bytes = 0;
	if (!error)
		return;
	memset(error, 0, sizeof(*error));
	error->byte_offset = SIZE_MAX;
	if (!source)
		return;
	while (source[bytes] && bytes < sizeof(error->source) - 1) {
		size_t used = 0;
		size_t available = 0;
		uint32_t point;
		while (available < 4 && source[bytes + available])
			++available;
		if (hx_os_i_utf_decode(HX_OS_UTF8,
			    (const unsigned char *)source + bytes, available,
			    &point, &used) != HX_OS_OK ||
			bytes + used >= sizeof(error->source))
			break;
		memcpy(error->source + bytes, source + bytes, used);
		bytes += used;
	}
	error->source_truncated = source[bytes] != 0;
}

hx_os_status hx_os_cfg_fail(hx_os_cfg_parser *parser, hx_os_status status,
	size_t position, const char *message)
{
	hx_os_config_error *error = parser->error;
	size_t i;
	if (parser->status != HX_OS_OK)
		return parser->status;
	parser->status = status;
	if (!error)
		return status;
	(void)hx_os_i_error(&error->detail, status, HX_OS_ERROR_NONE, 0);
	(void)snprintf(error->message, sizeof(error->message), "%s", message);
	if (position == SIZE_MAX || !parser->text)
		return status;
	if (position > parser->length)
		position = parser->length;
	error->byte_offset = position;
	error->line = 1;
	error->column = 1;
	for (i = 0; i < position; ++i) {
		unsigned char c = (unsigned char)parser->text[i];
		if (c == '\r') {
			++error->line;
			error->column = 1;
			if (i + 1 < position && parser->text[i + 1] == '\n')
				++i;
		}
		else if (c == '\n') {
			++error->line;
			error->column = 1;
		}
		else if ((c & 0xc0) != 0x80)
			++error->column;
	}
	return status;
}

int hx_os_cfg_reject(
	hx_os_cfg_parser *parser, hx_os_status status, const char *message)
{
	(void)hx_os_cfg_fail(parser, status, parser->position, message);
	return 0;
}

int hx_os_cfg_work(hx_os_cfg_parser *parser, size_t amount)
{
	hx_os_config_document *doc = parser->document;
	if (parser->status != HX_OS_OK)
		return 0;
	if (amount > doc->options.max_work - doc->work)
		return hx_os_cfg_reject(parser, HX_OS_LIMIT,
			"Configuration work limit exceeded");
	doc->work += amount;
	return 1;
}

void *hx_os_cfg_alloc(hx_os_cfg_parser *parser, size_t bytes)
{
	hx_os_config_document *doc = parser->document;
	hx_os_cfg_block *block;
	size_t total, cost;
	if (parser->status != HX_OS_OK)
		return NULL;
	if (!hx_os_i_add_size(sizeof(*block), bytes, &total))
		goto limit;
	cost = hx_os_i_allocation_cost(total, 0);
	if (!cost || cost > doc->options.max_memory_bytes - doc->memory)
		goto limit;
	block = hx_os_i_alloc(doc->context, total, 0);
	if (!block) {
		(void)hx_os_cfg_reject(parser, HX_OS_NO_MEMORY,
			"Configuration allocation failed");
		return NULL;
	}
	doc->memory += cost;
	block->next = doc->blocks;
	doc->blocks = block;
	return block + 1;
limit:
	(void)hx_os_cfg_reject(
		parser, HX_OS_LIMIT, "Configuration memory limit exceeded");
	return NULL;
}

hx_os_config_slice hx_os_cfg_copy(
	hx_os_cfg_parser *parser, const char *data, size_t bytes)
{
	hx_os_config_slice result = {NULL, 0};
	char *copy;
	if (bytes == SIZE_MAX || !hx_os_cfg_work(parser, bytes))
		return result;
	copy = hx_os_cfg_alloc(parser, bytes + 1);
	if (copy) {
		if (bytes)
			memcpy(copy, data, bytes);
		copy[bytes] = 0;
		result.data = copy;
		result.bytes = bytes;
	}
	return result;
}

hx_os_config_node *hx_os_cfg_node(
	hx_os_cfg_parser *parser, int kind, size_t depth)
{
	hx_os_config_node *node;
	hx_os_config_document *doc = parser->document;
	if (depth > doc->options.max_depth ||
		doc->nodes == doc->options.max_nodes) {
		(void)hx_os_cfg_reject(parser, HX_OS_LIMIT,
			"Configuration depth or node limit exceeded");
		return NULL;
	}
	node = hx_os_cfg_alloc(parser, sizeof(*node));
	if (!node)
		return NULL;
	memset(node, 0, sizeof(*node));
	node->document = doc;
	node->kind = kind;
	node->depth = depth;
	++doc->nodes;
	return node;
}

hx_os_config_node *hx_os_cfg_lookup(hx_os_cfg_parser *parser,
	hx_os_config_node *object, hx_os_config_slice name)
{
	hx_os_config_node *node;
	for (node = object->first; node; node = node->next) {
		if (!hx_os_cfg_work(parser, 1 + name.bytes))
			return NULL;
		if (node->name.bytes == name.bytes &&
			!memcmp(node->name.data, name.data, name.bytes))
			return node;
	}
	return NULL;
}

int hx_os_cfg_append(hx_os_cfg_parser *parser, hx_os_config_node *parent,
	hx_os_config_node *child, int duplicates)
{
	hx_os_config_node *existing = NULL;
	if (!parent || !child || parser->status != HX_OS_OK)
		return 0;
	if (parent->kind == HX_OS_CONFIG_OBJECT)
		existing = hx_os_cfg_lookup(parser, parent, child->name);
	if (parser->status != HX_OS_OK)
		return 0;
	if (existing) {
		hx_os_config_node **link;
		if (duplicates == HX_OS_CONFIG_DUPLICATE_ERROR)
			return hx_os_cfg_reject(parser, HX_OS_EXISTS,
				"Duplicate configuration key");
		if (duplicates == HX_OS_CONFIG_DUPLICATE_FIRST)
			return 1;
		for (link = &parent->first; *link != existing;
			link = &(*link)->next) {
		}
		child->next = existing->next;
		*link = child;
		if (parent->last == existing)
			parent->last = child;
		return 1;
	}
	if (parent->last)
		parent->last->next = child;
	else
		parent->first = child;
	parent->last = child;
	++parent->count;
	return 1;
}

const char *hx_os_config_format_name(int format)
{
	static const char *const names[] = {
		"json", "yaml", "xml", "ini", "toml", "env", "properties"};
	return format >= HX_OS_CONFIG_JSON && format <= HX_OS_CONFIG_PROPERTIES
		? names[format]
		: "unknown";
}

int hx_os_config_backend_available(int format, int backend)
{
#if HX_OS_ENABLE_CONFIG
	if (format < HX_OS_CONFIG_JSON || format > HX_OS_CONFIG_PROPERTIES)
		return 0;
	switch (backend) {
	case HX_OS_CONFIG_BUILTIN:
		return 1;
	case HX_OS_CONFIG_CJSON:
		return HX_OS_HAVE_CJSON && format == HX_OS_CONFIG_JSON;
	case HX_OS_CONFIG_LIBYAML:
		return HX_OS_HAVE_LIBYAML && format == HX_OS_CONFIG_YAML;
	case HX_OS_CONFIG_MINIXML:
		return HX_OS_HAVE_MINIXML && format == HX_OS_CONFIG_XML;
	case HX_OS_CONFIG_INIH:
		return HX_OS_HAVE_INIH && format == HX_OS_CONFIG_INI;
	case HX_OS_CONFIG_TOMLC:
		return HX_OS_HAVE_TOMLC && format == HX_OS_CONFIG_TOML;
	default:
		return 0;
	}
#else
	(void)format;
	(void)backend;
	return 0;
#endif
}

static int hx_os_cfg_options(hx_os_cfg_parser *parser,
	const hx_os_config_options *input, hx_os_config_options *options)
{
	hx_os_config_options_init(options);
	if (input) {
		if (input->size != sizeof(*input) ||
			input->version != HX_OS_ABI_VERSION)
			return hx_os_cfg_reject(parser, HX_OS_INVALID,
				"Invalid configuration options version");
		*options = *input;
	}
	if (options->duplicates < HX_OS_CONFIG_DUPLICATE_ERROR ||
		options->duplicates > HX_OS_CONFIG_DUPLICATE_LAST ||
		!options->max_input_bytes || !options->max_memory_bytes ||
		!options->max_nodes || !options->max_depth ||
		options->max_depth > 128 || !options->max_work)
		return hx_os_cfg_reject(parser, HX_OS_INVALID,
			"Invalid configuration limits or duplicate policy");
	return 1;
}

static int hx_os_cfg_dispatch(hx_os_cfg_parser *parser, hx_os_config_node **out)
{
#if HX_OS_ENABLE_CONFIG
	switch (parser->document->options.backend) {
#if HX_OS_HAVE_CJSON
	case HX_OS_CONFIG_CJSON:
		return hx_os_cfg_cjson(parser, out);
#endif
#if HX_OS_HAVE_LIBYAML
	case HX_OS_CONFIG_LIBYAML:
		return hx_os_cfg_libyaml(parser, out);
#endif
#if HX_OS_HAVE_MINIXML
	case HX_OS_CONFIG_MINIXML:
		return hx_os_cfg_minixml(parser, out);
#endif
#if HX_OS_HAVE_INIH
	case HX_OS_CONFIG_INIH:
		return hx_os_cfg_inih(parser, out);
#endif
#if HX_OS_HAVE_TOMLC
	case HX_OS_CONFIG_TOMLC:
		return hx_os_cfg_tomlc(parser, out);
#endif
	default:
		break;
	}
	switch (parser->format) {
	case HX_OS_CONFIG_JSON:
		return hx_os_cfg_json(parser, out);
	case HX_OS_CONFIG_YAML:
		return hx_os_cfg_yaml(parser, out);
	case HX_OS_CONFIG_XML:
		return hx_os_cfg_xml(parser, out);
	case HX_OS_CONFIG_TOML:
		return hx_os_cfg_toml(parser, out);
	default:
		return hx_os_cfg_lines(parser, out);
	}
#else
	(void)out;
	return hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
		"Configuration module is disabled");
#endif
}

static int hx_os_cfg_input(hx_os_cfg_parser *parser, int backend)
{
	size_t i;
	for (i = 0; i < parser->length; ++i) {
		unsigned char c = (unsigned char)parser->text[i];
		if (parser->format == HX_OS_CONFIG_TOML && c == '\r' &&
			(i + 1 == parser->length ||
				parser->text[i + 1] != '\n')) {
			(void)hx_os_cfg_fail(parser, HX_OS_SYNTAX, i,
				"TOML requires LF or CRLF line endings");
			return 0;
		}
		if ((c < 0x20 && !hx_os_cfg_space(c) &&
			    !(parser->format == HX_OS_CONFIG_PROPERTIES &&
				    c == '\f')) ||
			(c == 0x7f &&
				(parser->format == HX_OS_CONFIG_YAML ||
					parser->format == HX_OS_CONFIG_TOML))) {
			(void)hx_os_cfg_fail(parser, HX_OS_SYNTAX, i,
				"Control character in configuration input");
			return 0;
		}
		if (parser->format == HX_OS_CONFIG_YAML &&
			backend == HX_OS_CONFIG_BUILTIN &&
			((c == 0xc2 && i + 1 < parser->length &&
				 (unsigned char)parser->text[i + 1] < 0xa0) ||
				(c == 0xe2 && i + 2 < parser->length &&
					(unsigned char)parser->text[i + 1] ==
						0x80 &&
					((unsigned char)parser->text[i + 2] ==
							0xa8 ||
						(unsigned char)parser->text[i +
							2] == 0xa9)))) {
			(void)hx_os_cfg_fail(parser, HX_OS_NOT_SUPPORTED, i,
				"YAML Unicode line/control character requires "
				"libyaml");
			return 0;
		}
	}
	return 1;
}

hx_os_status hx_os_config_parse(hx_os_context *context, int format,
	const void *input, size_t bytes, const hx_os_config_options *options,
	hx_os_config_document **out, hx_os_config_error *error)
{
	hx_os_cfg_parser parser;
	hx_os_config_options settings;
	hx_os_config_document *doc;
	size_t bad = 0, cost;
	memset(&parser, 0, sizeof(parser));
	parser.text = input ? (const char *)input : "";
	parser.length = bytes;
	parser.format = format;
	parser.error = error;
	hx_os_cfg_error_init(error,
		options && options->size == sizeof(*options)
			? options->source_name
			: NULL);
	if (out)
		*out = NULL;
	if (!out || (!input && bytes))
		return hx_os_cfg_fail(&parser, HX_OS_INVALID, SIZE_MAX,
			"Invalid configuration input");
	if (!hx_os_cfg_options(&parser, options, &settings))
		return parser.status;
	if (bytes > settings.max_input_bytes || bytes > SIZE_MAX - 2)
		return hx_os_cfg_fail(&parser, HX_OS_LIMIT, SIZE_MAX,
			"Configuration input limit exceeded");
	if (!hx_os_config_backend_available(format, settings.backend))
		return hx_os_cfg_fail(&parser, HX_OS_NOT_SUPPORTED, SIZE_MAX,
			"Configuration format or backend is unavailable");
	if (hx_os_text_validate_utf8(parser.text, bytes, &bad) != HX_OS_OK)
		return hx_os_cfg_fail(&parser, HX_OS_INVALID_ENCODING, bad,
			"Configuration input is not UTF-8");
	if (!hx_os_cfg_input(&parser, settings.backend))
		return parser.status;
	cost = hx_os_i_allocation_cost(sizeof(*doc), 0);
	if (cost > settings.max_memory_bytes)
		return hx_os_cfg_fail(&parser, HX_OS_LIMIT, SIZE_MAX,
			"Configuration memory limit exceeded");
	doc = hx_os_i_zero(context, sizeof(*doc));
	if (!doc)
		return hx_os_cfg_fail(&parser, HX_OS_NO_MEMORY, SIZE_MAX,
			"Configuration allocation failed");
	doc->context = context;
	doc->options = settings;
	doc->options.source_name = NULL;
	doc->memory = cost;
	doc->references.value = 1;
	parser.document = doc;
	if (bytes >= 3 && !memcmp(input, "\xef\xbb\xbf", 3))
		parser.position = 3;
	if (!hx_os_cfg_work(&parser, bytes) ||
		!hx_os_cfg_dispatch(&parser, &doc->root) ||
		parser.status != HX_OS_OK) {
		if (parser.status == HX_OS_OK)
			(void)hx_os_cfg_reject(&parser, HX_OS_SYNTAX,
				"Invalid configuration document");
		hx_os_config_release(doc);
		return parser.status;
	}
	*out = doc;
	return HX_OS_OK;
}

hx_os_status hx_os_config_load(hx_os_context *context, int format,
	const char *utf8_path, const hx_os_config_options *options,
	hx_os_config_document **out, hx_os_config_error *error)
{
	hx_os_cfg_parser parser;
	hx_os_config_options settings;
	hx_os_file *file = NULL;
	hx_os_error native, close_error;
	hx_os_status status, close_status;
	char *buffer = NULL;
	size_t capacity = 0, bytes = 0;
	memset(&parser, 0, sizeof(parser));
	parser.error = error;
	hx_os_cfg_error_init(error, utf8_path);
	if (out)
		*out = NULL;
	if (!out || !utf8_path)
		return hx_os_cfg_fail(&parser, HX_OS_INVALID, SIZE_MAX,
			"Invalid configuration path");
	if (!hx_os_cfg_options(&parser, options, &settings))
		return parser.status;
	if (!hx_os_config_backend_available(format, settings.backend))
		return hx_os_cfg_fail(&parser, HX_OS_NOT_SUPPORTED, SIZE_MAX,
			"Configuration format or backend is unavailable");
	status = hx_os_file_open(
		context, utf8_path, HX_OS_FILE_READ, &file, &native);
	while (status == HX_OS_OK) {
		size_t completed = 0;
		if (bytes == settings.max_input_bytes) {
			char extra;
			status = hx_os_file_read_some(
				file, &extra, 1, &completed, &native);
			if (completed)
				status = HX_OS_LIMIT;
			break;
		}
		if (bytes == capacity) {
			size_t next = capacity > settings.max_input_bytes / 2
				? settings.max_input_bytes
				: capacity * 2;
			char *replacement;
			if (!capacity)
				next = settings.max_input_bytes < 4096
					? settings.max_input_bytes
					: 4096;
			replacement = hx_os_i_alloc(context, next, 0);
			if (!replacement) {
				status = HX_OS_NO_MEMORY;
				break;
			}
			if (bytes)
				memcpy(replacement, buffer, bytes);
			hx_os_memory_free(buffer);
			buffer = replacement;
			capacity = next;
		}
		status = hx_os_file_read_some(file, buffer + bytes,
			capacity - bytes, &completed, &native);
		bytes += completed;
		if (status == HX_OS_OK && !completed)
			status = HX_OS_IO_ERROR;
	}
	close_status = hx_os_file_close(&file, &close_error);
	if (status == HX_OS_EOF) {
		status = close_status;
		if (close_status != HX_OS_OK)
			native = close_error;
	}
	if (status == HX_OS_OK) {
		settings.source_name = options && options->source_name
			? options->source_name
			: utf8_path;
		status = hx_os_config_parse(
			context, format, buffer, bytes, &settings, out, error);
	}
	else {
		(void)hx_os_cfg_fail(&parser, status, SIZE_MAX,
			"Configuration file could not be read within limits");
		if (error && native.status == status)
			error->detail = native;
	}
	hx_os_memory_free(buffer);
	return status;
}

void hx_os_config_retain(hx_os_config_document *document)
{
	if (document)
		(void)hx_os_atomic_fetch_add(
			&document->references, 1, HX_OS_RELAXED);
}

void hx_os_config_release(hx_os_config_document *document)
{
	hx_os_cfg_block *block;
	if (!document ||
		hx_os_atomic_fetch_add(
			&document->references, -1, HX_OS_ACQ_REL) != 1)
		return;
	block = document->blocks;
	while (block) {
		hx_os_cfg_block *next = block->next;
		hx_os_memory_free(block);
		block = next;
	}
	hx_os_memory_free(document);
}

const hx_os_config_node *hx_os_config_root(
	const hx_os_config_document *document)
{
	return document ? document->root : NULL;
}

int hx_os_config_kind(const hx_os_config_node *node)
{
	return node ? node->kind : -1;
}

hx_os_config_slice hx_os_config_name(const hx_os_config_node *node)
{
	hx_os_config_slice empty = {"", 0};
	return node && node->name.data ? node->name : empty;
}

size_t hx_os_config_count(const hx_os_config_node *node)
{
	return node ? node->count : 0;
}

const hx_os_config_node *hx_os_config_first(const hx_os_config_node *node)
{
	return node ? node->first : NULL;
}

const hx_os_config_node *hx_os_config_next(const hx_os_config_node *node)
{
	return node ? node->next : NULL;
}

const hx_os_config_node *hx_os_config_at(
	const hx_os_config_node *node, size_t index)
{
	node = hx_os_config_first(node);
	while (node && index--)
		node = node->next;
	return node;
}

hx_os_status hx_os_config_member(const hx_os_config_node *object,
	const char *key, size_t bytes, const hx_os_config_node **out)
{
	const hx_os_config_node *node;
	if (!out || (!key && bytes))
		return HX_OS_INVALID;
	*out = NULL;
	if (!object)
		return HX_OS_NOT_FOUND;
	if (object->kind != HX_OS_CONFIG_OBJECT)
		return HX_OS_TYPE_MISMATCH;
	for (node = object->first; node; node = node->next) {
		if (node->name.bytes == bytes &&
			(!bytes || !memcmp(node->name.data, key, bytes))) {
			*out = node;
			return HX_OS_OK;
		}
	}
	return HX_OS_NOT_FOUND;
}

const hx_os_config_node *hx_os_config_attributes(
	const hx_os_config_node *element)
{
	return element && element->kind == HX_OS_CONFIG_ELEMENT
		? element->attributes
		: NULL;
}

hx_os_status hx_os_config_attribute(const hx_os_config_node *element,
	const char *name, const hx_os_config_node **out)
{
	if (!name || !out)
		return HX_OS_INVALID;
	*out = NULL;
	if (!element)
		return HX_OS_NOT_FOUND;
	if (element->kind != HX_OS_CONFIG_ELEMENT)
		return HX_OS_TYPE_MISMATCH;
	return hx_os_config_member(
		element->attributes, name, strlen(name), out);
}

static int hx_os_cfg_pointer_key(
	hx_os_config_slice key, const char *token, size_t bytes)
{
	size_t i = 0, j = 0;
	while (i < bytes) {
		char c = token[i++];
		if (c == '~')
			c = token[i++] == '0' ? '~' : '/';
		if (j == key.bytes || key.data[j++] != c)
			return 0;
	}
	return j == key.bytes;
}

hx_os_status hx_os_config_pointer(const hx_os_config_node *root,
	const char *utf8_pointer, const hx_os_config_node **out)
{
	const char *path = utf8_pointer;
	size_t bad;
	if (!out || !path)
		return HX_OS_INVALID;
	*out = NULL;
	if (hx_os_text_validate_utf8(path, strlen(path), &bad) != HX_OS_OK)
		return HX_OS_INVALID_ENCODING;
	while (*path) {
		const char *token;
		size_t bytes = 0, i;
		if (*path++ != '/')
			return HX_OS_INVALID;
		token = path;
		while (*path && *path != '/')
			++path;
		bytes = (size_t)(path - token);
		for (i = 0; i < bytes; ++i)
			if (token[i] == '~' &&
				(++i == bytes ||
					(token[i] != '0' && token[i] != '1')))
				return HX_OS_INVALID;
		if (!root)
			return HX_OS_NOT_FOUND;
		if (root->kind == HX_OS_CONFIG_OBJECT) {
			root = root->first;
			while (root &&
				!hx_os_cfg_pointer_key(
					root->name, token, bytes))
				root = root->next;
		}
		else if (root->kind == HX_OS_CONFIG_ARRAY) {
			size_t index = 0;
			if (!bytes || (bytes > 1 && token[0] == '0'))
				return HX_OS_INVALID;
			for (i = 0; i < bytes; ++i) {
				unsigned digit = (unsigned char)token[i] - '0';
				if (digit > 9)
					return HX_OS_INVALID;
				if (index > (SIZE_MAX - digit) / 10)
					return HX_OS_RANGE;
				index = index * 10 + digit;
			}
			root = hx_os_config_at(root, index);
		}
		else
			return HX_OS_TYPE_MISMATCH;
	}
	*out = root;
	return root ? HX_OS_OK : HX_OS_NOT_FOUND;
}

hx_os_status hx_os_config_scalar_text(
	const hx_os_config_node *node, hx_os_config_slice *out)
{
	if (!out)
		return HX_OS_INVALID;
	if (!node)
		return HX_OS_NOT_FOUND;
	if (!node->value.data)
		return HX_OS_TYPE_MISMATCH;
	*out = node->value;
	return HX_OS_OK;
}

hx_os_status hx_os_config_get_string(
	const hx_os_config_node *node, hx_os_config_slice *out)
{
	if (!out)
		return HX_OS_INVALID;
	if (!node)
		return HX_OS_NOT_FOUND;
	if (node->kind != HX_OS_CONFIG_STRING &&
		node->kind != HX_OS_CONFIG_ATTRIBUTE &&
		node->kind != HX_OS_CONFIG_TEXT &&
		node->kind != HX_OS_CONFIG_DATETIME)
		return HX_OS_TYPE_MISMATCH;
	*out = node->value;
	return HX_OS_OK;
}

static hx_os_status hx_os_cfg_numeric(
	const hx_os_config_node *node, hx_os_config_slice *out)
{
	if (!node)
		return HX_OS_NOT_FOUND;
	if (node->kind != HX_OS_CONFIG_INTEGER &&
		node->kind != HX_OS_CONFIG_REAL &&
		node->kind != HX_OS_CONFIG_STRING &&
		node->kind != HX_OS_CONFIG_ATTRIBUTE)
		return HX_OS_TYPE_MISMATCH;
	*out = node->value;
	return HX_OS_OK;
}

hx_os_status hx_os_config_get_i64(const hx_os_config_node *node, int64_t *out)
{
	hx_os_config_slice value;
	uint64_t magnitude;
	int negative;
	hx_os_status status;
	if (!out)
		return HX_OS_INVALID;
	status = hx_os_cfg_numeric(node, &value);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_cfg_integer(value, &magnitude, &negative);
	if (status != HX_OS_OK)
		return status;
	if (magnitude > (uint64_t)INT64_MAX + (unsigned)negative)
		return HX_OS_RANGE;
	*out = negative ? -(int64_t)(magnitude ? magnitude - 1 : 0) -
			(magnitude ? 1 : 0)
			: (int64_t)magnitude;
	return HX_OS_OK;
}

hx_os_status hx_os_config_get_u64(const hx_os_config_node *node, uint64_t *out)
{
	hx_os_config_slice value;
	uint64_t magnitude;
	int negative;
	hx_os_status status;
	if (!out)
		return HX_OS_INVALID;
	status = hx_os_cfg_numeric(node, &value);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_cfg_integer(value, &magnitude, &negative);
	if (status != HX_OS_OK)
		return status;
	if (negative && magnitude)
		return HX_OS_RANGE;
	*out = magnitude;
	return HX_OS_OK;
}

hx_os_status hx_os_config_get_double(const hx_os_config_node *node, double *out)
{
#if HX_OS_HAVE_LOCAL_STRTOD
	hx_os_config_slice value;
	hx_os_status status;
	char *copy, *end;
	size_t i, n = 0;
	int kind, conversion_error;
	double result;
#if defined(_WIN32)
	_locale_t locale;
#else
	locale_t locale;
#endif
	if (!out)
		return HX_OS_INVALID;
	status = hx_os_cfg_numeric(node, &value);
	if (status != HX_OS_OK)
		return status;
	if (!hx_os_cfg_number(value.data, value.bytes, 1, &kind))
		return HX_OS_TYPE_MISMATCH;
	if (kind == HX_OS_CONFIG_INTEGER) {
		uint64_t magnitude;
		int negative;
		status = hx_os_cfg_integer(value, &magnitude, &negative);
		if (status == HX_OS_OK) {
			*out = negative ? -(double)magnitude
					: (double)magnitude;
			return HX_OS_OK;
		}
	}
	copy = hx_os_i_alloc(node->document->context, value.bytes + 1, 0);
	if (!copy)
		return HX_OS_NO_MEMORY;
	for (i = 0; i < value.bytes; ++i)
		if (value.data[i] != '_')
			copy[n++] = value.data[i];
	copy[n] = 0;
#if defined(_WIN32)
	locale = _create_locale(LC_NUMERIC, "C");
#else
	locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
#endif
	if (!locale) {
		hx_os_memory_free(copy);
		return HX_OS_NO_MEMORY;
	}
	errno = 0;
#if defined(_WIN32)
	result = _strtod_l(copy, &end, locale);
#else
	result = strtod_l(copy, &end, locale);
#endif
	conversion_error = errno;
	status = end != copy + n	     ? HX_OS_TYPE_MISMATCH
		: conversion_error == ERANGE ? HX_OS_RANGE
					     : HX_OS_OK;
#if defined(_WIN32)
	_free_locale(locale);
#else
	freelocale(locale);
#endif
	hx_os_memory_free(copy);
	if (status == HX_OS_OK)
		*out = result;
	return status;
#else
	if (!out)
		return HX_OS_INVALID;
	return node ? HX_OS_NOT_SUPPORTED : HX_OS_NOT_FOUND;
#endif
}

hx_os_status hx_os_config_get_bool(const hx_os_config_node *node, int *out)
{
	if (!out)
		return HX_OS_INVALID;
	if (!node)
		return HX_OS_NOT_FOUND;
	if (node->kind != HX_OS_CONFIG_BOOL &&
		node->kind != HX_OS_CONFIG_STRING &&
		node->kind != HX_OS_CONFIG_ATTRIBUTE)
		return HX_OS_TYPE_MISMATCH;
	if (hx_os_cfg_equal(node->value, "true"))
		*out = 1;
	else if (hx_os_cfg_equal(node->value, "false"))
		*out = 0;
	else
		return HX_OS_TYPE_MISMATCH;
	return HX_OS_OK;
}
