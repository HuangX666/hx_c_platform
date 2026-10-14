#include "internal.h"

#define HX_OS_POOL_CLASSES 11
#define HX_OS_POOL_MIN_SLOT 64

typedef struct hx_os_pool_page hx_os_pool_page;
typedef struct hx_os_pool_chunk {
	hx_os_pool_page *page;
	struct hx_os_pool_chunk *next;
	size_t requested;
	size_t alignment;
} hx_os_pool_chunk;

struct hx_os_pool_page {
	hx_os_pool_page *next;
	hx_os_pool_chunk *free_list;
	size_t cost;
	size_t stride;
	size_t count;
	size_t available;
	size_t index;
};

struct hx_os_memory_pool {
	hx_os_context *context;
	hx_os_mutex *mutex;
	hx_os_memory_pool_options options;
	hx_os_memory_stats stats;
	hx_os_pool_page *pages;
	size_t cached;
	size_t operations;
	int closing;
};

static size_t hx_os_pool_header_bytes(void)
{
	return (sizeof(hx_os_pool_page) + HX_OS_I_ALIGNMENT - 1) &
		~(HX_OS_I_ALIGNMENT - 1);
}

void hx_os_memory_pool_options_init(hx_os_memory_pool_options *options)
{
	memset(options, 0, sizeof(*options));
	options->size = sizeof(*options);
	options->version = HX_OS_ABI_VERSION;
	options->block_bytes = 65536;
	options->cached_bytes = 262144;
}

hx_os_status hx_os_memory_pool_create(hx_os_context *context,
	const hx_os_memory_pool_options *input, hx_os_memory_pool **out,
	hx_os_error *error)
{
	hx_os_memory_pool_options options;
	hx_os_memory_pool *pool;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	hx_os_memory_pool_options_init(&options);
	if (input) {
		if (input->size < sizeof(*input) ||
			input->version != HX_OS_ABI_VERSION ||
			!input->block_bytes)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		options = *input;
	}

	pool = hx_os_i_zero(context, sizeof(*pool));
	if (!pool)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	status = hx_os_mutex_create(context, 0, &pool->mutex, error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(pool);

		return status;
	}

	pool->context = context;
	pool->options = options;
	pool->stats.reserved_bytes = hx_os_i_allocation_bytes(pool) +
		hx_os_i_allocation_bytes(pool->mutex);
	pool->stats.peak_reserved_bytes = pool->stats.reserved_bytes;
	if (options.maximum_bytes &&
		pool->stats.reserved_bytes > options.maximum_bytes) {
		hx_os_mutex_destroy(pool->mutex);
		hx_os_memory_free(pool);

		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}

	*out = pool;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_pool_page_create(hx_os_memory_pool *pool,
	size_t stride, size_t index, hx_os_pool_page **out)
{
	hx_os_pool_page *page;
	size_t count = index < HX_OS_POOL_CLASSES
		? pool->options.block_bytes / stride
		: 1;
	size_t bytes;
	size_t cost;
	size_t i;
	unsigned char *data;

	if (!count)
		count = 1;

	if (count > 1024)
		count = 1024;

	if (stride > (SIZE_MAX - hx_os_pool_header_bytes()) / count)
		return HX_OS_RANGE;

	bytes = hx_os_pool_header_bytes() + stride * count;
	cost = hx_os_i_allocation_cost(bytes, 0);
	if (!cost || cost > SIZE_MAX - pool->stats.reserved_bytes)
		return HX_OS_RANGE;

	if (pool->options.maximum_bytes &&
		cost > pool->options.maximum_bytes - pool->stats.reserved_bytes)
		return HX_OS_LIMIT;
	// Reserve under the lock, allocate outside it. The operation lease
	// prevents destruction while an allocator callback is in flight.
	pool->stats.reserved_bytes += cost;
	if (pool->stats.reserved_bytes > pool->stats.peak_reserved_bytes)
		pool->stats.peak_reserved_bytes = pool->stats.reserved_bytes;

	++pool->operations;
	(void)hx_os_mutex_unlock(pool->mutex, NULL);
	page = hx_os_i_zero(pool->context, bytes);
	(void)hx_os_mutex_lock(pool->mutex, NULL);
	--pool->operations;
	if (!page) {
		pool->stats.reserved_bytes -= cost;

		return HX_OS_NO_MEMORY;
	}

	page->cost = cost;
	page->stride = stride;
	page->count = count;
	page->available = count;
	page->index = index;
	data = (unsigned char *)page + hx_os_pool_header_bytes();

	for (i = 0; i < count; ++i) {
		hx_os_pool_chunk *chunk =
			(hx_os_pool_chunk *)(data + i * stride);

		chunk->page = page;
		chunk->next = page->free_list;
		page->free_list = chunk;
	}

	page->next = pool->pages;
	pool->pages = page;
	pool->cached += cost;
	*out = page;

	return HX_OS_OK;
}

hx_os_status hx_os_memory_pool_alloc(hx_os_memory_pool *pool, size_t bytes,
	size_t alignment, void **out, hx_os_error *error)
{
	size_t needed;
	size_t stride = HX_OS_POOL_MIN_SLOT;
	size_t index = 0;
	hx_os_pool_page *page;
	hx_os_pool_chunk *chunk;
	hx_os_status status = HX_OS_OK;
	uintptr_t address;

	if (!pool || !out || !hx_os_i_alignment_valid(alignment))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (alignment < HX_OS_I_ALIGNMENT)
		alignment = HX_OS_I_ALIGNMENT;

	if (!hx_os_i_add_size(bytes ? bytes : 1, sizeof(*chunk) + sizeof(chunk),
		    &needed) ||
		!hx_os_i_add_size(needed, alignment - 1, &needed))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	while (stride < needed && index < HX_OS_POOL_CLASSES) {
		stride *= 2;
		++index;
	}

	if (index == HX_OS_POOL_CLASSES)
		stride = needed;

	(void)hx_os_mutex_lock(pool->mutex, NULL);
	if (pool->closing) {
		(void)hx_os_mutex_unlock(pool->mutex, NULL);

		return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);
	}

	if (bytes > SIZE_MAX - pool->stats.requested_bytes) {
		(void)hx_os_mutex_unlock(pool->mutex, NULL);

		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	}

	for (page = pool->pages; page; page = page->next)
		if (page->index == index && page->stride == stride &&
			page->available)
			break;

	if (!page)
		status = hx_os_pool_page_create(pool, stride, index, &page);

	if (status == HX_OS_OK) {
		if (page->available == page->count)
			pool->cached -= page->cost;

		chunk = page->free_list;
		page->free_list = chunk->next;
		--page->available;
		chunk->requested = bytes;
		chunk->alignment = alignment;
		address = (uintptr_t)(chunk + 1) + sizeof(chunk);
		address =
			(address + alignment - 1) & ~(uintptr_t)(alignment - 1);
		((hx_os_pool_chunk **)address)[-1] = chunk;
		*out = (void *)address;
		++pool->stats.live_allocations;
		pool->stats.requested_bytes += bytes;
	}

	(void)hx_os_mutex_unlock(pool->mutex, NULL);

	return hx_os_i_error(error, status, 0, 0);
}

static void hx_os_pool_unlink_page(
	hx_os_memory_pool *pool, hx_os_pool_page *page)
{
	hx_os_pool_page **cursor = &pool->pages;

	while (*cursor != page)
		cursor = &(*cursor)->next;

	*cursor = page->next;
	pool->cached -= page->cost;
}

void hx_os_memory_pool_free(hx_os_memory_pool *pool, void *memory)
{
	hx_os_pool_chunk *chunk;
	hx_os_pool_page *page;
	size_t released_cost = 0;
	int release = 0;

	if (!memory)
		return;

	chunk = ((hx_os_pool_chunk **)memory)[-1];
	page = chunk->page;
	(void)hx_os_mutex_lock(pool->mutex, NULL);
	--pool->stats.live_allocations;
	pool->stats.requested_bytes -= chunk->requested;
	chunk->next = page->free_list;
	page->free_list = chunk;
	++page->available;
	if (page->available == page->count) {
		pool->cached += page->cost;
		if (page->index == HX_OS_POOL_CLASSES ||
			pool->cached > pool->options.cached_bytes) {
			hx_os_pool_unlink_page(pool, page);
			released_cost = page->cost;
			++pool->operations;
			release = 1;
		}
	}

	(void)hx_os_mutex_unlock(pool->mutex, NULL);
	if (release) {
		hx_os_memory_free(page);
		(void)hx_os_mutex_lock(pool->mutex, NULL);
		pool->stats.reserved_bytes -= released_cost;
		--pool->operations;
		(void)hx_os_mutex_unlock(pool->mutex, NULL);
	}
}

hx_os_status hx_os_memory_pool_realloc(hx_os_memory_pool *pool, void **memory,
	size_t bytes, hx_os_error *error)
{
	hx_os_pool_chunk *chunk;
	void *replacement;
	size_t old_bytes;
	hx_os_status status;

	if (!pool || !memory)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!*memory)
		return hx_os_memory_pool_alloc(pool, bytes, 0, memory, error);

	if (!bytes) {
		hx_os_memory_pool_free(pool, *memory);
		*memory = NULL;

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}

	chunk = ((hx_os_pool_chunk **)*memory)[-1];
	old_bytes = chunk->requested;
	if (bytes <= chunk->page->stride -
			(size_t)((unsigned char *)*memory -
				(unsigned char *)chunk)) {
		(void)hx_os_mutex_lock(pool->mutex, NULL);
		pool->stats.requested_bytes -= old_bytes;
		pool->stats.requested_bytes += bytes;
		chunk->requested = bytes;
		(void)hx_os_mutex_unlock(pool->mutex, NULL);

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}

	status = hx_os_memory_pool_alloc(
		pool, bytes, chunk->alignment, &replacement, error);
	if (status != HX_OS_OK)
		return status;

	memcpy(replacement, *memory, old_bytes < bytes ? old_bytes : bytes);

	hx_os_memory_pool_free(pool, *memory);
	*memory = replacement;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_memory_pool_collect(hx_os_memory_pool *pool)
{
	hx_os_pool_page *page;
	hx_os_pool_page *retired = NULL;
	size_t released_cost = 0;

	(void)hx_os_mutex_lock(pool->mutex, NULL);
	++pool->operations;
	page = pool->pages;

	while (page) {
		hx_os_pool_page *next = page->next;

		if (page->available == page->count) {
			hx_os_pool_unlink_page(pool, page);
			page->next = retired;
			retired = page;
		}

		page = next;
	}

	(void)hx_os_mutex_unlock(pool->mutex, NULL);

	while (retired) {
		page = retired->next;
		released_cost += retired->cost;

		hx_os_memory_free(retired);
		retired = page;
	}

	(void)hx_os_mutex_lock(pool->mutex, NULL);
	pool->stats.reserved_bytes -= released_cost;
	--pool->operations;
	(void)hx_os_mutex_unlock(pool->mutex, NULL);
}

void hx_os_memory_pool_stats(hx_os_memory_pool *pool, hx_os_memory_stats *stats)
{
	(void)hx_os_mutex_lock(pool->mutex, NULL);
	*stats = pool->stats;
	(void)hx_os_mutex_unlock(pool->mutex, NULL);
}

hx_os_status hx_os_memory_pool_destroy(
	hx_os_memory_pool **pool, hx_os_error *error)
{
	hx_os_memory_pool *value;
	hx_os_pool_page *page;

	if (!pool)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	value = *pool;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	(void)hx_os_mutex_lock(value->mutex, NULL);
	if (value->closing || value->stats.live_allocations ||
		value->operations) {
		(void)hx_os_mutex_unlock(value->mutex, NULL);

		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	}

	value->closing = 1;
	page = value->pages;
	value->pages = NULL;
	*pool = NULL;
	(void)hx_os_mutex_unlock(value->mutex, NULL);

	while (page) {
		hx_os_pool_page *next = page->next;

		hx_os_memory_free(page);
		page = next;
	}

	hx_os_mutex_destroy(value->mutex);
	hx_os_memory_free(value);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
