#include "utility_test.h"
HX_OS_DECLARE_VECTOR(test_vector, uint64_t)
HX_OS_DECLARE_QUEUE(test_queue, double)
HX_OS_DECLARE_VECTOR(test_pointers, const char *)
static void test_strings(void)
{
	hx_string text = HX_STRING_INIT, joined = HX_STRING_INIT;
	hx_string_parts *parts = NULL;
	hx_string_view views[4], invalid = {"\xff", 1};
	size_t count = 99, i;
	CHECK(hx_string_assign_cstr(&text, " \tAB中文😀 cd\r\n", NULL) ==
		HX_OS_OK);
	CHECK(hx_string_trim_ascii(&text, NULL) == HX_OS_OK);
	CHECK(!strcmp(text.data, "AB中文😀 cd"));
	CHECK(hx_string_lower_ascii(&text, NULL) == HX_OS_OK);
	CHECK(!strcmp(text.data, "ab中文😀 cd"));
	CHECK(hx_string_upper_ascii(&text, NULL) == HX_OS_OK);
	CHECK(!strcmp(text.data, "AB中文😀 CD"));
	CHECK(hx_string_starts_with(&text, hx_string_view_from_cstr("AB中文")));
	CHECK(hx_string_ends_with(&text, hx_string_view_from_cstr("😀 CD")));
	CHECK(!hx_string_starts_with(&text, invalid));
	CHECK(hx_string_replace_all(&text, hx_string_view_from_cstr("AB"),
		      hx_string_view_of(&text), &count, NULL) == HX_OS_OK &&
		count == 1);
	CHECK(!strcmp(text.data, "AB中文😀 CD中文😀 CD"));
	CHECK(hx_string_replace_all(&text, hx_string_view_from_cstr(""),
		      hx_string_view_from_cstr("x"), &count,
		      NULL) == HX_OS_INVALID);
	CHECK(count == 1);
	CHECK(hx_string_assign_cstr(&text, ",a,,中文,", NULL) == HX_OS_OK);
	CHECK(hx_string_split_ex(&text, hx_string_view_from_cstr(","), 4, 1,
		      &parts, NULL) == HX_OS_LIMIT &&
		!parts);
	CHECK(hx_string_split(&text, hx_string_view_from_cstr(","), &parts,
		      NULL) == HX_OS_OK);
	CHECK(hx_string_parts_count(parts) == 5);
	hx_string_destroy(&text);
	CHECK(hx_string_parts_at(parts, 3, &views[0]) == HX_OS_OK);
	CHECK(views[0].size == strlen("中文"));
	CHECK(!memcmp(views[0].data, "中文", views[0].size));
	CHECK(hx_string_parts_at(parts, 5, &views[1]) == HX_OS_RANGE);
	hx_string_parts_destroy(&parts);
	CHECK(!parts);
	CHECK(hx_string_assign_cstr(&text, ",a,,b,", NULL) == HX_OS_OK);
	CHECK(hx_string_split_ex(&text, hx_string_view_from_cstr(","), 2, 0,
		      &parts, NULL) == HX_OS_OK);
	CHECK(hx_string_parts_count(parts) == 2);
	for (i = 0; i < 2; ++i)
		CHECK(hx_string_parts_at(parts, i, &views[i]) == HX_OS_OK);
	CHECK(hx_string_join(&joined, views, 2, hx_string_view_from_cstr("😀"),
		      NULL) == HX_OS_OK);
	CHECK(!strcmp(joined.data, "a😀b"));
	views[0] = hx_string_view_of(&joined);
	views[1] = views[0];
	CHECK(hx_string_join(&joined, views, 2, hx_string_view_from_cstr("|"),
		      NULL) == HX_OS_OK);
	CHECK(!strcmp(joined.data, "a😀b|a😀b"));
	CHECK(hx_string_join(&joined, &invalid, 1, hx_string_view_from_cstr(""),
		      NULL) == HX_OS_INVALID_ENCODING);
	CHECK(!strcmp(joined.data, "a😀b|a😀b"));
	hx_string_parts_destroy(&parts);
	views[0].data = "a\0b";
	views[0].size = 3;
	CHECK(hx_string_assign(&text, views[0], NULL) == HX_OS_OK);
	views[1].data = "\0";
	views[1].size = 1;
	CHECK(hx_string_split(&text, views[1], &parts, NULL) == HX_OS_OK);
	CHECK(hx_string_parts_count(parts) == 2);
	hx_string_parts_destroy(&parts);
	hx_string_destroy(&text);
	hx_string_destroy(&joined);
}
static void test_failures(void)
{
	test_memory memory = {0, 0, 0};
	hx_os_context *context = test_context(&memory);
	hx_string text;
	hx_string_parts *parts = NULL;
	size_t live, count = 7;
	hx_string_init(&text, context);
	CHECK(hx_string_assign_cstr(&text, "aaaa", NULL) == HX_OS_OK);
	live = memory.live;
	memory.fail = memory.calls + 1;
	CHECK(hx_string_replace_all(&text, hx_string_view_from_cstr("a"),
		      hx_string_view_from_cstr("longer"), &count,
		      NULL) == HX_OS_NO_MEMORY);
	CHECK(count == 7 && !strcmp(text.data, "aaaa") && memory.live == live);
	memory.fail = memory.calls + 1;
	CHECK(hx_string_split(&text, hx_string_view_from_cstr("a"), &parts,
		      NULL) == HX_OS_NO_MEMORY &&
		!parts);
	memory.fail = 0;
	hx_string_destroy(&text);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!memory.live);
}
static void test_typed_containers(void)
{
	test_vector *vector = NULL;
	test_pointers *pointers = NULL;
	test_queue *queue = NULL;
	uint64_t integer;
	double real;
	if (!(hx_os_capabilities() & HX_OS_CAP_CONTAINERS)) {
		CHECK(test_vector_create(NULL, 2, &vector, NULL) ==
			HX_OS_NOT_SUPPORTED);
		return;
	}
	CHECK(test_vector_create(NULL, 2, &vector, NULL) == HX_OS_OK);
	CHECK(test_vector_push(vector, UINT64_MAX, NULL) == HX_OS_OK);
	CHECK(test_vector_push(vector, 42, NULL) == HX_OS_OK);
	CHECK(test_vector_push(vector, 43, NULL) == HX_OS_LIMIT);
	CHECK(test_vector_size(vector) == 2);
	CHECK(*test_vector_at(vector, 0) == UINT64_MAX);
	CHECK((uintptr_t)test_vector_at(vector, 0) %
			offsetof(test_vector_alignment, value) ==
		0);
	CHECK(test_vector_pop(vector, &integer, NULL) == HX_OS_OK &&
		integer == 42);
	CHECK(test_vector_destroy(&vector, NULL) == HX_OS_OK && !vector);
	CHECK(test_queue_create(NULL, 2, &queue, NULL) == HX_OS_OK);
	CHECK(test_queue_push(queue, 1.5, NULL) == HX_OS_OK);
	CHECK(test_queue_push(queue, 2.5, NULL) == HX_OS_OK);
	CHECK(test_queue_pop(queue, &real, NULL) == HX_OS_OK && real == 1.5);
	CHECK(test_queue_destroy(&queue, NULL) == HX_OS_OK);
	CHECK(test_pointers_create(NULL, 2, &pointers, NULL) == HX_OS_OK);
	CHECK(test_pointers_push(pointers, "borrowed", NULL) == HX_OS_OK);
	CHECK(!strcmp(*test_pointers_at(pointers, 0), "borrowed"));
	CHECK(test_pointers_destroy(&pointers, NULL) == HX_OS_OK);
}
int main(void)
{
	test_strings();
	test_failures();
	test_typed_containers();
	return 0;
}
