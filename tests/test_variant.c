#include <hx_variant.h>
#include <hx_container.h>
#include "variant_fixture.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__,     \
				#expression);                                  \
			abort();                                               \
		}                                                              \
	} while (0)

typedef struct test_allocator {
	size_t attempts;
	size_t fail;
	size_t live;
} test_allocator;
static void *HX_OS_CALL test_allocate(size_t bytes, void *user)
{
	test_allocator *state = user;
	void *memory;
	if (++state->attempts == state->fail)
		return NULL;
	memory = malloc(bytes);
	if (memory)
		++state->live;
	return memory;
}
static void HX_OS_CALL test_free(void *memory, void *user)
{
	test_allocator *state = user;
	CHECK(state->live > 0);
	--state->live;
	free(memory);
}
static hx_os_context *test_context(test_allocator *allocator)
{
	hx_os_context_options options;
	hx_os_context *context = NULL;
	hx_os_context_options_init(&options);
	options.allocator.allocate = test_allocate;
	options.allocator.deallocate = test_free;
	options.allocator.user = allocator;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);
	return context;
}
static hx_string_view test_view(const char *data, size_t size)
{
	hx_string_view view = {data, size};
	return view;
}

typedef struct test_record {
	hx_string name;
	int32_t id;
} test_record;
typedef struct test_record_stats {
	size_t copies;
	size_t drops;
} test_record_stats;
static test_record_stats record_stats;
static hx_os_status HX_OS_CALL test_record_copy(void *destination,
	const void *source, hx_os_context *context, void *user,
	hx_os_error *error)
{
	test_record *out = destination;
	const test_record *input = source;
	hx_os_status status;
	hx_string_init(&out->name, context);
	status = hx_string_copy(&out->name, &input->name, error);
	if (status != HX_OS_OK) {
		hx_string_destroy(&out->name);
		return status;
	}
	out->id = input->id;
	++((test_record_stats *)user)->copies;
	return HX_OS_OK;
}
static void HX_OS_CALL test_record_drop(void *object, void *user)
{
	hx_string_destroy(&((test_record *)object)->name);
	++((test_record_stats *)user)->drops;
}
static const hx_variant_type record_type = {sizeof(test_record), 0,
	test_record_copy, test_record_drop, &record_stats, 0};
static hx_os_status HX_OS_CALL test_nested_copy(void *destination,
	const void *source, hx_os_context *context, void *user,
	hx_os_error *error)
{
	hx_os_status status;
	(void)user;
	hx_variant_init(destination, context);
	status = hx_variant_copy(destination, source, error);
	if (status != HX_OS_OK)
		hx_variant_destroy(destination);
	return status;
}
static void HX_OS_CALL test_nested_drop(void *object, void *user)
{
	(void)user;
	hx_variant_destroy(object);
}
static const hx_variant_type nested_type = {
	sizeof(hx_variant), 0, test_nested_copy, test_nested_drop, NULL, 0};

static void test_scalars(hx_os_context *context, test_allocator *allocator)
{
	hx_variant value, copy = HX_VARIANT_INIT;
	hx_os_error error;
	int boolean = -1, marker = 7;
	int32_t i32 = 0;
	uint32_t u32 = 0;
	int64_t i64 = 0;
	uint64_t u64 = 0;
	float f32 = 0;
	double f64 = 0;
	void *pointer = NULL;
	size_t attempts = allocator->attempts;
	hx_variant_init(&value, context);
	CHECK(hx_variant_kind(&value) == HX_VARIANT_EMPTY);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);
	CHECK(hx_variant_set_bool(&value, -9, NULL) == HX_OS_OK);
	CHECK(hx_variant_get_bool(&value, &boolean, NULL) == HX_OS_OK);
	CHECK(boolean == 1);
	CHECK(hx_variant_set_i32(&value, INT32_MIN, NULL) == HX_OS_OK);
	CHECK(hx_variant_get_i32(&value, &i32, NULL) == HX_OS_OK);
	CHECK(i32 == INT32_MIN);
	i64 = 123;
	CHECK(hx_variant_get_i64(&value, &i64, &error) == HX_OS_TYPE_MISMATCH);
	CHECK(i64 == 123 && error.status == HX_OS_TYPE_MISMATCH);
	CHECK(hx_variant_set_u32(&value, UINT32_MAX, NULL) == HX_OS_OK);
	CHECK(hx_variant_get_u32(&value, &u32, NULL) == HX_OS_OK);
	CHECK(u32 == UINT32_MAX);
	CHECK(hx_variant_set_i64(&value, INT64_MIN, NULL) == HX_OS_OK);
	CHECK(hx_variant_get_i64(&value, &i64, NULL) == HX_OS_OK);
	CHECK(i64 == INT64_MIN);
	CHECK(hx_variant_set_u64(&value, UINT64_MAX, NULL) == HX_OS_OK);
	CHECK(hx_variant_get_u64(&value, &u64, NULL) == HX_OS_OK);
	CHECK(u64 == UINT64_MAX);
	CHECK(hx_variant_set_float(&value, 1.25f, NULL) == HX_OS_OK);
	CHECK(hx_variant_get_float(&value, &f32, NULL) == HX_OS_OK);
	CHECK(f32 == 1.25f);
	CHECK(hx_variant_set_double(&value, -INFINITY, NULL) == HX_OS_OK);
	CHECK(hx_variant_get_double(&value, &f64, NULL) == HX_OS_OK);
	CHECK(isinf(f64) && f64 < 0);
	CHECK(hx_variant_set_double(&value, NAN, NULL) == HX_OS_OK);
	CHECK(hx_variant_get_double(&value, &f64, NULL) == HX_OS_OK);
	CHECK(isnan(f64));
	CHECK(hx_variant_set_pointer(&value, &marker, NULL) == HX_OS_OK);
	CHECK(hx_variant_get_pointer(&value, &pointer, NULL) == HX_OS_OK);
	CHECK(pointer == &marker);
	CHECK(hx_variant_copy(&copy, &value, NULL) == HX_OS_OK);
	CHECK(hx_variant_get_pointer(&copy, &pointer, NULL) == HX_OS_OK);
	CHECK(pointer == &marker);
	hx_variant_clear(&value);
	CHECK(value.context == context && hx_variant_kind(&value) == 0);
	CHECK(hx_variant_copy(&copy, &value, NULL) == HX_OS_OK);
	CHECK(hx_variant_kind(&copy) == HX_VARIANT_EMPTY);
	CHECK(allocator->attempts == attempts);
	CHECK(hx_variant_get_bool(NULL, &boolean, NULL) == HX_OS_INVALID);
	CHECK(hx_variant_set_i32(NULL, 0, NULL) == HX_OS_INVALID);
	CHECK(hx_variant_get_i32(&value, NULL, NULL) == HX_OS_INVALID);
	hx_variant_destroy(&copy);
	hx_variant_destroy(&value);
	hx_variant_destroy(&value);
}
static void test_strings(hx_os_context *context)
{
	hx_variant value, copy;
	hx_string output = HX_STRING_INIT, input;
	hx_string_view view;
	const char text[] = "中文😀\0end";
	const char invalid[] = {(char)0xc0, (char)0xaf};
	hx_variant_init(&value, context);
	hx_variant_init(&copy, context);
	hx_string_init(&input, context);
	CHECK(hx_variant_set_string(&value, test_view(text, sizeof(text) - 1),
		      NULL) == HX_OS_OK);
	CHECK(hx_variant_copy(&copy, &value, NULL) == HX_OS_OK);
	CHECK(hx_variant_get_string_view(&copy, &view, NULL) == HX_OS_OK);
	CHECK(view.size == sizeof(text) - 1 &&
		!memcmp(view.data, text, view.size));
	CHECK(hx_variant_set_string(&value, view, NULL) == HX_OS_OK);
	CHECK(hx_variant_get_string_view(&value, &view, NULL) == HX_OS_OK);
	/* Input can borrow the outgoing value, including a substring. */
	CHECK(hx_variant_set_string(&value,
		      test_view(view.data + 3, view.size - 3),
		      NULL) == HX_OS_OK);
	CHECK(hx_variant_set_string(&value, test_view(invalid, sizeof(invalid)),
		      NULL) == HX_OS_INVALID_ENCODING);
	CHECK(hx_variant_get_string(&value, &output, NULL) == HX_OS_OK);
	CHECK(hx_string_size_bytes(&output) == sizeof(text) - 4);
	CHECK(hx_variant_set_cstr(&copy, "changed", NULL) == HX_OS_OK);
	CHECK(hx_string_assign_cstr(&input, "owned", NULL) == HX_OS_OK);
	CHECK(hx_variant_take_string(&value, &input, NULL) == HX_OS_OK);
	CHECK(!hx_string_size_bytes(&input));
	CHECK(hx_variant_get_string_view(&value, &view, NULL) == HX_OS_OK);
	CHECK(view.size == 5 && !memcmp(view.data, "owned", 5));
	CHECK(hx_variant_take_string(&value, &value.data.string, NULL) ==
		HX_OS_OK);
	CHECK(hx_variant_set_cstr(&value, "", NULL) == HX_OS_OK);
	CHECK(hx_variant_get_string_view(&value, &view, NULL) == HX_OS_OK);
	CHECK(view.data && !view.size && !view.data[0]);
	hx_variant_destroy(&value);
	hx_variant_destroy(&copy);
	hx_string_destroy(&output);
	hx_string_destroy(&input);
}
static hx_os_status HX_OS_CALL test_construct_fail(
	void *out, hx_os_context *context, void *user, hx_os_error *error)
{
	(void)out;
	(void)context;
	(void)user;
	CHECK(error && error->status == HX_OS_OK);
	*error = (hx_os_error){HX_OS_IO_ERROR, HX_OS_ERROR_ERRNO, 71, 19};
	return HX_OS_IO_ERROR;
}
static hx_os_status HX_OS_CALL test_construct_i32(
	void *out, hx_os_context *context, void *user, hx_os_error *error)
{
	(void)context;
	(void)error;
	*(int32_t *)out = *(const int32_t *)user;
	return HX_OS_OK;
}
static hx_os_status HX_OS_CALL test_visit(
	const hx_variant *value, void *user, hx_os_error *error)
{
	int32_t result;
	CHECK(error && error->status == HX_OS_OK);
	CHECK(hx_variant_get_i32(value, &result, NULL) == HX_OS_OK);
	*(int32_t *)user = result;
	*error = (hx_os_error){HX_OS_IO_ERROR, HX_OS_ERROR_ERRNO, 72, 20};
	return HX_OS_IO_ERROR;
}
static void test_custom(hx_os_context *context)
{
	hx_variant value, copy;
	test_point point = {1.25, 3.5};
	const hx_variant_type other_point = test_point_type;
	hx_variant_type descriptor;
	const void *object = NULL, *sentinel = &point;
	void *mutable_object = NULL;
	hx_os_error error;
	int32_t input = 42, visited = 0;
	size_t allocations;
	hx_variant_init(&value, context);
	hx_variant_init(&copy, context);
	CHECK(test_variant_point_type() == &test_point_type);
	CHECK(hx_variant_set_custom(&value, &test_point_type, &point, NULL) ==
		HX_OS_OK);
	point.x = 99;
	CHECK(hx_variant_get_custom(&value, &test_point_type, &object, NULL) ==
		HX_OS_OK);
	CHECK(((const test_point *)object)->x == 1.25);
	CHECK(hx_variant_get_custom(&value, &other_point, &sentinel, NULL) ==
		HX_OS_TYPE_MISMATCH);
	CHECK(sentinel == &point);
	CHECK(hx_variant_copy(&copy, &value, NULL) == HX_OS_OK);
	CHECK(hx_variant_get_custom_mut(&copy, &test_point_type,
		      &mutable_object, NULL) == HX_OS_OK);
	CHECK(mutable_object != object);
	((test_point *)mutable_object)->x = 7;
	CHECK(((const test_point *)object)->x == 1.25);
	CHECK(hx_variant_set_custom(&value, &test_point_type, object, NULL) ==
		HX_OS_OK);
	CHECK(hx_variant_copy(&value, &value, NULL) == HX_OS_OK);
	allocations = hx_os_context_allocations(context);
	CHECK(hx_variant_emplace_custom(&value, &test_point_type,
		      test_construct_fail, NULL, &error) == HX_OS_IO_ERROR);
	CHECK(error.native_code == 71 && error.cause_code == 19);
	CHECK(hx_os_context_allocations(context) == allocations);
	CHECK(hx_variant_custom_type(&value) == &test_point_type);
	descriptor = test_point_type;
	descriptor.size = SIZE_MAX;
	CHECK(hx_variant_set_custom(&value, &descriptor, &point, NULL) ==
		HX_OS_RANGE);
	descriptor = test_point_type;
	descriptor.alignment = 3;
	CHECK(hx_variant_set_custom(&value, &descriptor, &point, NULL) ==
		HX_OS_INVALID);
	descriptor = test_point_type;
	descriptor.flags = 32;
	CHECK(hx_variant_set_custom(&value, &descriptor, &point, NULL) ==
		HX_OS_INVALID);
	descriptor = test_point_type;
	descriptor.alignment = 64;
	CHECK(hx_variant_set_custom(&value, &descriptor, &point, NULL) ==
		HX_OS_OK);
	CHECK(hx_variant_get_custom(&value, &descriptor, &object, NULL) ==
		HX_OS_OK);
	CHECK(!((uintptr_t)object & 63));
	hx_variant_clear(&value);
	descriptor.size = sizeof(input);
	descriptor.flags = HX_VARIANT_TYPE_NO_COPY;
	CHECK(hx_variant_emplace_custom(&value, &descriptor, test_construct_i32,
		      &input, NULL) == HX_OS_OK);
	CHECK(hx_variant_copy(&copy, &value, NULL) == HX_OS_NOT_SUPPORTED);
	CHECK(hx_variant_custom_type(&copy) == &test_point_type);
	CHECK(hx_variant_set_i32(&value, input, NULL) == HX_OS_OK);
	CHECK(hx_variant_visit(&value, test_visit, &visited, &error) ==
		HX_OS_IO_ERROR);
	CHECK(visited == 42 && error.native_code == 72 &&
		error.cause_code == 20);
	CHECK(hx_variant_visit(&value, NULL, NULL, NULL) == HX_OS_INVALID);
	hx_variant_destroy(&value);
	hx_variant_destroy(&copy);
}
static void test_owned_failure(void)
{
	size_t fail;
	for (fail = 1; fail < 8; ++fail) {
		test_allocator allocator = {0};
		hx_os_context *context = test_context(&allocator);
		hx_variant value, copy;
		test_record record;
		hx_os_status status;
		const void *object;
		void *mutable_object;
		int32_t previous = 0;
		hx_variant_init(&value, context);
		hx_variant_init(&copy, context);
		hx_string_init(&record.name, NULL);
		record.id = 17;
		CHECK(hx_string_assign_cstr(&record.name, "姓名😀", NULL) ==
			HX_OS_OK);
		CHECK(hx_variant_set_i32(&value, 9, NULL) == HX_OS_OK);
		allocator.fail = allocator.attempts + fail;
		status = hx_variant_set_custom(
			&value, &record_type, &record, NULL);
		if (status != HX_OS_OK) {
			CHECK(status == HX_OS_NO_MEMORY);
			CHECK(hx_variant_get_i32(&value, &previous, NULL) ==
					HX_OS_OK &&
				previous == 9);
			CHECK(hx_os_context_allocations(context) == 0);
		}
		else {
			allocator.fail = 0;
			CHECK(hx_variant_copy(&copy, &value, NULL) == HX_OS_OK);
			CHECK(hx_variant_get_custom_mut(&value, &record_type,
				      &mutable_object, NULL) == HX_OS_OK);
			CHECK(hx_string_assign_cstr(
				      &((test_record *)mutable_object)->name,
				      "changed", NULL) == HX_OS_OK);
			CHECK(hx_variant_get_custom_mut(&copy, &record_type,
				      &mutable_object, NULL) == HX_OS_OK);
			object = mutable_object;
			CHECK(!strcmp(
				hx_string_c_str(
					&((const test_record *)object)->name),
				"姓名😀"));
			/* Taking a nested string must move before dropping its
			 * owner. */
			CHECK(hx_variant_take_string(&copy,
				      &((test_record *)mutable_object)->name,
				      NULL) == HX_OS_OK);
		}
		hx_variant_destroy(&value);
		hx_variant_destroy(&copy);
		hx_string_destroy(&record.name);
		CHECK(hx_os_context_allocations(context) == 0);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
		CHECK(allocator.live == 0);
		if (status == HX_OS_OK)
			break;
	}
	CHECK(fail < 8 && record_stats.copies == record_stats.drops);
}
static void test_move_contexts(void)
{
	test_allocator first = {0}, second = {0};
	hx_os_context *a = test_context(&first), *b = test_context(&second);
	hx_variant left, right;
	hx_string string;
	size_t attempts;
	hx_variant_init(&left, a);
	hx_variant_init(&right, b);
	hx_string_init(&string, b);
	CHECK(hx_string_assign_cstr(&string, "different allocator", NULL) ==
		HX_OS_OK);
	attempts = first.attempts + second.attempts;
	CHECK(hx_variant_take_string(&left, &string, NULL) == HX_OS_OK);
	CHECK(first.attempts + second.attempts == attempts);
	CHECK(left.context == a && left.data.string.context == b);
	CHECK(hx_variant_copy(&right, &left, NULL) == HX_OS_OK);
	hx_variant_swap(&left, &right);
	CHECK(left.context == b && right.context == a);
	hx_variant_move(&left, &right);
	CHECK(left.context == a && !right.context);
	hx_variant_move(&left, &left);
	hx_variant_destroy(&left);
	hx_variant_destroy(&right);
	hx_string_destroy(&string);
	CHECK(hx_os_context_destroy(&a, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&b, NULL) == HX_OS_OK);
	CHECK(!first.live && !second.live);
}
static void test_nested_aliases(hx_os_context *context)
{
	hx_variant owner, input;
	void *nested = NULL;
	int32_t result = 0;
	hx_variant_init(&owner, context);
	hx_variant_init(&input, context);
	CHECK(hx_variant_set_i32(&input, 29, NULL) == HX_OS_OK);
	CHECK(hx_variant_set_custom(&owner, &nested_type, &input, NULL) ==
		HX_OS_OK);
	CHECK(hx_variant_get_custom_mut(&owner, &nested_type, &nested, NULL) ==
		HX_OS_OK);
	/* The copied scalar source is freed when its old owner is dropped. */
	CHECK(hx_variant_copy(&owner, nested, NULL) == HX_OS_OK);
	CHECK(hx_variant_get_i32(&owner, &result, NULL) == HX_OS_OK &&
		result == 29);
	CHECK(hx_variant_set_cstr(&input, "nested move", NULL) == HX_OS_OK);
	CHECK(hx_variant_set_custom(&owner, &nested_type, &input, NULL) ==
		HX_OS_OK);
	CHECK(hx_variant_get_custom_mut(&owner, &nested_type, &nested, NULL) ==
		HX_OS_OK);
	hx_variant_move(&owner, nested);
	CHECK(hx_variant_kind(&owner) == HX_VARIANT_STRING);
	hx_variant_destroy(&owner);
	hx_variant_destroy(&input);
}
static void test_container_failure(void)
{
	size_t fail;
	for (fail = 1; fail < 12; ++fail) {
		test_allocator allocator = {0};
		hx_os_context *context = test_context(&allocator);
		hx_os_container_options options;
		hx_os_vector *vector = NULL;
		hx_variant value;
		test_record record;
		hx_os_status status;
		hx_variant_init(&value, context);
		hx_string_init(&record.name, NULL);
		record.id = 17;
		CHECK(hx_string_assign_cstr(&record.name, "owned", NULL) ==
			HX_OS_OK);
		CHECK(hx_variant_set_custom(
			      &value, &record_type, &record, NULL) == HX_OS_OK);
		hx_variant_container_options_init(&options, context);
		CHECK(hx_os_vector_create(context, &options, &vector, NULL) ==
			HX_OS_OK);
		allocator.fail = allocator.attempts + fail;
		status = hx_os_vector_push_back(vector, &value, NULL);
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		CHECK(hx_os_sequence_size(vector) ==
			(status == HX_OS_OK ? 1u : 0u));
		CHECK(hx_os_sequence_destroy(&vector, NULL) == HX_OS_OK);
		hx_variant_destroy(&value);
		hx_string_destroy(&record.name);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
		CHECK(!allocator.live &&
			record_stats.copies == record_stats.drops);
		if (status == HX_OS_OK)
			break;
	}
	CHECK(fail < 12);
}
static void test_containers(hx_os_context *context)
{
	hx_os_container_options options;
	hx_os_vector *vector = NULL;
	hx_os_queue *queue = NULL;
	hx_variant value, removed;
	test_point point = {2, 4};
	const hx_variant *stored;
	const void *before, *after;
	hx_variant_init(&value, context);
	hx_variant_container_options_init(&options, context);
	options.max_items = 8;
	CHECK(hx_os_vector_create(context, &options, &vector, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_deque_create(context, &options, &queue, NULL) == HX_OS_OK);
	CHECK(hx_variant_set_custom(&value, &test_point_type, &point, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_vector_push_back(vector, &value, NULL) == HX_OS_OK);
	stored = hx_os_sequence_at(vector, 0);
	CHECK(hx_variant_get_custom(stored, &test_point_type, &before, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_sequence_reserve(vector, 8, NULL) == HX_OS_OK);
	stored = hx_os_sequence_at(vector, 0);
	CHECK(hx_variant_get_custom(stored, &test_point_type, &after, NULL) ==
			HX_OS_OK &&
		before == after);
	CHECK(hx_variant_set_cstr(&value, "queue string", NULL) == HX_OS_OK);
	CHECK(hx_os_queue_push(queue, &value, NULL) == HX_OS_OK);
	CHECK(hx_os_queue_push(queue, stored, NULL) == HX_OS_OK);
	CHECK(hx_os_queue_pop(queue, &removed, NULL) == HX_OS_OK);
	CHECK(hx_variant_kind(&removed) == HX_VARIANT_STRING);
	hx_variant_destroy(&removed);
	CHECK(hx_os_queue_pop(queue, &removed, NULL) == HX_OS_OK);
	CHECK(hx_variant_custom_type(&removed) == &test_point_type);
	hx_variant_destroy(&removed);
	hx_variant_destroy(&value);
	CHECK(hx_os_sequence_destroy(&queue, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_destroy(&vector, NULL) == HX_OS_OK);
}
static void test_disabled(void)
{
	hx_variant value = HX_VARIANT_INIT, copy = HX_VARIANT_INIT;
	hx_string string = HX_STRING_INIT;
	hx_string_view view = {"sentinel", 8};
	test_point point = {1, 2};
	const void *object = &point;
	int32_t integer = 17;
	CHECK(hx_variant_set_i32(&value, 9, NULL) == HX_OS_NOT_SUPPORTED);
	CHECK(hx_variant_get_i32(&value, &integer, NULL) ==
		HX_OS_NOT_SUPPORTED);
	CHECK(integer == 17);
	CHECK(hx_variant_set_cstr(&value, "hello", NULL) ==
		HX_OS_NOT_SUPPORTED);
	CHECK(hx_variant_take_string(&value, &string, NULL) ==
		HX_OS_NOT_SUPPORTED);
	CHECK(hx_variant_get_string_view(&value, &view, NULL) ==
		HX_OS_NOT_SUPPORTED);
	CHECK(view.size == 8);
	CHECK(hx_variant_set_custom(&value, &test_point_type, &point, NULL) ==
		HX_OS_NOT_SUPPORTED);
	CHECK(hx_variant_get_custom(&value, &test_point_type, &object, NULL) ==
			HX_OS_NOT_SUPPORTED &&
		object == &point);
	CHECK(hx_variant_copy(&copy, &value, NULL) == HX_OS_NOT_SUPPORTED);
	CHECK(hx_variant_visit(&value, test_visit, &integer, NULL) ==
		HX_OS_NOT_SUPPORTED);
	hx_variant_move(&value, &copy);
	hx_variant_destroy(&value);
	hx_variant_destroy(&copy);
	hx_string_destroy(&string);
}
int main(void)
{
	test_allocator allocator = {0};
	hx_os_context *context = test_context(&allocator);
	struct test_alignment {
		char prefix;
		hx_variant value;
	};
	if (hx_os_capabilities() & HX_OS_CAP_VARIANT) {
		test_scalars(context, &allocator);
		test_strings(context);
		test_custom(context);
		test_owned_failure();
		test_move_contexts();
		test_nested_aliases(context);
		if (hx_os_capabilities() & HX_OS_CAP_CONTAINERS) {
			test_containers(context);
			test_container_failure();
		}
	}
	else
		test_disabled();
	CHECK(!hx_os_context_allocations(context));
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(!allocator.live);
	printf("Variant contracts passed; value=%zu align=%zu type=%zu\n",
		sizeof(hx_variant), offsetof(struct test_alignment, value),
		sizeof(hx_variant_type));
	return 0;
}
