#include "hx_os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "check failed at line %d: %s\n",       \
				__LINE__, #value);                             \
			exit(1);                                               \
		}                                                              \
	} while (0)

static void test_string(hx_os_context *context)
{
	hx_string string;
	hx_string copy;
	hx_string_view view;
	unsigned char encoded[16];
	hx_os_text_result result;
	hx_string_init(&string, context);
	hx_string_init(&copy, context);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);
	CHECK(hx_string_assign_cstr(&string, "中文", NULL) == HX_OS_OK);
	CHECK(hx_string_size_bytes(&string) == 6);
	CHECK(hx_string_codepoint_count(&string) == 2);
	CHECK(hx_string_erase(&string, 1, 1, NULL) == HX_OS_INVALID);
	CHECK(hx_string_append(&string, hx_string_view_of(&string), NULL) ==
		HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&string), "中文中文"));
	CHECK(hx_string_substring(&string, &string, 3, 6, NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&string), "文中"));
	CHECK(hx_string_copy(&copy, &string, NULL) == HX_OS_OK);
	CHECK(hx_string_format(&string, NULL, "%s-%d", hx_string_c_str(&string),
		      42) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&string), "文中-42"));
	CHECK(hx_string_append_cstr(&string, "\xc0\x80", NULL) ==
		HX_OS_INVALID_ENCODING);
	CHECK(!strcmp(hx_string_c_str(&string), "文中-42"));
	view.data = "a\0b";
	view.size = 3;
	CHECK(hx_string_assign(&copy, view, NULL) == HX_OS_OK);
	CHECK(hx_string_size_bytes(&copy) == 3);
	hx_string_move(&string, &copy);
	CHECK(hx_string_size_bytes(&string) == 3);
	CHECK(hx_string_size_bytes(&copy) == 0);
	CHECK(hx_string_assign_encoded(&string, HX_OS_UTF16LE,
		      "\x2d\x4e\x87\x65", 4, NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&string), "中文"));
	CHECK(hx_string_encode(&string, HX_OS_UTF16BE, encoded, sizeof(encoded),
		      &result, NULL) == HX_OS_OK);
	CHECK(result.output_used == 4);
	CHECK(!memcmp(encoded, "\x4e\x2d\x65\x87", 4));
	CHECK(hx_string_assign_encoded(&string, HX_OS_UTF16LE, "\x00\xd8", 2,
		      NULL) == HX_OS_TRUNCATED_INPUT);
	CHECK(!strcmp(hx_string_c_str(&string), "中文"));
	hx_string_destroy(&copy);
	hx_string_destroy(&string);
}

static void test_memory(hx_os_context *context)
{
	hx_os_memory_pool_options options;
	hx_os_memory_pool *pool;
	hx_os_memory_stats stats;
	hx_os_object_pool *objects;
	hx_os_arena *arena;
	void *blocks[128];
	void *memory;
	size_t i;
	hx_os_memory_pool_options_init(&options);
	options.block_bytes = 4096;
	options.maximum_bytes = 65536;
	options.cached_bytes = 8192;
	CHECK(hx_os_memory_pool_create(context, &options, &pool, NULL) ==
		HX_OS_OK);
	for (i = 0; i < 128; ++i) {
		CHECK(hx_os_memory_pool_alloc(pool, 33, 64, &blocks[i], NULL) ==
			HX_OS_OK);
		CHECK(((uintptr_t)blocks[i] & 63) == 0);
		memset(blocks[i], (int)i, 33);
	}
	CHECK(hx_os_memory_pool_destroy(&pool, NULL) == HX_OS_BUSY);
	CHECK(hx_os_memory_pool_alloc(pool, 1048576, 0, &memory, NULL) ==
		HX_OS_LIMIT);
	for (i = 0; i < 128; ++i) {
		size_t j;
		for (j = 0; j < 33; ++j)
			CHECK(((unsigned char *)blocks[i])[j] ==
				(unsigned char)i);
		hx_os_memory_pool_free(pool, blocks[i]);
	}
	CHECK(hx_os_memory_pool_alloc(pool, 17, 128, &memory, NULL) ==
		HX_OS_OK);
	memset(memory, 0x47, 17);
	CHECK(hx_os_memory_pool_realloc(pool, &memory, 4096, NULL) == HX_OS_OK);
	CHECK(((uintptr_t)memory & 127) == 0);
	CHECK(((unsigned char *)memory)[16] == 0x47);
	hx_os_memory_pool_free(pool, memory);
	hx_os_memory_pool_collect(pool);
	hx_os_memory_pool_stats(pool, &stats);
	CHECK(!stats.live_allocations && !stats.requested_bytes);
	CHECK(stats.reserved_bytes <= options.maximum_bytes);
	CHECK(hx_os_memory_pool_destroy(&pool, NULL) == HX_OS_OK);
	CHECK(hx_os_object_pool_create(context, 20, 32, 1, &objects, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_object_pool_acquire(objects, &memory, NULL) == HX_OS_OK);
	CHECK(hx_os_object_pool_acquire(objects, &blocks[0], NULL) ==
		HX_OS_LIMIT);
	hx_os_object_pool_release(objects, memory);
	CHECK(hx_os_object_pool_destroy(&objects, NULL) == HX_OS_OK);
	CHECK(hx_os_arena_create(context, 1024, 4096, &arena, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_arena_alloc(arena, 100, 256, &memory, NULL) == HX_OS_OK);
	CHECK(((uintptr_t)memory & 255) == 0);
	CHECK(hx_os_arena_alloc(arena, 8192, 0, &memory, NULL) == HX_OS_LIMIT);
	hx_os_arena_reset(arena);
	CHECK(hx_os_arena_alloc(arena, 100, 64, &memory, NULL) == HX_OS_OK);
	hx_os_arena_destroy(arena);
}

static void test_encoding(void)
{
	unsigned char output[128];
	hx_os_text_result result;
	hx_os_text_converter *converter;
	size_t required;
	const char utf8[] = "中文";
	CHECK(hx_os_text_measure(HX_OS_UTF8, HX_OS_UTF16LE, utf8,
		      sizeof(utf8) - 1, &required, NULL) == HX_OS_OK);
	CHECK(required == 4);
	CHECK(hx_os_text_convert(HX_OS_UTF8, HX_OS_UTF16LE, utf8,
		      sizeof(utf8) - 1, output, sizeof(output), &result,
		      NULL) == HX_OS_OK);
	CHECK(result.output_used == 4);
	CHECK(!memcmp(output, "\x2d\x4e\x87\x65", 4));
	CHECK(hx_os_text_converter_create(NULL, HX_OS_UTF8, HX_OS_UTF16LE,
		      HX_OS_TEXT_STRICT, &converter, NULL) == HX_OS_OK);
	CHECK(hx_os_text_converter_update(converter, utf8, 2, output,
		      sizeof(output), 0, &result, NULL) == HX_OS_NEED_INPUT);
	CHECK(result.input_used == 0);
	CHECK(hx_os_text_converter_update(converter, utf8, 6, output, 2, 1,
		      &result, NULL) == HX_OS_OUTPUT_FULL);
	CHECK(result.input_used == 3 && result.output_used == 2);
	CHECK(hx_os_text_converter_update(converter, utf8 + 3, 3, output + 2, 2,
		      1, &result, NULL) == HX_OS_OK);
	hx_os_text_converter_destroy(converter);
	CHECK(hx_os_text_convert(HX_OS_UTF8, HX_OS_ASCII, utf8, 6, output,
		      sizeof(output), &result, NULL) == HX_OS_UNREPRESENTABLE);
	CHECK(result.error_offset == 0 && result.input_used == 0);
	CHECK(hx_os_text_converter_create(NULL, HX_OS_UTF8, HX_OS_ASCII,
		      HX_OS_TEXT_REPLACE, &converter, NULL) == HX_OS_OK);
	CHECK(hx_os_text_converter_update(converter, utf8, 6, output,
		      sizeof(output), 1, &result, NULL) == HX_OS_OK);
	CHECK(result.replacements == 2 && result.output_used == 2);
	CHECK(!memcmp(output, "??", 2));
	hx_os_text_converter_destroy(converter);
	if (hx_os_capabilities() & HX_OS_CAP_LEGACY_TEXT) {
		CHECK(hx_os_encoding_supported(HX_OS_GBK));
		CHECK(hx_os_text_convert(HX_OS_UTF8, HX_OS_GBK, utf8, 6, output,
			      sizeof(output), &result, NULL) == HX_OS_OK);
		CHECK(result.output_used == 4);
		CHECK(!memcmp(output, "\xd6\xd0\xce\xc4", 4));
		CHECK(hx_os_text_convert(HX_OS_SHIFT_JIS, HX_OS_UTF8,
			      "\x82\xa0", 2, output, sizeof(output), &result,
			      NULL) == HX_OS_OK);
		CHECK(result.output_used == 3 && !memcmp(output, "あ", 3));
		CHECK(hx_os_text_convert(HX_OS_EUC_KR, HX_OS_UTF8, "\xb0\xa1",
			      2, output, sizeof(output), &result,
			      NULL) == HX_OS_OK);
		CHECK(result.output_used == 3 && !memcmp(output, "가", 3));
		CHECK(hx_os_text_convert(HX_OS_WINDOWS_1252, HX_OS_UTF8, "\x80",
			      1, output, sizeof(output), &result,
			      NULL) == HX_OS_OK);
		CHECK(result.output_used == 3 && !memcmp(output, "€", 3));
	}
}

static void test_files(hx_os_context *context)
{
	const char *directory_name = "hx_os_contract_中文";
	const char *path = "hx_os_contract_中文/data.txt";
	const char *moved = "hx_os_contract_中文/moved.txt";
	hx_os_file *file;
	hx_os_directory *directory;
	hx_os_status status;
	unsigned char buffer[16];
	char name[64];
	size_t count;
	size_t required;
	uint64_t size;
	CHECK(hx_os_directory_create(directory_name, 1, NULL) == HX_OS_OK);
	CHECK(hx_os_file_open(context, path,
		      HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
			      HX_OS_FILE_TRUNCATE,
		      &file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_write_all(file, "abc", 3, &count, NULL) == HX_OS_OK);
	CHECK(count == 3);
	CHECK(hx_os_file_sync(file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_open(context, path,
		      HX_OS_FILE_WRITE | HX_OS_FILE_APPEND, &file,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_file_write_all(file, "def", 3, &count, NULL) == HX_OS_OK);
	CHECK(hx_os_file_sync(file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_size(file, &size, NULL) == HX_OS_OK && size == 6);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_open(context, path, HX_OS_FILE_READ, &file, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_read_some(
		      file, buffer, sizeof(buffer), &count, NULL) == HX_OS_OK);
	CHECK(count == 6 && !memcmp(buffer, "abcdef", 6));
	CHECK(hx_os_file_read_some(
		      file, buffer, sizeof(buffer), &count, NULL) == HX_OS_EOF);
	CHECK(count == 0);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_open(context, directory_name, &directory, NULL) ==
		HX_OS_OK);
	status = hx_os_directory_next(directory, name, 1, &required, NULL);
	CHECK(status == HX_OS_OUTPUT_FULL);
	CHECK(required == strlen("data.txt") + 1);
	CHECK(hx_os_directory_next(directory, name, sizeof(name), &required,
		      NULL) == HX_OS_OK);
	CHECK(!strcmp(name, "data.txt"));
	hx_os_directory_close(directory);
	CHECK(hx_os_file_move(path, moved, 1, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(moved, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_remove(directory_name, NULL) == HX_OS_OK);
}

static void test_long_paths(hx_os_context *context)
{
	char path[1024] = "hx_os_long_中文_😀";
	size_t lengths[8];
	size_t i;
	hx_os_file *file;
	hx_os_error error;
	lengths[0] = strlen(path);
	for (i = 1; i < 8; ++i) {
		strcat(path,
			"/abcdefghijklmnopqrstuvwxyz_0123456789_abcdefghij");
		lengths[i] = strlen(path);
	}
	CHECK(strlen(path) > 260);
	CHECK(hx_os_directory_create(path, 1, &error) == HX_OS_OK);
	strcat(path, "/内容_😀.txt");
	CHECK(hx_os_file_open(context, path,
		      HX_OS_FILE_CREATE | HX_OS_FILE_WRITE |
			      HX_OS_FILE_EXCLUSIVE,
		      &file, &error) == HX_OS_OK);
	CHECK(hx_os_file_close(&file, &error) == HX_OS_OK);
	CHECK(hx_os_file_remove(path, &error) == HX_OS_OK);
	for (i = 8; i > 0; --i) {
		path[lengths[i - 1]] = 0;
		CHECK(hx_os_directory_remove(path, &error) == HX_OS_OK);
	}
	CHECK(hx_os_file_open(context, "bad\xc0\x80", HX_OS_FILE_READ, &file,
		      &error) == HX_OS_INVALID_ENCODING);
	CHECK(file == NULL && error.status == HX_OS_INVALID_ENCODING);
}

int main(void)
{
	hx_os_context *context;
	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	test_string(context);
	if (hx_os_capabilities() & HX_OS_CAP_MEMORY_POOLS)
		test_memory(context);
	test_encoding();
	test_files(context);
	test_long_paths(context);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	puts("string, memory, text and file contracts passed");
	return 0;
}
