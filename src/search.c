#include "internal.h"
#include "hx_search.h"

hx_os_status hx_os_bytes_find(hx_os_context *context, hx_os_bytes_view input,
	hx_os_bytes_view needle, size_t start, size_t budget, size_t *offset,
	hx_os_error *error)
{
	const unsigned char *text = input.data, *pattern = needle.data;
	size_t *prefix, i, matched = 0, cost;
	hx_os_status status = HX_OS_NOT_FOUND;

	if (!HX_OS_ENABLE_CONTAINERS)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!offset || (!input.data && input.size) ||
		(!needle.data && needle.size) || start > input.size)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!needle.size) {
		*offset = start;
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
	if (needle.size > input.size - start)
		return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
	if (needle.size > SIZE_MAX / sizeof(*prefix))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	if (!budget)
		budget = 16 * 1024 * 1024;
	cost = hx_os_i_allocation_cost(needle.size * sizeof(*prefix), 0);
	if (!cost || cost > budget)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	prefix = hx_os_i_alloc(context, needle.size * sizeof(*prefix), 0);
	if (!prefix)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	prefix[0] = 0;
	for (i = 1; i < needle.size; ++i) {
		while (matched && pattern[i] != pattern[matched])
			matched = prefix[matched - 1];
		if (pattern[i] == pattern[matched])
			++matched;
		prefix[i] = matched;
	}
	matched = 0;
	for (i = start; i < input.size; ++i) {
		while (matched && text[i] != pattern[matched])
			matched = prefix[matched - 1];
		if (text[i] == pattern[matched])
			++matched;
		if (matched == needle.size) {
			*offset = i + 1 - matched;
			status = HX_OS_OK;
			break;
		}
	}
	hx_os_memory_free(prefix);
	return hx_os_i_error(error, status, 0, 0);
}

#define HX_OS_MATCH_NONE UINT32_MAX

typedef struct hx_os_match_node {
	uint32_t edge, failure, dictionary, terminal;
} hx_os_match_node;

typedef struct hx_os_match_edge {
	uint32_t next, node;
	unsigned char byte;
} hx_os_match_edge;

typedef struct hx_os_match_pattern {
	uint32_t next, bytes;
} hx_os_match_pattern;

struct hx_os_matcher {
	hx_os_match_node *nodes;
	hx_os_match_edge *edges;
	hx_os_match_pattern *patterns;
	void *storage;
};

static uint32_t hx_os_match_child(
	const hx_os_matcher *matcher, uint32_t node, unsigned char byte)
{
	uint32_t edge;

	for (edge = matcher->nodes[node].edge; edge != HX_OS_MATCH_NONE;
		edge = matcher->edges[edge].next)
		if (matcher->edges[edge].byte == byte)
			return matcher->edges[edge].node;
	return HX_OS_MATCH_NONE;
}

hx_os_status hx_os_matcher_create(hx_os_context *context,
	const hx_os_bytes_view *patterns, size_t count, size_t budget,
	hx_os_matcher **out, hx_os_error *error)
{
	hx_os_matcher *matcher;
	size_t total = 0, bytes, i, qread = 0, qwrite = 0;
	uint32_t *queue, nodes = 1, edges = 0;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!HX_OS_ENABLE_CONTAINERS)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!patterns || !count || count > 4096)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	for (i = 0; i < count; ++i) {
		if (!patterns[i].data || !patterns[i].size)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		if (patterns[i].size > 1024 * 1024 - total)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		total += patterns[i].size;
	}
	bytes = (total + 1) * (sizeof(*matcher->nodes) + sizeof(*queue)) +
		total * sizeof(*matcher->edges) +
		count * sizeof(*matcher->patterns);
	if (!budget)
		budget = 16 * 1024 * 1024;
	if (hx_os_i_allocation_cost(bytes, 0) +
			hx_os_i_allocation_cost(sizeof(*matcher), 0) >
		budget)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	matcher = hx_os_i_zero(context, sizeof(*matcher));
	if (!matcher)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	matcher->storage = hx_os_i_alloc(context, bytes, 0);
	if (!matcher->storage) {
		hx_os_memory_free(matcher);
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	matcher->nodes = matcher->storage;
	matcher->edges = (hx_os_match_edge *)(matcher->nodes + total + 1);
	matcher->patterns = (hx_os_match_pattern *)(matcher->edges + total);
	queue = (uint32_t *)(matcher->patterns + count);
	for (i = 0; i <= total; ++i) {
		matcher->nodes[i].edge = HX_OS_MATCH_NONE;
		matcher->nodes[i].terminal = HX_OS_MATCH_NONE;
		matcher->nodes[i].dictionary = HX_OS_MATCH_NONE;
		matcher->nodes[i].failure = 0;
	}
	for (i = 0; i < count; ++i) {
		const unsigned char *pattern = patterns[i].data;
		uint32_t node = 0;
		size_t j;

		for (j = 0; j < patterns[i].size; ++j) {
			uint32_t child =
				hx_os_match_child(matcher, node, pattern[j]);

			if (child == HX_OS_MATCH_NONE) {
				child = nodes++;
				matcher->edges[edges].node = child;
				matcher->edges[edges].byte = pattern[j];
				matcher->edges[edges].next =
					matcher->nodes[node].edge;
				matcher->nodes[node].edge = edges++;
			}
			node = child;
		}
		matcher->patterns[i].next = matcher->nodes[node].terminal;
		matcher->patterns[i].bytes = (uint32_t)patterns[i].size;
		matcher->nodes[node].terminal = (uint32_t)i;
	}
	queue[qwrite++] = 0;
	while (qread < qwrite) {
		uint32_t node = queue[qread++], edge;

		for (edge = matcher->nodes[node].edge; edge != HX_OS_MATCH_NONE;
			edge = matcher->edges[edge].next) {
			uint32_t child = matcher->edges[edge].node;
			uint32_t fail = matcher->nodes[node].failure;
			uint32_t next = node
				? hx_os_match_child(matcher, fail,
					  matcher->edges[edge].byte)
				: 0;

			while (next == HX_OS_MATCH_NONE && fail) {
				fail = matcher->nodes[fail].failure;
				next = hx_os_match_child(matcher, fail,
					matcher->edges[edge].byte);
			}
			if (next == HX_OS_MATCH_NONE)
				next = 0;
			matcher->nodes[child].failure = next;
			matcher->nodes[child].dictionary =
				matcher->nodes[next].terminal !=
					HX_OS_MATCH_NONE
				? next
				: matcher->nodes[next].dictionary;
			queue[qwrite++] = child;
		}
	}
	*out = matcher;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_matcher_scan(const hx_os_matcher *matcher,
	hx_os_bytes_view input, size_t maximum, hx_os_match_fn callback,
	void *user, size_t *completed, hx_os_error *error)
{
	const unsigned char *data = input.data;
	uint32_t node = 0;
	size_t i;

	if (completed)
		*completed = 0;
	if (!matcher || (!data && input.size) || !callback || !completed)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!maximum)
		maximum = 65536;
	for (i = 0; i < input.size; ++i) {
		uint32_t next = hx_os_match_child(matcher, node, data[i]);
		uint32_t terminal;

		while (next == HX_OS_MATCH_NONE && node) {
			node = matcher->nodes[node].failure;
			next = hx_os_match_child(matcher, node, data[i]);
		}
		node = next == HX_OS_MATCH_NONE ? 0 : next;
		for (terminal = node; terminal != HX_OS_MATCH_NONE;
			terminal = matcher->nodes[terminal].dictionary) {
			uint32_t pattern;

			for (pattern = matcher->nodes[terminal].terminal;
				pattern != HX_OS_MATCH_NONE;
				pattern = matcher->patterns[pattern].next) {
				size_t bytes = matcher->patterns[pattern].bytes;
				hx_os_status status;

				if (*completed == maximum)
					return hx_os_i_error(
						error, HX_OS_LIMIT, 0, 0);
				status = callback(
					pattern, i + 1 - bytes, bytes, user);
				if (status != HX_OS_OK)
					return hx_os_i_error(
						error, status, 0, 0);
				++*completed;
			}
		}
	}
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_matcher_destroy(hx_os_matcher **matcher)
{
	hx_os_matcher *value = *matcher;

	*matcher = NULL;
	if (value) {
		hx_os_memory_free(value->storage);
		hx_os_memory_free(value);
	}
}
