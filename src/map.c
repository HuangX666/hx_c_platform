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
	int height; // AVL height, or red-black color: zero black, one red.
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

static int hx_os_map_kind(const hx_os_map *map)
{
	return map->kind & ~HX_OS_MAP_MULTI;
}

static int hx_os_map_red_black(const hx_os_map *map)
{
	return hx_os_map_kind(map) == HX_OS_MAP_RED_BLACK;
}

#if defined(_MSC_VER)
static __declspec(thread) hx_os_map *hx_os_hash_scope;
#else
static __thread hx_os_map *hx_os_hash_scope;
#endif

void *hx_os_i_map_exchange_execution(void *value)
{
	hx_os_map *previous = hx_os_hash_scope;

	hx_os_hash_scope = value;

	return previous;
}

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
	hx_os_map_node **after, int inserting_duplicate)
{
	hx_os_map_node *node = NULL, *found = NULL;

	if (!key)
		key = "";

	if (hx_os_map_kind(map) == HX_OS_MAP_HASH) {
		if (!inserting_duplicate)
			HASH_FIND(hh, map->hash, key, (unsigned)bytes, node);

		return node;
	}

	node = map->root;

	while (node) {
		int comparison = hx_os_map_compare(
			map, key, bytes, node->key, node->key_bytes);
		if (!comparison && !inserting_duplicate) {
			found = node;
			if (!(map->kind & HX_OS_MAP_MULTI))
				return found;

			node = node->left;
			continue;
		}

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

	return found;
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
	if (!hx_os_map_red_black(map)) {
		hx_os_map_height_update(node);
		hx_os_map_height_update(pivot);
	}

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

static int hx_os_map_is_red(const hx_os_map_node *node)
{
	return node && node->height;
}

static void hx_os_map_rb_insert(hx_os_map *map, hx_os_map_node *node)
{
	while (hx_os_map_is_red(node->parent)) {
		hx_os_map_node *parent = node->parent;
		hx_os_map_node *grand = parent->parent;
		int left = parent == grand->left;
		hx_os_map_node *uncle = left ? grand->right : grand->left;

		if (hx_os_map_is_red(uncle)) {
			parent->height = uncle->height = 0;
			grand->height = 1;
			node = grand;
		}
		else {
			if (node == (left ? parent->right : parent->left)) {
				node = parent;
				(void)hx_os_map_rotate(map, node, left);
				parent = node->parent;
				grand = parent->parent;
			}

			parent->height = 0;
			grand->height = 1;
			(void)hx_os_map_rotate(map, grand, !left);
		}
	}

	map->root->height = 0;
}

static void hx_os_map_rb_repair(
	hx_os_map *map, hx_os_map_node *node, hx_os_map_node *parent)
{
	while (node != map->root && !hx_os_map_is_red(node)) {
		int left = node == parent->left;
		hx_os_map_node *sibling = left ? parent->right : parent->left;

		if (hx_os_map_is_red(sibling)) {
			sibling->height = 0;
			parent->height = 1;
			(void)hx_os_map_rotate(map, parent, left);
			sibling = left ? parent->right : parent->left;
		}

		if (!sibling ||
			(!hx_os_map_is_red(sibling->left) &&
				!hx_os_map_is_red(sibling->right))) {
			if (sibling)
				sibling->height = 1;

			node = parent;
			parent = node->parent;
		}
		else {
			hx_os_map_node *far =
				left ? sibling->right : sibling->left;

			if (!hx_os_map_is_red(far)) {
				hx_os_map_node *near =
					left ? sibling->left : sibling->right;

				near->height = 0;
				sibling->height = 1;
				(void)hx_os_map_rotate(map, sibling, !left);
				sibling = left ? parent->right : parent->left;
				far = left ? sibling->right : sibling->left;
			}

			sibling->height = parent->height;
			parent->height = far->height = 0;
			(void)hx_os_map_rotate(map, parent, left);
			node = map->root;
		}
	}

	if (node)
		node->height = 0;
}

static void hx_os_map_rb_erase(hx_os_map *map, hx_os_map_node *node)
{
	hx_os_map_node *replacement = node, *child, *parent;
	int red = node->height;

	if (!node->left || !node->right) {
		child = node->left ? node->left : node->right;
		parent = node->parent;
		hx_os_map_replace(map, node, child);
	}
	else {
		replacement = node->right;

		while (replacement->left)
			replacement = replacement->left;

		red = replacement->height;
		child = replacement->right;
		if (replacement->parent == node) {
			parent = replacement;
			if (child)
				child->parent = replacement;
		}
		else {
			parent = replacement->parent;
			hx_os_map_replace(map, replacement, child);
			replacement->right = node->right;
			replacement->right->parent = replacement;
		}

		hx_os_map_replace(map, node, replacement);
		replacement->left = node->left;
		replacement->left->parent = replacement;
		replacement->height = node->height;
	}

	if (!red)
		hx_os_map_rb_repair(map, child, parent);
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
	if ((kind & ~HX_OS_MAP_MULTI) < HX_OS_MAP_HASH ||
		(kind & ~HX_OS_MAP_MULTI) > HX_OS_MAP_RED_BLACK ||
		((kind & ~HX_OS_MAP_MULTI) == HX_OS_MAP_HASH && compare))
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

	existing = hx_os_map_lookup(map, key, key_bytes, &parent, &before,
		&after, (map->kind & HX_OS_MAP_MULTI) && !replace);
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

	if (hx_os_map_kind(map) == HX_OS_MAP_HASH) {
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

		if (hx_os_map_red_black(map))
			hx_os_map_rb_insert(map, node);
		else
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

	node = hx_os_map_lookup(map, key, bytes, NULL, NULL, NULL, 0);
	if (!node)
		return HX_OS_NOT_FOUND;

	*value = map->base.options.element_size ? node->value : NULL;

	return HX_OS_OK;
}

static hx_os_status hx_os_map_remove_node(
	hx_os_map *map, hx_os_map_node *node, void *out)
{
	if (out &&
		hx_os_i_container_overlap(out, map->base.options.element_size,
			node,
			map->value_offset + map->base.options.element_size +
				node->key_bytes + 1))
		return HX_OS_INVALID;
	if (out)
		memcpy(out, node->value, map->base.options.element_size);
	else
		hx_os_i_container_drop(&map->base, node->value);

	if (hx_os_map_kind(map) == HX_OS_MAP_HASH) {
		hx_os_map *previous = hx_os_hash_scope;

		hx_os_hash_scope = map;
		HASH_DELETE(hh, map->hash, node);
		hx_os_hash_scope = previous;
	}
	else if (hx_os_map_red_black(map))
		hx_os_map_rb_erase(map, node);
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

	node = hx_os_map_lookup(map, key, bytes, NULL, NULL, NULL, 0);

	status = node ? hx_os_map_remove_node(map, node, out) : HX_OS_NOT_FOUND;

	return hx_os_i_container_leave(&map->base, status, error);
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

static hx_os_map_node *hx_os_map_bound(
	const hx_os_map *map, const void *key, size_t bytes, int upper)
{
	hx_os_map_node *node = map->root, *candidate = NULL;

	while (node) {
		int order = hx_os_map_compare(
			map, key ? key : "", bytes, node->key, node->key_bytes);
		if (order < 0 || (!order && !upper)) {
			candidate = node;
			node = node->left;
		}
		else
			node = node->right;
	}

	return candidate;
}

static hx_os_status hx_os_map_seek(const hx_os_map *map, const void *key,
	size_t bytes, hx_os_map_iterator *iterator, int upper)
{
	if (!map || !iterator || (!key && bytes))
		return HX_OS_INVALID;

	if (map->base.busy)
		return HX_OS_BUSY;

	if (bytes > UINT_MAX)
		return HX_OS_LIMIT;

	if (hx_os_map_kind(map) == HX_OS_MAP_HASH)
		return HX_OS_NOT_SUPPORTED;

	iterator->owner = map;
	iterator->generation = map->base.generation;
	iterator->entry = hx_os_map_bound(map, key, bytes, upper);

	return iterator->entry ? HX_OS_OK : HX_OS_EOF;
}

hx_os_status hx_os_map_lower_bound(const hx_os_map *map, const void *key,
	size_t bytes, hx_os_map_iterator *iterator)
{
	return hx_os_map_seek(map, key, bytes, iterator, 0);
}

hx_os_status hx_os_map_upper_bound(const hx_os_map *map, const void *key,
	size_t bytes, hx_os_map_iterator *iterator)
{
	return hx_os_map_seek(map, key, bytes, iterator, 1);
}

hx_os_status hx_os_map_equal_range(const hx_os_map *map, const void *key,
	size_t bytes, hx_os_map_iterator *first, hx_os_map_iterator *last)
{
	hx_os_map_iterator begin, end;
	hx_os_status status;

	if (!first || !last || first == last)
		return HX_OS_INVALID;

	status = hx_os_map_seek(map, key, bytes, &begin, 0);
	if (status != HX_OS_OK && status != HX_OS_EOF)
		return status;

	status = hx_os_map_seek(map, key, bytes, &end, 1);
	if (status != HX_OS_OK && status != HX_OS_EOF)
		return status;

	*first = begin;
	*last = end;

	return HX_OS_OK;
}

hx_os_status hx_os_map_count(
	const hx_os_map *map, const void *key, size_t bytes, size_t *count)
{
	size_t found = 0;
	hx_os_map_node *node;

	if (!map || !count || (!key && bytes))
		return HX_OS_INVALID;

	if (map->base.busy)
		return HX_OS_BUSY;

	if (bytes > UINT_MAX)
		return HX_OS_LIMIT;

	if (!key)
		key = "";

	if (hx_os_map_kind(map) == HX_OS_MAP_HASH) {
		unsigned hash, bucket;
		UT_hash_handle *handle;

		HASH_VALUE(key, (unsigned)bytes, hash);
		if (map->hash) {
			HASH_TO_BKT(
				hash, map->hash->hh.tbl->num_buckets, bucket);

			for (handle = map->hash->hh.tbl->buckets[bucket]
					      .hh_head;
				handle; handle = handle->hh_next)
				if (handle->hashv == hash &&
					handle->keylen == bytes &&
					!memcmp(handle->key, key, bytes))
					++found;
		}
	}
	else {
		node = hx_os_map_bound(map, key, bytes, 0);

		while (node &&
			!hx_os_map_compare(
				map, key, bytes, node->key, node->key_bytes)) {
			++found;
			node = node->next;
		}
	}

	*count = found;

	return HX_OS_OK;
}

hx_os_status hx_os_map_last(const hx_os_map *map, hx_os_map_iterator *iterator)
{
	hx_os_status status = hx_os_map_begin(map, iterator);
	if (status != HX_OS_OK && status != HX_OS_EOF)
		return status;

	iterator->entry = map->last;

	return status;
}

hx_os_status hx_os_map_previous(hx_os_map_iterator *iterator)
{
	hx_os_status status = hx_os_map_iterator_check(iterator);
	if (status != HX_OS_OK)
		return status;

	iterator->entry = ((const hx_os_map_node *)iterator->entry)->previous;

	return iterator->entry ? HX_OS_OK : HX_OS_EOF;
}

hx_os_status hx_os_map_erase_at(hx_os_map *map, hx_os_map_iterator *iterator,
	void *out, hx_os_error *error)
{
	hx_os_status status;
	hx_os_map_node *node, *next;

	if (!map || !iterator || iterator->owner != map ||
		(out && !map->base.options.element_size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_map_iterator_check(iterator);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	status = hx_os_i_container_enter(&map->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	node = (hx_os_map_node *)iterator->entry;
	next = node->next;

	status = hx_os_map_remove_node(map, node, out);
	if (status == HX_OS_OK) {
		iterator->entry = next;
		iterator->generation = map->base.generation;
	}

	return hx_os_i_container_leave(&map->base, status, error);
}

hx_os_status hx_os_map_erase_all(hx_os_map *map, const void *key, size_t bytes,
	size_t *count, hx_os_error *error)
{
	hx_os_status status;
	hx_os_map_node *node, *entry;
	size_t removed = 0;

	if (!map || (!key && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (bytes > UINT_MAX)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	status = hx_os_i_container_enter(&map->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	node = hx_os_map_lookup(map, key, bytes, NULL, NULL, NULL, 0);
	// Keep one matching node until last so borrowed keys need no copy.
	if (node && hx_os_map_kind(map) == HX_OS_MAP_HASH) {
		unsigned bucket;
		UT_hash_handle *handle, *next;

		HASH_TO_BKT(node->hh.hashv, node->hh.tbl->num_buckets, bucket);

		for (handle = node->hh.tbl->buckets[bucket].hh_head; handle;
			handle = next) {
			next = handle->hh_next;
			entry = (hx_os_map_node *)((char *)handle -
				offsetof(hx_os_map_node, hh));
			if (entry != node &&
				entry->key_bytes == node->key_bytes &&
				!memcmp(entry->key, node->key,
					node->key_bytes)) {
				(void)hx_os_map_remove_node(map, entry, NULL);
				++removed;
			}
		}
	}
	else if (node) {
		entry = node->next;

		while (entry &&
			!hx_os_map_compare(map, node->key, node->key_bytes,
				entry->key, entry->key_bytes)) {
			hx_os_map_node *next = entry->next;

			(void)hx_os_map_remove_node(map, entry, NULL);
			++removed;
			entry = next;
		}
	}

	if (node) {
		(void)hx_os_map_remove_node(map, node, NULL);
		++removed;
	}

	if (count)
		*count = removed;

	return hx_os_i_container_leave(&map->base, HX_OS_OK, error);
}

static int hx_os_map_validate_tree(const hx_os_map *map,
	const hx_os_map_node *node, const hx_os_map_node *parent,
	size_t *visited, const hx_os_map_node **ordered, unsigned depth)
{
	int left, right;

	if (!node)
		return 0;

	if (depth > 128 || node->parent != parent || ++*visited > map->count)
		return -1;

	left = hx_os_map_validate_tree(
		map, node->left, node, visited, ordered, depth + 1);
	if (left < 0 || *ordered != node)
		return -1;

	*ordered = node->next;
	right = hx_os_map_validate_tree(
		map, node->right, node, visited, ordered, depth + 1);
	if (left < 0 || right < 0)
		return -1;

	if (hx_os_map_red_black(map)) {
		if (left != right || (node->height != 0 && node->height != 1) ||
			(hx_os_map_is_red(node) &&
				(hx_os_map_is_red(node->left) ||
					hx_os_map_is_red(node->right))))
			return -1;
		return left + !node->height;
	}

	if (left - right < -1 || left - right > 1 ||
		node->height != 1 + (left > right ? left : right))
		return -1;
	return node->height;
}

hx_os_status hx_os_map_validate(const hx_os_map *map)
{
	const hx_os_map_node *node, *previous = NULL;
	size_t count = 0, visited = 0;

	if (!map)
		return HX_OS_INVALID;

	if (map->base.busy)
		return HX_OS_BUSY;

	if (hx_os_map_kind(map) != HX_OS_MAP_HASH) {
		const hx_os_map_node *ordered = map->first;

		if ((hx_os_map_red_black(map) && hx_os_map_is_red(map->root)) ||
			hx_os_map_validate_tree(map, map->root, NULL, &visited,
				&ordered, 0) < 0 ||
			visited != map->count || ordered)
			return HX_OS_INVALID;
	}

	for (node = map->first; node; node = node->next) {
		if (++count > map->count || node->previous != previous)
			return HX_OS_INVALID;

		if (previous && hx_os_map_kind(map) != HX_OS_MAP_HASH) {
			int order = hx_os_map_compare(map, previous->key,
				previous->key_bytes, node->key,
				node->key_bytes);
			if (order > 0 ||
				(!order && !(map->kind & HX_OS_MAP_MULTI)))
				return HX_OS_INVALID;
		}

		previous = node;
	}

	return count == map->count && previous == map->last ? HX_OS_OK
							    : HX_OS_INVALID;
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
