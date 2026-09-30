#include "containers_private.h"
#include "hx_string.h"

static hx_os_status hx_os_container_string_copy(
	void *destination, const void *source, void *user)
{
	hx_os_status status;
	hx_string_init(destination, user);
	status = hx_string_copy(destination, source, NULL);
	if (status != HX_OS_OK)
		hx_string_destroy(destination);
	return status;
}

static void hx_os_container_string_drop(void *value, void *user)
{
	(void)user;
	hx_string_destroy(value);
}

void hx_os_string_container_options_init(
	hx_os_container_options *options, hx_os_context *context)
{
	if (!options)
		return;
	hx_os_container_options_init(options, sizeof(hx_string));
	options->copy = hx_os_container_string_copy;
	options->drop = hx_os_container_string_drop;
	options->user = context;
}

void hx_os_container_options_init(
	hx_os_container_options *options, size_t element_size)
{
	if (!options)
		return;
	memset(options, 0, sizeof(*options));
	options->size = sizeof(*options);
	options->version = HX_OS_ABI_VERSION;
	options->element_size = element_size;
	options->max_items = 1024 * 1024;
	options->max_bytes = 16 * 1024 * 1024;
}

hx_os_status hx_os_i_container_init(hx_os_container_base *base,
	hx_os_context *context, const hx_os_container_options *options,
	size_t object_size, int set_allowed)
{
	size_t alignment;
	if (!options || options->size != sizeof(*options) ||
		options->version != HX_OS_ABI_VERSION ||
		(!options->element_size && !set_allowed) ||
		(!options->element_size && (options->copy || options->drop)) ||
		!options->max_items || !options->max_bytes ||
		!hx_os_i_alignment_valid(options->element_alignment))
		return HX_OS_INVALID;
	alignment = options->element_alignment;
	if (alignment && options->element_size % alignment)
		return HX_OS_INVALID;
	memset(base, 0, sizeof(*base));
	base->context = context;
	base->options = *options;
	base->memory = hx_os_i_allocation_cost(object_size, 0);
	if (!base->memory || base->memory > options->max_bytes)
		return HX_OS_LIMIT;
	return HX_OS_OK;
}

void *hx_os_i_container_alloc(
	hx_os_container_base *base, size_t bytes, size_t alignment)
{
	size_t cost = hx_os_i_allocation_cost(bytes, alignment);
	void *memory;
	if (!cost || cost > base->options.max_bytes - base->memory) {
		base->allocation_status = HX_OS_LIMIT;
		return NULL;
	}
	memory = hx_os_i_alloc(base->context, bytes, alignment);
	if (!memory)
		base->allocation_status = HX_OS_NO_MEMORY;
	else
		base->memory += cost;
	return memory;
}

void hx_os_i_container_free(hx_os_container_base *base, void *memory)
{
	size_t cost;
	if (!memory)
		return;
	cost = hx_os_i_allocation_bytes(memory);
	hx_os_memory_free(memory);
	base->memory -= cost;
}

hx_os_status hx_os_i_container_enter(hx_os_container_base *base)
{
	if (base->busy)
		return HX_OS_BUSY;
	base->busy = 1;
	base->allocation_status = HX_OS_OK;
	return HX_OS_OK;
}

hx_os_status hx_os_i_container_leave(
	hx_os_container_base *base, hx_os_status status, hx_os_error *error)
{
	base->busy = 0;
	return hx_os_i_error(error, status, 0, 0);
}

int hx_os_i_container_overlap(
	const void *a, size_t a_bytes, const void *b, size_t b_bytes)
{
	uintptr_t left = (uintptr_t)a, right = (uintptr_t)b;
	if (!a || !b || !a_bytes || !b_bytes)
		return 0;
	return left >= right ? left - right < b_bytes : right - left < a_bytes;
}

hx_os_status hx_os_i_container_copy(hx_os_container_base *base,
	void *destination, const void *source, int take)
{
	if (!base->options.element_size)
		return HX_OS_OK;
	if (!take && base->options.copy)
		return base->options.copy(
			destination, source, base->options.user);
	if (!take && base->options.drop)
		return HX_OS_NOT_SUPPORTED;
	memcpy(destination, source, base->options.element_size);
	return HX_OS_OK;
}

void hx_os_i_container_drop(hx_os_container_base *base, void *value)
{
	if (base->options.drop)
		base->options.drop(value, base->options.user);
}
