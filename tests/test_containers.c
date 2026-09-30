#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "check failed at %d: %s\n", __LINE__,  \
				#value);                                       \
			exit(1);                                               \
		}                                                              \
	} while (0)

static int test_compare(const void *a, const void *b, void *user)
{
	int left, right;
	(void)user;
	memcpy(&left, a, sizeof(left));
	memcpy(&right, b, sizeof(right));
	return (left > right) - (left < right);
}

static int test_key_compare(const void *a, size_t a_bytes, const void *b,
	size_t b_bytes, void *user)
{
	size_t *comparisons = user;
	CHECK(a_bytes == sizeof(int) && b_bytes == sizeof(int));
	if (comparisons)
		++*comparisons;
	return test_compare(a, b, NULL);
}

static void test_sequence(hx_os_context *context, int kind)
{
	hx_os_container_options options;
	hx_os_sequence *sequence;
	hx_os_sequence_iterator iterator;
	hx_os_status status;
	size_t i;
	int value, removed;
	const void *borrowed;
	hx_os_container_options_init(&options, sizeof(int));
	options.max_items = 512;
	if (kind == 0)
		status =
			hx_os_vector_create(context, &options, &sequence, NULL);
	else if (kind == 1)
		status = hx_os_deque_create(context, &options, &sequence, NULL);
	else
		status = hx_os_list_create(context, &options, &sequence, NULL);
	CHECK(status == HX_OS_OK);
	for (i = 0; i < 300; ++i) {
		value = (int)i;
		CHECK(hx_os_sequence_push_back(sequence, &value, NULL) ==
			HX_OS_OK);
	}
	for (i = 0; i < 100; ++i) {
		CHECK(hx_os_sequence_pop_front(sequence, &removed, NULL) ==
			HX_OS_OK);
		CHECK(removed == (int)i);
	}
	for (i = 300; i < 500; ++i) {
		value = (int)i;
		CHECK(hx_os_sequence_push_back(sequence, &value, NULL) ==
			HX_OS_OK);
	}
	CHECK(hx_os_sequence_size(sequence) == 400);
	CHECK(hx_os_sequence_begin(sequence, &iterator) == HX_OS_OK);
	for (i = 100; i < 500; ++i) {
		CHECK(hx_os_sequence_value(&iterator, &borrowed) == HX_OS_OK);
		CHECK(*(const int *)borrowed == (int)i);
		CHECK(hx_os_sequence_next(&iterator) ==
			(i == 499 ? HX_OS_EOF : HX_OS_OK));
	}
	borrowed = hx_os_sequence_at(sequence, 2);
	CHECK(hx_os_sequence_insert(sequence, 1, borrowed, NULL) == HX_OS_OK);
	CHECK(*(const int *)hx_os_sequence_at(sequence, 1) == 102);
	CHECK(hx_os_sequence_value(&iterator, &borrowed) == HX_OS_INVALID);
	CHECK(hx_os_sequence_erase(sequence, 1, &removed, NULL) == HX_OS_OK &&
		removed == 102);
	CHECK(hx_os_sequence_clear(sequence, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_size(sequence) == 0);
	value = 42;
	CHECK(hx_os_sequence_push_front(sequence, &value, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_pop_back(sequence, &removed, NULL) == HX_OS_OK &&
		removed == 42);
	CHECK(hx_os_sequence_pop_back(sequence, &removed, NULL) ==
		HX_OS_NOT_FOUND);
	CHECK(hx_os_sequence_destroy(&sequence, NULL) == HX_OS_OK && !sequence);
}

static void test_heap_and_sort(hx_os_context *context)
{
	hx_os_container_options options;
	hx_os_vector *vector;
	hx_os_heap *heap;
	int value, result, found;
	size_t i, index;
	hx_os_container_options_init(&options, sizeof(int));
	options.compare = test_compare;
	CHECK(hx_os_vector_create(context, &options, &vector, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_heap_create(context, &options, &heap, NULL) == HX_OS_OK);
	for (i = 0; i < 257; ++i) {
		value = (int)((i * 127) % 257);
		CHECK(hx_os_heap_push(heap, &value, NULL) == HX_OS_OK);
		CHECK(hx_os_vector_push_back(vector, &value, NULL) == HX_OS_OK);
	}
	CHECK(hx_os_vector_sort(vector, NULL) == HX_OS_OK);
	for (i = 0; i < 257; ++i) {
		CHECK(((int *)hx_os_vector_data(vector))[i] == (int)i);
		CHECK(hx_os_heap_pop(heap, &result, NULL) == HX_OS_OK);
		CHECK(result == 256 - (int)i);
		value = (int)i;
		CHECK(hx_os_vector_lower_bound(
			      vector, &value, &index, &found) == HX_OS_OK);
		CHECK(found && index == i);
	}
	value = 999;
	CHECK(hx_os_vector_lower_bound(vector, &value, &index, &found) ==
		HX_OS_OK);
	CHECK(!found && index == 257);
	CHECK(hx_os_sequence_destroy(&heap, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_destroy(&vector, NULL) == HX_OS_OK);
}

static void test_map(hx_os_context *context, int kind)
{
	hx_os_container_options options;
	hx_os_map *map;
	hx_os_map_iterator iterator;
	int present[257] = {0}, values[257] = {0};
	uint32_t random = 193;
	size_t step, count = 0, comparisons = 0;
	hx_os_container_options_init(&options, sizeof(int));
	options.user = &comparisons;
	CHECK(hx_os_map_create(context, kind, &options,
		      kind == HX_OS_MAP_ORDERED ? test_key_compare : NULL, &map,
		      NULL) == HX_OS_OK);
	for (step = 0; step < 6000; ++step) {
		int key, value, operation;
		const void *stored;
		hx_os_status status;
		random = random * UINT32_C(1664525) + UINT32_C(1013904223);
		key = (int)((random >> 8) % 257);
		operation = (int)(random % 4);
		value = (int)step;
		if (operation < 2) {
			status = hx_os_map_insert(map, &key, sizeof(key),
				&value, operation, NULL);
			if (present[key] && !operation)
				CHECK(status == HX_OS_EXISTS);
			else {
				CHECK(status == HX_OS_OK);
				if (!present[key])
					++count;
				present[key] = 1;
				values[key] = value;
			}
		}
		else if (operation == 2) {
			status = hx_os_map_erase(
				map, &key, sizeof(key), &value, NULL);
			CHECK(status ==
				(present[key] ? HX_OS_OK : HX_OS_NOT_FOUND));
			if (present[key]) {
				CHECK(value == values[key]);
				present[key] = 0;
				--count;
			}
		}
		else {
			status =
				hx_os_map_find(map, &key, sizeof(key), &stored);
			CHECK(status ==
				(present[key] ? HX_OS_OK : HX_OS_NOT_FOUND));
			if (present[key])
				CHECK(*(const int *)stored == values[key]);
		}
		CHECK(hx_os_map_size(map) == count);
		if (step % 100 == 0) {
			size_t seen = 0;
			int previous = -1;
			status = hx_os_map_begin(map, &iterator);
			while (status == HX_OS_OK) {
				const void *key_data, *data;
				size_t bytes;
				int actual_key;
				CHECK(hx_os_map_entry(&iterator, &key_data,
					      &bytes, &data) == HX_OS_OK);
				CHECK(bytes == sizeof(int));
				memcpy(&actual_key, key_data, bytes);
				CHECK(present[actual_key] &&
					*(const int *)data ==
						values[actual_key]);
				if (kind == HX_OS_MAP_ORDERED)
					CHECK(actual_key > previous);
				previous = actual_key;
				++seen;
				status = hx_os_map_next(&iterator);
			}
			CHECK(status == HX_OS_EOF && seen == count);
		}
	}
	CHECK(hx_os_map_clear(map, NULL) == HX_OS_OK);
	comparisons = 0;
	for (step = 0; step < 3000; ++step) {
		int key = (int)step;
		CHECK(hx_os_map_insert(map, &key, sizeof(key), &key, 0, NULL) ==
			HX_OS_OK);
	}
	CHECK(comparisons < 100000);
	for (step = 0; step < 3000; ++step) {
		int key = (int)((step * 997) % 3000);
		CHECK(hx_os_map_erase(map, &key, sizeof(key), NULL, NULL) ==
			HX_OS_OK);
	}
	CHECK(hx_os_map_size(map) == 0);
	CHECK(hx_os_map_destroy(&map, NULL) == HX_OS_OK && !map);
	hx_os_container_options_init(&options, 0);
	CHECK(hx_os_map_create(context, kind, &options, NULL, &map, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_map_insert(map, "中文", 6, NULL, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_map_insert(map, "a\0b", 3, NULL, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_map_insert(map, NULL, 0, NULL, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_map_size(map) == 3);
	CHECK(hx_os_map_begin(map, &iterator) == HX_OS_OK);
	CHECK(hx_os_map_erase(map, NULL, 0, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_map_next(&iterator) == HX_OS_INVALID);
	CHECK(hx_os_map_destroy(&map, NULL) == HX_OS_OK);
}

typedef struct test_owned {
	hx_os_context *context;
	hx_os_sequence *sequence;
	size_t copies;
	size_t drops;
	int fail;
} test_owned;

static hx_os_status test_copy(void *out, const void *input, void *user)
{
	test_owned *state = user;
	char *value;
	const char *source = *(char *const *)input;
	if (state->sequence)
		CHECK(hx_os_sequence_clear(state->sequence, NULL) ==
			HX_OS_BUSY);
	if (state->fail)
		return HX_OS_NO_MEMORY;
	CHECK(hx_os_memory_alloc(state->context, strlen(source) + 1, 0,
		      (void **)&value, NULL) == HX_OS_OK);
	strcpy(value, source);
	*(char **)out = value;
	++state->copies;
	return HX_OS_OK;
}

static void test_drop(void *value, void *user)
{
	test_owned *state = user;
	hx_os_memory_free(*(char **)value);
	++state->drops;
}

static void test_ownership(hx_os_context *context)
{
	hx_os_container_options options;
	hx_os_sequence *sequence;
	test_owned owned = {0};
	char *source = "owned", *taken;
	size_t i;
	owned.context = context;
	hx_os_container_options_init(&options, sizeof(char *));
	options.copy = test_copy;
	options.drop = test_drop;
	options.user = &owned;
	CHECK(hx_os_vector_create(context, &options, &sequence, NULL) ==
		HX_OS_OK);
	owned.sequence = sequence;
	for (i = 0; i < 20; ++i)
		CHECK(hx_os_sequence_push_back(sequence, &source, NULL) ==
			HX_OS_OK);
	owned.fail = 1;
	CHECK(hx_os_sequence_push_back(sequence, &source, NULL) ==
		HX_OS_NO_MEMORY);
	CHECK(hx_os_sequence_size(sequence) == 20);
	CHECK(hx_os_sequence_pop_back(sequence, &taken, NULL) == HX_OS_OK);
	CHECK(!strcmp(taken, source));
	test_drop(&taken, &owned);
	CHECK(hx_os_sequence_destroy(&sequence, NULL) == HX_OS_OK);
	owned.sequence = NULL;
	CHECK(owned.copies == owned.drops);
	options.copy = NULL;
	CHECK(hx_os_vector_create(context, &options, &sequence, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_sequence_push_back(sequence, &source, NULL) ==
		HX_OS_NOT_SUPPORTED);
	CHECK(hx_os_memory_alloc(context, 4, 0, (void **)&taken, NULL) ==
		HX_OS_OK);
	strcpy(taken, "abc");
	CHECK(hx_os_sequence_push_back_take(sequence, &taken, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_sequence_destroy(&sequence, NULL) == HX_OS_OK);
}

typedef union test_memory {
	struct {
		size_t bytes;
	} fields;
	long double alignment;
	void *pointer;
} test_memory;

typedef struct test_allocator {
	size_t attempts;
	size_t fail;
	size_t bytes;
	size_t peak;
} test_allocator;

static void *test_allocate(size_t bytes, void *user)
{
	test_allocator *allocator = user;
	test_memory *memory;
	if (++allocator->attempts == allocator->fail)
		return NULL;
	memory = malloc(sizeof(*memory) + bytes);
	if (!memory)
		return NULL;
	memory->fields.bytes = bytes;
	allocator->bytes += bytes;
	if (allocator->peak < allocator->bytes)
		allocator->peak = allocator->bytes;
	return memory + 1;
}

static void test_free(void *data, void *user)
{
	test_allocator *allocator = user;
	test_memory *memory = (test_memory *)data - 1;
	allocator->bytes -= memory->fields.bytes;
	free(memory);
}

static void test_limits_and_failure(void)
{
	size_t fail;
	for (fail = 2; fail < 700; ++fail) {
		hx_os_context_options context_options;
		hx_os_container_options options;
		hx_os_context *context;
		hx_os_map *map = NULL;
		test_allocator allocator = {0};
		hx_os_status status;
		int key, successes = 0;
		allocator.fail = fail;
		hx_os_context_options_init(&context_options);
		context_options.allocator.allocate = test_allocate;
		context_options.allocator.deallocate = test_free;
		context_options.allocator.user = &allocator;
		CHECK(hx_os_context_create(&context_options, &context, NULL) ==
			HX_OS_OK);
		hx_os_container_options_init(&options, sizeof(int));
		options.max_bytes = 1024 * 1024;
		status = hx_os_map_create(
			context, HX_OS_MAP_HASH, &options, NULL, &map, NULL);
		if (status == HX_OS_OK) {
			for (key = 0; key < 600; ++key) {
				status = hx_os_map_insert(
					map, &key, sizeof(key), &key, 0, NULL);
				CHECK(status == HX_OS_OK ||
					status == HX_OS_NO_MEMORY);
				if (status == HX_OS_OK)
					++successes;
				else
					break;
			}
			CHECK(hx_os_map_size(map) == (size_t)successes);
			for (key = 0; key < successes; ++key) {
				const void *value;
				CHECK(hx_os_map_find(map, &key, sizeof(key),
					      &value) == HX_OS_OK);
				CHECK(*(const int *)value == key);
			}
			CHECK(hx_os_map_destroy(&map, NULL) == HX_OS_OK);
		}
		else
			CHECK(status == HX_OS_NO_MEMORY);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
		CHECK(allocator.bytes == 0);
		if (successes == 600)
			break;
	}
	CHECK(fail < 700);
	{
		hx_os_container_options options;
		hx_os_vector *vector;
		int value = 1;
		hx_os_container_options_init(&options, sizeof(value));
		options.max_items = 1;
		CHECK(hx_os_vector_create(NULL, &options, &vector, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_vector_push_back(vector, &value, NULL) == HX_OS_OK);
		CHECK(hx_os_vector_push_back(vector, &value, NULL) ==
			HX_OS_LIMIT);
		CHECK(hx_os_sequence_size(vector) == 1);
		CHECK(hx_os_sequence_reserve(vector, SIZE_MAX, NULL) ==
			HX_OS_LIMIT);
		CHECK(hx_os_sequence_destroy(&vector, NULL) == HX_OS_OK);
	}
}

static void test_string_values(hx_os_context *context)
{
	hx_os_container_options options;
	hx_os_vector *vector;
	hx_string text;
	const hx_string *stored;
	hx_os_string_container_options_init(&options, context);
	CHECK(hx_os_vector_create(context, &options, &vector, NULL) ==
		HX_OS_OK);
	hx_string_init(&text, context);
	CHECK(hx_string_assign_cstr(&text, "中文", NULL) == HX_OS_OK);
	CHECK(hx_os_vector_push_back(vector, &text, NULL) == HX_OS_OK);
	CHECK(hx_string_assign_cstr(&text, "changed", NULL) == HX_OS_OK);
	stored = hx_os_sequence_at(vector, 0);
	CHECK(!strcmp(hx_string_c_str(stored), "中文"));
	CHECK(hx_os_sequence_reserve(vector, 128, NULL) == HX_OS_OK);
	stored = hx_os_sequence_at(vector, 0);
	CHECK(!strcmp(hx_string_c_str(stored), "中文"));
	hx_string_destroy(&text);
	CHECK(hx_os_sequence_destroy(&vector, NULL) == HX_OS_OK);
}

static void test_reserved_path(void)
{
	hx_os_context_options context_options;
	hx_os_container_options options;
	hx_os_context *context;
	hx_os_vector *vector;
	hx_os_deque *deque;
	test_allocator allocator = {0};
	size_t before, i;
	int value, result;
	hx_os_context_options_init(&context_options);
	context_options.allocator.allocate = test_allocate;
	context_options.allocator.deallocate = test_free;
	context_options.allocator.user = &allocator;
	CHECK(hx_os_context_create(&context_options, &context, NULL) ==
		HX_OS_OK);
	hx_os_container_options_init(&options, sizeof(int));
	CHECK(hx_os_vector_create(context, &options, &vector, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_deque_create(context, &options, &deque, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_reserve(vector, 128, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_reserve(deque, 128, NULL) == HX_OS_OK);
	before = allocator.attempts;
	for (i = 0; i < 128; ++i) {
		value = (int)i;
		CHECK(hx_os_vector_push_back(vector, &value, NULL) == HX_OS_OK);
		CHECK(hx_os_queue_push(deque, &value, NULL) == HX_OS_OK);
		CHECK(hx_os_queue_pop(deque, &result, NULL) == HX_OS_OK);
		CHECK(result == value);
	}
	CHECK(allocator.attempts == before);
	CHECK(hx_os_sequence_destroy(&vector, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_destroy(&deque, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(allocator.bytes == 0);
}

static void test_memory_budget(void)
{
	int kind;
	for (kind = 0; kind < 5; ++kind) {
		test_allocator allocator = {0};
		hx_os_context_options context_options;
		hx_os_container_options options;
		hx_os_context *context;
		hx_os_sequence *sequence = NULL;
		hx_os_map *map = NULL;
		unsigned char value[64] = {0};
		size_t baseline, count = 0;
		hx_os_status status;
		hx_os_context_options_init(&context_options);
		context_options.allocator.allocate = test_allocate;
		context_options.allocator.deallocate = test_free;
		context_options.allocator.user = &allocator;
		CHECK(hx_os_context_create(&context_options, &context, NULL) ==
			HX_OS_OK);
		baseline = allocator.bytes;
		hx_os_container_options_init(&options, sizeof(value));
		options.element_alignment = 64;
		options.max_bytes = 4096;
		if (kind == 0)
			status = hx_os_vector_create(
				context, &options, &sequence, NULL);
		else if (kind == 1)
			status = hx_os_deque_create(
				context, &options, &sequence, NULL);
		else if (kind == 2)
			status = hx_os_list_create(
				context, &options, &sequence, NULL);
		else
			status = hx_os_map_create(
				context, kind - 3, &options, NULL, &map, NULL);
		CHECK(status == HX_OS_OK);
		for (;;) {
			const void *stored;
			if (sequence)
				status = hx_os_sequence_push_back(
					sequence, value, NULL);
			else
				status = hx_os_map_insert(map, &count,
					sizeof(count), value, 0, NULL);
			if (status != HX_OS_OK)
				break;
			if (sequence)
				stored = hx_os_sequence_at(sequence, count);
			else
				CHECK(hx_os_map_find(map, &count, sizeof(count),
					      &stored) == HX_OS_OK);
			CHECK(((uintptr_t)stored & 63) == 0);
			++count;
			CHECK(count < 100);
		}
		CHECK(status == HX_OS_LIMIT && count > 0);
		CHECK(allocator.peak <= baseline + options.max_bytes);
		CHECK(hx_os_sequence_destroy(&sequence, NULL) == HX_OS_OK);
		CHECK(hx_os_map_destroy(&map, NULL) == HX_OS_OK);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
		CHECK(allocator.bytes == 0);
	}
}

static void test_string_failure(void)
{
	size_t failure;
	for (failure = 1; failure < 8; ++failure) {
		hx_os_context_options context_options;
		hx_os_container_options options;
		hx_os_context *context;
		hx_os_vector *vector;
		hx_string string;
		hx_os_status status;
		test_allocator allocator = {0};
		hx_os_context_options_init(&context_options);
		context_options.allocator.allocate = test_allocate;
		context_options.allocator.deallocate = test_free;
		context_options.allocator.user = &allocator;
		CHECK(hx_os_context_create(&context_options, &context, NULL) ==
			HX_OS_OK);
		hx_string_init(&string, context);
		CHECK(hx_string_assign_cstr(&string, "中文", NULL) == HX_OS_OK);
		hx_os_string_container_options_init(&options, context);
		CHECK(hx_os_vector_create(context, &options, &vector, NULL) ==
			HX_OS_OK);
		allocator.fail = allocator.attempts + failure;
		status = hx_os_vector_push_back(vector, &string, NULL);
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		CHECK(hx_os_sequence_size(vector) ==
			(status == HX_OS_OK ? 1u : 0u));
		CHECK(hx_os_sequence_destroy(&vector, NULL) == HX_OS_OK);
		hx_string_destroy(&string);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
		CHECK(allocator.bytes == 0);
		if (status == HX_OS_OK)
			break;
	}
	CHECK(failure < 8);
}

int main(void)
{
	hx_os_context *context;
	hx_os_container_options options;
	hx_os_vector *vector;
	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	if (hx_os_capabilities() & HX_OS_CAP_CONTAINERS) {
		int i;
		for (i = 0; i < 3; ++i)
			test_sequence(context, i);
		test_heap_and_sort(context);
		test_map(context, HX_OS_MAP_HASH);
		test_map(context, HX_OS_MAP_ORDERED);
		test_ownership(context);
		test_limits_and_failure();
		test_string_values(context);
		test_reserved_path();
		test_memory_budget();
		test_string_failure();
	}
	else {
		hx_os_container_options_init(&options, sizeof(int));
		CHECK(hx_os_vector_create(context, &options, &vector, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!vector);
	}
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	puts("Container contracts passed");
	return 0;
}
