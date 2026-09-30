#include "containers_private.h"

static void *hx_os_map_hash_alloc(size_t bytes);
static void hx_os_map_hash_free(void *memory, size_t bytes);
static void hx_os_map_hash_oom(void *node);
#define HASH_NONFATAL_OOM 1
#define uthash_malloc(bytes) hx_os_map_hash_alloc(bytes)
#define uthash_free(memory, bytes) hx_os_map_hash_free(memory, bytes)
#define uthash_nonfatal_oom(node) hx_os_map_hash_oom(node)
#include <uthash.h>

typedef struct hx_os_map_node {
	struct hx_os_map_node *previous;
	struct hx_os_map_node *next;
	struct hx_os_map_node *left;
	struct hx_os_map_node *right;
	struct hx_os_map_node *parent;
	UT_hash_handle hh;
	const void *key;
	void *value;
	size_t key_bytes;
	int height;
	int failed;
} hx_os_map_node;

struct hx_os_map {
	hx_os_container_base base;
	int kind;
	hx_os_key_compare_fn compare;
	size_t count;
	size_t value_offset;
	hx_os_map_node *first;
	hx_os_map_node *last;
	hx_os_map_node *root;
	hx_os_map_node *hash;
};

#if defined(_MSC_VER)
static __declspec(thread) hx_os_map *hx_os_hash_scope;
#else
static __thread hx_os_map *hx_os_hash_scope;
#endif

static void *hx_os_map_hash_alloc(size_t bytes)
{
	return hx_os_i_container_alloc(&hx_os_hash_scope->base, bytes, 0);
}

static void hx_os_map_hash_free(void *memory, size_t bytes)
{
	(void)bytes;
	hx_os_i_container_free(&hx_os_hash_scope->base, memory);
}

static void hx_os_map_hash_oom(void *node)
{
	((hx_os_map_node *)node)->failed = 1;
}

static int hx_os_map_compare(const hx_os_map *map, const void *a,
	size_t a_bytes, const void *b, size_t b_bytes)
{
	size_t bytes = a_bytes < b_bytes ? a_bytes : b_bytes;
	int order;
	if (map->compare)
		return map->compare(
			a, a_bytes, b, b_bytes, map->base.options.user);
	order = bytes ? memcmp(a, b, bytes) : 0;
	return order ? order : (a_bytes > b_bytes) - (a_bytes < b_bytes);
}

static hx_os_map_node *hx_os_map_lookup(const hx_os_map *map, const void *key,
	size_t bytes, hx_os_map_node **parent, hx_os_map_node **before,
	hx_os_map_node **after)
{
	hx_os_map_node *node = NULL;
	if (!key)
		key = "";
	if (map->kind == HX_OS_MAP_HASH) {
		HASH_FIND(hh, map->hash, key, (unsigned)bytes, node);
		return node;
	}
	node = map->root;
	while (node) {
		int comparison = hx_os_map_compare(
			map, key, bytes, node->key, node->key_bytes);
		if (!comparison)
			return node;
		if (parent)
			*parent = node;
		if (comparison < 0) {
			if (after)
				*after = node;
			node = node->left;
		}
		else {
			if (before)
				*before = node;
			node = node->right;
		}
	}
	return NULL;
}

static int hx_os_map_height(hx_os_map_node *node)
{
	return node ? node->height : 0;
}

static void hx_os_map_height_update(hx_os_map_node *node)
{
	int left = hx_os_map_height(node->left),
	    right = hx_os_map_height(node->right);
	node->height = (left > right ? left : right) + 1;
}

static void hx_os_map_replace(
	hx_os_map *map, hx_os_map_node *old, hx_os_map_node *replacement)
{
	if (!old->parent)
		map->root = replacement;
	else if (old->parent->left == old)
		old->parent->left = replacement;
	else
		old->parent->right = replacement;
	if (replacement)
		replacement->parent = old->parent;
}

static hx_os_map_node *hx_os_map_rotate(
	hx_os_map *map, hx_os_map_node *node, int left)
{
	hx_os_map_node *pivot = left ? node->right : node->left;
	hx_os_map_replace(map, node, pivot);
	if (left) {
		node->right = pivot->left;
		if (node->right)
			node->right->parent = node;
		pivot->left = node;
	}
	else {
		node->left = pivot->right;
		if (node->left)
			node->left->parent = node;
		pivot->right = node;
	}
	node->parent = pivot;
	hx_os_map_height_update(node);
	hx_os_map_height_update(pivot);
	return pivot;
}

static void hx_os_map_balance(hx_os_map *map, hx_os_map_node *node)
{
	while (node) {
		int difference;
		hx_os_map_height_update(node);
		difference = hx_os_map_height(node->left) -
			hx_os_map_height(node->right);
		if (difference > 1) {
			if (hx_os_map_height(node->left->left) <
				hx_os_map_height(node->left->right))
				(void)hx_os_map_rotate(map, node->left, 1);
			node = hx_os_map_rotate(map, node, 0);
		}
		else if (difference < -1) {
			if (hx_os_map_height(node->right->right) <
				hx_os_map_height(node->right->left))
				(void)hx_os_map_rotate(map, node->right, 0);
			node = hx_os_map_rotate(map, node, 1);
		}
		node = node->parent;
	}
}

static void hx_os_map_unlink_tree(hx_os_map *map, hx_os_map_node *node)
{
	hx_os_map_node *start;
	if (!node->left || !node->right) {
		start = node->parent;
		hx_os_map_replace(
			map, node, node->left ? node->left : node->right);
	}
	else {
		hx_os_map_node *successor = node->right;
		while (successor->left)
			successor = successor->left;
		if (successor->parent != node) {
			start = successor->parent;
			hx_os_map_replace(map, successor, successor->right);
			successor->right = node->right;
			successor->right->parent = successor;
		}
		else
			start = successor;
		hx_os_map_replace(map, node, successor);
		successor->left = node->left;
		successor->left->parent = successor;
		hx_os_map_height_update(successor);
	}
	hx_os_map_balance(map, start);
}

hx_os_status hx_os_map_create(hx_os_context *context, int kind,
	const hx_os_container_options *options, hx_os_key_compare_fn compare,
	hx_os_map **out, hx_os_error *error)
{
	hx_os_container_base base;
	hx_os_map *map;
	hx_os_status status;
	size_t alignment;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if ((kind != HX_OS_MAP_HASH && kind != HX_OS_MAP_ORDERED) ||
		(kind == HX_OS_MAP_HASH && compare))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_container_init(
		&base, context, options, sizeof(*map), 1);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	alignment = options->element_alignment;
	if (alignment < HX_OS_I_ALIGNMENT)
		alignment = HX_OS_I_ALIGNMENT;
	if (alignment > SIZE_MAX - sizeof(hx_os_map_node))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	map = hx_os_i_zero(context, sizeof(*map));
	if (!map)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	map->base = base;
	map->kind = kind;
	map->compare = compare;
	map->value_offset =
		(sizeof(hx_os_map_node) + alignment - 1) & ~(alignment - 1);
	*out = map;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

size_t hx_os_map_size(const hx_os_map *map)
{
	return map ? map->count : 0;
}

size_t hx_os_map_memory(const hx_os_map *map)
{
	return map ? map->base.memory : 0;
}

static hx_os_status hx_os_map_add(hx_os_map *map, const void *key,
	size_t key_bytes, const void *value, int replace, int take)
{
	hx_os_map_node *parent = NULL, *before = NULL, *after = NULL, *node,
		       *existing;
	hx_os_status status;
	size_t bytes, value_size = map->base.options.element_size;
	existing =
		hx_os_map_lookup(map, key, key_bytes, &parent, &before, &after);
	if (existing && !replace)
		return HX_OS_EXISTS;
	if (!existing && map->count == map->base.options.max_items)
		return HX_OS_LIMIT;
	if (existing && take && map->base.options.drop &&
		hx_os_i_container_overlap(
			value, value_size, existing->value, value_size))
		return HX_OS_INVALID;
	if (!hx_os_i_add_size(map->value_offset, value_size, &bytes) ||
		!hx_os_i_add_size(bytes, key_bytes, &bytes) ||
		!hx_os_i_add_size(bytes, 1, &bytes))
		return HX_OS_LIMIT;
	node = hx_os_i_container_alloc(
		&map->base, bytes, map->base.options.element_alignment);
	if (!node)
		return map->base.allocation_status;
	memset(node, 0, sizeof(*node));
	node->value = (unsigned char *)node + map->value_offset;
	node->key = (unsigned char *)node->value + value_size;
	node->key_bytes = key_bytes;
	if (key_bytes)
		memcpy((void *)node->key, key, key_bytes);
	((unsigned char *)node->key)[key_bytes] = 0;
	status = hx_os_i_container_copy(&map->base, node->value, value, take);
	if (status != HX_OS_OK) {
		hx_os_i_container_free(&map->base, node);
		return status;
	}
	if (existing) {
		hx_os_i_container_drop(&map->base, existing->value);
		if (value_size)
			memcpy(existing->value, node->value, value_size);
		hx_os_i_container_free(&map->base, node);
		++map->base.generation;
		return HX_OS_OK;
	}
	if (map->kind == HX_OS_MAP_HASH) {
		hx_os_map *previous = hx_os_hash_scope;
		hx_os_hash_scope = map;
		HASH_ADD_KEYPTR(
			hh, map->hash, node->key, (unsigned)key_bytes, node);
		hx_os_hash_scope = previous;
		if (node->failed) {
			if (!take)
				hx_os_i_container_drop(&map->base, node->value);
			hx_os_i_container_free(&map->base, node);
			return map->base.allocation_status;
		}
		before = map->last;
	}
	else {
		node->height = 1;
		node->parent = parent;
		if (!parent)
			map->root = node;
		else if (after == parent)
			parent->left = node;
		else
			parent->right = node;
		hx_os_map_balance(map, parent);
	}
	node->previous = before;
	node->next = after;
	if (before)
		before->next = node;
	else
		map->first = node;
	if (after)
		after->previous = node;
	else
		map->last = node;
	++map->count;
	++map->base.generation;
	return HX_OS_OK;
}

static hx_os_status hx_os_map_put(hx_os_map *map, const void *key,
	size_t key_bytes, const void *value, int replace, int take,
	hx_os_error *error)
{
	hx_os_status status;
	if (!map || (!key && key_bytes) ||
		(!value && map->base.options.element_size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (key_bytes > UINT_MAX)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	status = hx_os_i_container_enter(&map->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	status = hx_os_map_add(map, key, key_bytes, value, replace, take);
	return hx_os_i_container_leave(&map->base, status, error);
}

hx_os_status hx_os_map_insert(hx_os_map *map, const void *key, size_t key_bytes,
	const void *value, int replace, hx_os_error *error)
{
	return hx_os_map_put(map, key, key_bytes, value, replace, 0, error);
}

hx_os_status hx_os_map_insert_take(hx_os_map *map, const void *key,
	size_t key_bytes, void *value, int replace, hx_os_error *error)
{
	return hx_os_map_put(map, key, key_bytes, value, replace, 1, error);
}

hx_os_status hx_os_map_find(
	const hx_os_map *map, const void *key, size_t bytes, const void **value)
{
	hx_os_map_node *node;
	if (!map || !value || (!key && bytes))
		return HX_OS_INVALID;
	*value = NULL;
	if (map->base.busy)
		return HX_OS_BUSY;
	if (bytes > UINT_MAX)
		return HX_OS_LIMIT;
	node = hx_os_map_lookup(map, key, bytes, NULL, NULL, NULL);
	if (!node)
		return HX_OS_NOT_FOUND;
	*value = map->base.options.element_size ? node->value : NULL;
	return HX_OS_OK;
}

hx_os_status hx_os_map_erase(hx_os_map *map, const void *key, size_t bytes,
	void *out, hx_os_error *error)
{
	hx_os_map_node *node;
	hx_os_status status;
	if (!map || (!key && bytes) || (out && !map->base.options.element_size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (bytes > UINT_MAX)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	status = hx_os_i_container_enter(&map->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	node = hx_os_map_lookup(map, key, bytes, NULL, NULL, NULL);
	if (!node)
		return hx_os_i_container_leave(
			&map->base, HX_OS_NOT_FOUND, error);
	if (out &&
		hx_os_i_container_overlap(out, map->base.options.element_size,
			node,
			map->value_offset + map->base.options.element_size +
				node->key_bytes + 1))
		return hx_os_i_container_leave(
			&map->base, HX_OS_INVALID, error);
	if (out)
		memcpy(out, node->value, map->base.options.element_size);
	else
		hx_os_i_container_drop(&map->base, node->value);
	if (map->kind == HX_OS_MAP_HASH) {
		hx_os_map *previous = hx_os_hash_scope;
		hx_os_hash_scope = map;
		HASH_DELETE(hh, map->hash, node);
		hx_os_hash_scope = previous;
	}
	else
		hx_os_map_unlink_tree(map, node);
	if (node->previous)
		node->previous->next = node->next;
	else
		map->first = node->next;
	if (node->next)
		node->next->previous = node->previous;
	else
		map->last = node->previous;
	hx_os_i_container_free(&map->base, node);
	--map->count;
	++map->base.generation;
	return hx_os_i_container_leave(&map->base, HX_OS_OK, error);
}

hx_os_status hx_os_map_begin(const hx_os_map *map, hx_os_map_iterator *iterator)
{
	if (!map || !iterator)
		return HX_OS_INVALID;
	if (map->base.busy)
		return HX_OS_BUSY;
	iterator->owner = map;
	iterator->entry = map->first;
	iterator->generation = map->base.generation;
	return map->first ? HX_OS_OK : HX_OS_EOF;
}

static hx_os_status hx_os_map_iterator_check(const hx_os_map_iterator *iterator)
{
	if (!iterator || !iterator->owner)
		return HX_OS_INVALID;
	if (iterator->owner->base.busy)
		return HX_OS_BUSY;
	if (iterator->generation != iterator->owner->base.generation)
		return HX_OS_INVALID;
	return iterator->entry ? HX_OS_OK : HX_OS_EOF;
}

hx_os_status hx_os_map_next(hx_os_map_iterator *iterator)
{
	hx_os_status status = hx_os_map_iterator_check(iterator);
	if (status != HX_OS_OK)
		return status;
	iterator->entry = ((const hx_os_map_node *)iterator->entry)->next;
	return iterator->entry ? HX_OS_OK : HX_OS_EOF;
}

hx_os_status hx_os_map_entry(const hx_os_map_iterator *iterator,
	const void **key, size_t *key_bytes, const void **value)
{
	hx_os_status status;
	const hx_os_map_node *node;
	if (!key || !key_bytes || !value)
		return HX_OS_INVALID;
	status = hx_os_map_iterator_check(iterator);
	if (status != HX_OS_OK)
		return status;
	node = iterator->entry;
	*key = node->key;
	*key_bytes = node->key_bytes;
	*value =
		iterator->owner->base.options.element_size ? node->value : NULL;
	return HX_OS_OK;
}

static void hx_os_map_clear_values(hx_os_map *map)
{
	hx_os_map_node *node = map->first;
	if (map->hash) {
		hx_os_map *previous = hx_os_hash_scope;
		hx_os_hash_scope = map;
		HASH_CLEAR(hh, map->hash);
		hx_os_hash_scope = previous;
	}
	while (node) {
		hx_os_map_node *next = node->next;
		hx_os_i_container_drop(&map->base, node->value);
		hx_os_i_container_free(&map->base, node);
		node = next;
	}
	map->first = map->last = map->root = NULL;
	map->count = 0;
	++map->base.generation;
}

hx_os_status hx_os_map_clear(hx_os_map *map, hx_os_error *error)
{
	hx_os_status status;
	if (!map)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_container_enter(&map->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	hx_os_map_clear_values(map);
	return hx_os_i_container_leave(&map->base, HX_OS_OK, error);
}

hx_os_status hx_os_map_destroy(hx_os_map **map, hx_os_error *error)
{
	hx_os_map *value;
	hx_os_status status;
	if (!map)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	value = *map;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	status = hx_os_i_container_enter(&value->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	hx_os_map_clear_values(value);
	*map = NULL;
	hx_os_memory_free(value);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
