#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

typedef struct test_record {
	int key;
	int serial;
} test_record;

static int test_compare(const void *a, const void *b, void *user)
{
	const test_record *left = a, *right = b;
	(void)user;
	return (left->key > right->key) - (left->key < right->key);
}
static int test_even(const void *value, void *user)
{
	(void)user;
	return !(((const test_record *)value)->key % 2);
}
static hx_os_status test_visit(
	const void *value, void *user, hx_os_error *error)
{
	CHECK(value);
	CHECK(hx_os_sequence_reverse(user, NULL) == HX_OS_BUSY);
	*error = (hx_os_error){HX_OS_CANCELLED, HX_OS_ERROR_ERRNO, 77, 23};
	return HX_OS_CANCELLED;
}
static void test_sequence(int kind)
{
	hx_os_container_options options;
	hx_os_sequence *sequence;
	hx_os_sequence_iterator iterator;
	hx_os_status status;
	hx_os_error error;
	size_t index, count, removed;
	int key = -1, serial = -1;
	hx_os_container_options_init(&options, sizeof(test_record));
	options.compare = test_compare;
	if (kind == 0)
		status = hx_os_vector_create(NULL, &options, &sequence, NULL);
	else if (kind == 1)
		status = hx_os_deque_create(NULL, &options, &sequence, NULL);
	else
		status = hx_os_list_create(NULL, &options, &sequence, NULL);
	CHECK(status == HX_OS_OK);
	for (index = 0; index < 200; ++index) {
		test_record value = {(int)(index % 19), (int)index};
		CHECK(hx_os_sequence_push_back(sequence, &value, NULL) ==
			HX_OS_OK);
	}
	CHECK(hx_os_sequence_stable_sort(sequence, NULL) == HX_OS_OK);
	HX_OS_SEQUENCE_FOR_EACH(iterator, sequence, status)
	{
		const void *value;
		const test_record *record;
		CHECK(hx_os_sequence_value(&iterator, &value) == HX_OS_OK);
		record = value;
		CHECK(record->key >= key);
		CHECK(record->key != key || record->serial > serial);
		key = record->key;
		serial = record->serial;
	}
	CHECK(status == HX_OS_EOF);
	if (kind == 0) {
		test_record needle = {7, 0};
		size_t lower, upper;
		int found;
		CHECK(hx_os_vector_lower_bound(
			      sequence, &needle, &lower, &found) == HX_OS_OK);
		CHECK(hx_os_vector_upper_bound(sequence, &needle, &upper) ==
			HX_OS_OK);
		CHECK(found && upper - lower == 11);
	}
	CHECK(hx_os_sequence_visit(sequence, test_visit, sequence, &count,
		      &error) == HX_OS_CANCELLED);
	CHECK(count == 1 && error.native_code == 77 && error.cause_code == 23);
	CHECK(hx_os_sequence_count_if(sequence, test_even, NULL, &count) ==
		HX_OS_OK);
	CHECK(hx_os_sequence_remove_if(
		      sequence, test_even, NULL, &removed, NULL) == HX_OS_OK);
	CHECK(count == removed && hx_os_sequence_size(sequence) == 200 - count);
	CHECK(hx_os_sequence_unique(sequence, &removed, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_size(sequence) == 9);
	CHECK(hx_os_sequence_find_if(sequence, test_even, NULL, &iterator) ==
		HX_OS_NOT_FOUND);
	key = 19;
	HX_OS_SEQUENCE_FOR_EACH_REVERSE(iterator, sequence, status)
	{
		const void *value;
		CHECK(hx_os_sequence_value(&iterator, &value) == HX_OS_OK);
		key -= 2;
		CHECK(((const test_record *)value)->key == key);
	}
	CHECK(status == HX_OS_EOF && key == 1);
	CHECK(hx_os_sequence_reverse(sequence, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_rotate(sequence, 3, NULL) == HX_OS_OK);
	CHECK(((const test_record *)hx_os_sequence_at(sequence, 0))->key == 11);
	CHECK(hx_os_sequence_begin(sequence, &iterator) == HX_OS_OK);
	CHECK(hx_os_sequence_erase_at(sequence, &iterator, NULL, NULL) ==
		HX_OS_OK);
	{
		const void *value;
		CHECK(hx_os_sequence_value(&iterator, &value) == HX_OS_OK);
		CHECK(((const test_record *)value)->key == 9);
	}
	if (kind == 2) {
		hx_os_list *other;
		test_record tail = {99, 0};
		size_t before;
		CHECK(hx_os_list_create(NULL, &options, &other, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_sequence_push_back(other, &tail, NULL) == HX_OS_OK);
		before = hx_os_sequence_memory(sequence) +
			hx_os_sequence_memory(other);
		CHECK(hx_os_list_splice(other, 0, sequence, 1, 2, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_sequence_size(other) == 3 &&
			hx_os_sequence_size(sequence) == 6);
		CHECK(((const test_record *)hx_os_sequence_at(other, 0))->key ==
			7);
		CHECK(((const test_record *)hx_os_sequence_at(other, 1))->key ==
			5);
		CHECK(before ==
			hx_os_sequence_memory(sequence) +
				hx_os_sequence_memory(other));
		CHECK(hx_os_list_splice(sequence, 6, sequence, 0, 2, NULL) ==
			HX_OS_OK);
		CHECK(((const test_record *)hx_os_sequence_at(sequence, 0))
				->key == 1);
		CHECK(hx_os_sequence_destroy(&other, NULL) == HX_OS_OK);
	}
	CHECK(hx_os_sequence_destroy(&sequence, NULL) == HX_OS_OK);
}

typedef struct test_memory {
	size_t live;
	int fail;
} test_memory;
static void *test_allocate(size_t bytes, void *user)
{
	test_memory *memory = user;
	void *value;
	if (memory->fail)
		return NULL;
	value = malloc(bytes);
	if (value)
		++memory->live;
	return value;
}
static void test_free(void *value, void *user)
{
	test_memory *memory = user;
	CHECK(value && memory->live);
	--memory->live;
	free(value);
}
static void test_sort_failure(void)
{
	test_memory memory = {0};
	hx_os_context_options context_options;
	hx_os_container_options options;
	hx_os_context *context;
	hx_os_vector *vector;
	hx_os_sequence_iterator iterator;
	test_record record = {3, 0};
	const void *view;
	size_t live;
	hx_os_context_options_init(&context_options);
	context_options.allocator =
		(hx_os_allocator){test_allocate, test_free, &memory};
	CHECK(hx_os_context_create(&context_options, &context, NULL) ==
		HX_OS_OK);
	hx_os_container_options_init(&options, sizeof(record));
	options.compare = test_compare;
	CHECK(hx_os_vector_create(context, &options, &vector, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_sequence_push_back(vector, &record, NULL) == HX_OS_OK);
	record.key = 1;
	CHECK(hx_os_sequence_push_back(vector, &record, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_begin(vector, &iterator) == HX_OS_OK);
	live = memory.live;
	memory.fail = 1;
	CHECK(hx_os_sequence_stable_sort(vector, NULL) == HX_OS_NO_MEMORY);
	CHECK(memory.live == live);
	CHECK(hx_os_sequence_value(&iterator, &view) == HX_OS_OK);
	CHECK(((const test_record *)view)->key == 3);
	memory.fail = 0;
	CHECK(hx_os_sequence_stable_sort(vector, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_value(&iterator, &view) == HX_OS_INVALID);
	CHECK(hx_os_sequence_destroy(&vector, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!memory.live);
}

static hx_os_status test_transform(
	void *destination, const void *source, void *user, hx_os_error *error)
{
	if (user && --*(int *)user == 0) {
		*error =
			(hx_os_error){HX_OS_IO_ERROR, HX_OS_ERROR_ERRNO, 13, 9};
		return HX_OS_IO_ERROR;
	}
	*(test_record *)destination = *(const test_record *)source;
	((test_record *)destination)->key *= 2;
	return HX_OS_OK;
}
static hx_os_status test_fold(
	void *accumulator, const void *value, void *user, hx_os_error *error)
{
	(void)user;
	(void)error;
	*(int *)accumulator += ((const test_record *)value)->key;
	return HX_OS_OK;
}
static void test_algorithms(void)
{
	hx_os_container_options options;
	hx_os_vector *source, *copy;
	size_t i, completed;
	int total = 0, fail = 2;
	hx_os_error error;
	hx_os_container_options_init(&options, sizeof(test_record));
	options.compare = test_compare;
	CHECK(hx_os_vector_create(NULL, &options, &source, NULL) == HX_OS_OK);
	for (i = 0; i < 257; ++i) {
		test_record record = {(int)((i * 97) % 257), (int)i};
		CHECK(hx_os_sequence_push_back(source, &record, NULL) ==
			HX_OS_OK);
	}
	CHECK(hx_os_sequence_clone(source, &copy, NULL) == HX_OS_OK);
	CHECK(hx_os_vector_partial_sort(copy, 17, NULL) == HX_OS_OK);
	for (i = 0; i < 17; ++i)
		CHECK(((const test_record *)hx_os_sequence_at(copy, i))->key ==
			(int)i);
	CHECK(hx_os_sequence_destroy(&copy, NULL) == HX_OS_OK);
	for (i = 0; i < 257; i += 16) {
		size_t j;
		CHECK(hx_os_sequence_clone(source, &copy, NULL) == HX_OS_OK);
		CHECK(hx_os_vector_nth_element(copy, i, NULL) == HX_OS_OK);
		CHECK(((const test_record *)hx_os_sequence_at(copy, i))->key ==
			(int)i);
		for (j = 0; j < 257; ++j) {
			int key = ((const test_record *)hx_os_sequence_at(
					   copy, j))
					  ->key;
			CHECK(j < i ? key <= (int)i : key >= (int)i);
		}
		CHECK(hx_os_sequence_destroy(&copy, NULL) == HX_OS_OK);
	}
	CHECK(hx_os_sequence_copy_if(source, test_even, NULL, &copy, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_sequence_size(copy) == 129);
	CHECK(hx_os_sequence_destroy(&copy, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_transform(source, &options, test_transform, &fail,
		      &copy, &error) == HX_OS_IO_ERROR);
	CHECK(!copy && error.native_code == 13 && error.cause_code == 9);
	CHECK(hx_os_sequence_transform(source, &options, test_transform, NULL,
		      &copy, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_fold(copy, &total, test_fold, NULL, &completed,
		      NULL) == HX_OS_OK);
	CHECK(completed == 257 && total == 257 * 256);
	CHECK(hx_os_sequence_size(source) == 257);
	CHECK(hx_os_sequence_destroy(&copy, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_destroy(&source, NULL) == HX_OS_OK);
}
static void test_merge(void)
{
	const int a[] = {1, 1, 3}, b[] = {1, 2, 3, 3};
	const int expected[][7] = {
		{1, 1, 1, 2, 3, 3, 3}, {1, 1, 2, 3, 3}, {1, 3}, {1}, {1, 2, 3}};
	const size_t lengths[] = {7, 5, 2, 1, 3};
	hx_os_container_options options;
	hx_os_list *left, *right;
	size_t i;
	int operation;
	hx_os_container_options_init(&options, sizeof(test_record));
	options.compare = test_compare;
	CHECK(hx_os_list_create(NULL, &options, &left, NULL) == HX_OS_OK);
	CHECK(hx_os_list_create(NULL, &options, &right, NULL) == HX_OS_OK);
	for (i = 0; i < 3; ++i) {
		test_record record = {a[i], (int)i};
		CHECK(hx_os_sequence_push_back(left, &record, NULL) ==
			HX_OS_OK);
	}
	for (i = 0; i < 4; ++i) {
		test_record record = {b[i], (int)i};
		CHECK(hx_os_sequence_push_back(right, &record, NULL) ==
			HX_OS_OK);
	}
	for (operation = HX_OS_MERGE;
		operation <= HX_OS_SET_SYMMETRIC_DIFFERENCE; ++operation) {
		hx_os_vector *result;
		CHECK(hx_os_sequence_merge(left, right, operation, &result,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_sequence_size(result) == lengths[operation]);
		for (i = 0; i < lengths[operation]; ++i)
			CHECK(((const test_record *)hx_os_sequence_at(
				       result, i))
					->key == expected[operation][i]);
		CHECK(hx_os_sequence_destroy(&result, NULL) == HX_OS_OK);
	}
	CHECK(hx_os_sequence_destroy(&left, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_destroy(&right, NULL) == HX_OS_OK);
}

static void test_partition_replace(int kind)
{
	hx_os_sequence *sequence;
	hx_os_container_options options;
	size_t i, boundary, replaced;
	int result;
	hx_os_container_options_init(&options, sizeof(test_record));
	CHECK((kind == 0 ? hx_os_vector_create(NULL, &options, &sequence, NULL)
			      : kind == 1
			      ? hx_os_deque_create(
					NULL, &options, &sequence, NULL)
			      : hx_os_list_create(NULL, &options, &sequence,
					NULL)) == HX_OS_OK);
	CHECK(hx_os_sequence_all_of(sequence, test_even, NULL, &result) ==
			HX_OS_OK &&
		result);
	CHECK(hx_os_sequence_any_of(sequence, test_even, NULL, &result) ==
			HX_OS_OK &&
		!result);
	for (i = 0; i < 99; ++i) {
		test_record value = {(int)i, 0};
		CHECK(hx_os_sequence_push_back(sequence, &value, NULL) ==
			HX_OS_OK);
	}
	CHECK(hx_os_sequence_partition(
		      sequence, test_even, NULL, &boundary, NULL) == HX_OS_OK);
	CHECK(boundary == 50);
	for (i = 0; i < 99; ++i)
		CHECK(test_even(hx_os_sequence_at(sequence, i), NULL) ==
			(i < boundary));
	/* Replacement borrows an element that itself matches the predicate. */
	CHECK(hx_os_sequence_replace_if(sequence, test_even, NULL,
		      hx_os_sequence_at(sequence, 0), &replaced,
		      NULL) == HX_OS_OK &&
		replaced == 50);
	for (i = 0; i < boundary; ++i)
		CHECK(((const test_record *)hx_os_sequence_at(sequence, i))
				->key == 0);
	CHECK(hx_os_sequence_none_of(sequence, test_even, NULL, &result) ==
			HX_OS_OK &&
		!result);
	CHECK(hx_os_sequence_destroy(&sequence, NULL) == HX_OS_OK);
}

int main(void)
{
	if (!(hx_os_capabilities() & HX_OS_CAP_CONTAINERS)) {
		CHECK(hx_os_map_validate(NULL) == HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_sequence_stable_sort(NULL, NULL) ==
			HX_OS_NOT_SUPPORTED);
		return 0;
	}
	test_sequence(0);
	test_sequence(1);
	test_sequence(2);
	test_sort_failure();
	test_algorithms();
	test_merge();
	test_partition_replace(0);
	test_partition_replace(1);
	test_partition_replace(2);
	puts("STL sequence algorithms passed");
	return 0;
}
