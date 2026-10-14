#include "config_private.h"
#include "number_private.h"

typedef union hx_os_cfg_vendor_block hx_os_cfg_vendor_block;
union hx_os_cfg_vendor_block {
	struct {
		hx_os_cfg_vendor_block *next;
		hx_os_cfg_vendor_block *previous;
		hx_os_cfg_parser *owner;
		size_t bytes;
		size_t cost;
	} fields;

	hx_os_i_alignment alignment;
};

// Calls are synchronous; scopes nest, and no destructor or TLS key is owned.
#if defined(_MSC_VER)
static __declspec(thread) hx_os_cfg_parser *hx_os_cfg_vendor_parser;
#elif defined(__GNUC__)
static __thread hx_os_cfg_parser *hx_os_cfg_vendor_parser;
#else
#error A compiler TLS adapter is required for optional configuration parsers
#endif

hx_os_cfg_parser *hx_os_cfg_vendor_enter(hx_os_cfg_parser *parser)
{
	hx_os_cfg_parser *previous = hx_os_cfg_vendor_parser;

	hx_os_cfg_vendor_parser = parser;

	return previous;
}

void hx_os_cfg_vendor_free(void *memory)
{
	hx_os_cfg_vendor_block *block;
	hx_os_cfg_parser *owner;
	size_t cost;

	if (!memory)
		return;

	block = (hx_os_cfg_vendor_block *)memory - 1;
	owner = block->fields.owner;
	cost = block->fields.cost;
	if (block->fields.previous)
		block->fields.previous->fields.next = block->fields.next;
	else
		owner->vendor_blocks = block->fields.next;

	if (block->fields.next)
		block->fields.next->fields.previous = block->fields.previous;

	hx_os_memory_free(block);
	owner->document->memory -= cost;
}

void hx_os_cfg_vendor_leave(
	hx_os_cfg_parser *parser, hx_os_cfg_parser *previous)
{
	while (parser->vendor_blocks)
		hx_os_cfg_vendor_free(
			(hx_os_cfg_vendor_block *)parser->vendor_blocks + 1);

	hx_os_cfg_vendor_parser = previous;
}

void *hx_os_cfg_vendor_malloc(size_t bytes)
{
	hx_os_cfg_parser *parser = hx_os_cfg_vendor_parser;
	hx_os_cfg_vendor_block *block;
	size_t total, cost;

	if (!parser || parser->status != HX_OS_OK)
		return NULL;

	if (!hx_os_i_add_size(sizeof(*block), bytes, &total))
		goto limit;

	cost = hx_os_i_allocation_cost(total, 0);
	if (!cost ||
		cost > parser->document->options.max_memory_bytes -
				parser->document->memory)
		goto limit;
	block = hx_os_i_alloc(parser->document->context, total, 0);
	if (!block) {
		(void)hx_os_cfg_fail(parser, HX_OS_NO_MEMORY, SIZE_MAX,
			"Configuration backend allocation failed");

		return NULL;
	}

	parser->document->memory += cost;
	block->fields.owner = parser;
	block->fields.bytes = bytes;
	block->fields.cost = cost;
	block->fields.previous = NULL;
	block->fields.next = parser->vendor_blocks;
	if (block->fields.next)
		block->fields.next->fields.previous = block;

	parser->vendor_blocks = block;

	return block + 1;

limit:
	(void)hx_os_cfg_fail(parser, HX_OS_LIMIT, SIZE_MAX,
		"Configuration backend memory limit exceeded");
	return NULL;
}

void *hx_os_cfg_vendor_calloc(size_t count, size_t bytes)
{
	void *memory;

	if (count && bytes > SIZE_MAX / count) {
		if (hx_os_cfg_vendor_parser)
			(void)hx_os_cfg_fail(hx_os_cfg_vendor_parser,
				HX_OS_LIMIT, SIZE_MAX,
				"Configuration backend size overflow");

		return NULL;
	}

	memory = hx_os_cfg_vendor_malloc(count * bytes);
	if (memory)
		memset(memory, 0, count * bytes);

	return memory;
}

void *hx_os_cfg_vendor_realloc(void *memory, size_t bytes)
{
	hx_os_cfg_vendor_block *block;
	void *replacement;

	if (!bytes) {
		hx_os_cfg_vendor_free(memory);

		return NULL;
	}

	if (!memory)
		return hx_os_cfg_vendor_malloc(bytes);

	block = (hx_os_cfg_vendor_block *)memory - 1;
	if (bytes <= block->fields.bytes) {
		block->fields.bytes = bytes;

		return memory;
	}

	replacement = hx_os_cfg_vendor_malloc(bytes);
	if (replacement) {
		memcpy(replacement, memory, block->fields.bytes);

		hx_os_cfg_vendor_free(memory);
	}

	return replacement;
}

char *hx_os_cfg_vendor_strdup(const char *text)
{
	size_t bytes = strlen(text) + 1;

	char *copy = hx_os_cfg_vendor_malloc(bytes);
	if (copy)
		memcpy(copy, text, bytes);

	return copy;
}

double hx_os_cfg_vendor_strtod(const char *text, char **end)
{
	double result;
	int native_error;
	hx_os_status status =
		hx_os_i_strtod_c(text, end, &result, &native_error);

	if (status != HX_OS_OK && hx_os_cfg_vendor_parser)
		(void)hx_os_cfg_fail(hx_os_cfg_vendor_parser, status, SIZE_MAX,
			status == HX_OS_NO_MEMORY
				? "Numeric locale allocation failed"
				: "A per-call numeric locale is unavailable");

	errno = native_error;

	return result;
}

int hx_os_cfg_vendor_isspace(int character)
{
	return character == ' ' || character == '\t' || character == '\r' ||
		character == '\n' || character == '\v' || character == '\f';
}
