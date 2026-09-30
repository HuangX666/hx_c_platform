#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

static void *fail_allocate(size_t bytes, void *user)
{
	return *(int *)user ? NULL : malloc(bytes);
}
static void fail_free(void *memory, void *user)
{
	(void)user;
	free(memory);
}

int main(void)
{
	hx_os_pages *pages = NULL;
	hx_os_file_map *map = NULL;
	hx_os_file *file = NULL, *pipe_reader = NULL, *pipe_writer = NULL;
	hx_os_context *context = NULL, *failed_context = NULL;
	hx_os_context_options options;
	unsigned char data[8193], readback[8193], *view;
	size_t i, page, completed;
	int fail = 0;
	uint64_t cursor;
	char path[128];
#if defined(_WIN32)
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	page = info.dwPageSize;
#else
	page = (size_t)sysconf(_SC_PAGESIZE);
#endif
	if (!(hx_os_capabilities() & HX_OS_CAP_VIRTUAL_MEMORY)) {
		CHECK(hx_os_pages_alloc(NULL, 1, HX_OS_PAGE_READ, &pages,
			      NULL) == HX_OS_NOT_SUPPORTED &&
			!pages);
		CHECK(hx_os_file_map_open("unused", HX_OS_MAP_READ, &map,
			      NULL) == HX_OS_NOT_SUPPORTED &&
			!map);
		return 0;
	}
	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	hx_os_context_options_init(&options);
	options.allocator.allocate = fail_allocate;
	options.allocator.deallocate = fail_free;
	options.allocator.user = &fail;
	CHECK(hx_os_context_create(&options, &failed_context, NULL) ==
		HX_OS_OK);
	fail = 1;
	CHECK(hx_os_pages_alloc(failed_context, 1, HX_OS_PAGE_READ, &pages,
		      NULL) == HX_OS_NO_MEMORY &&
		!pages);
	CHECK(hx_os_pages_alloc(context, SIZE_MAX, HX_OS_PAGE_READ, &pages,
		      NULL) == HX_OS_RANGE);
	CHECK(hx_os_pages_alloc(context, 1, HX_OS_PAGE_WRITE, &pages, NULL) ==
		HX_OS_NOT_SUPPORTED);
	CHECK(hx_os_pages_alloc(context, 1, 128, &pages, NULL) ==
		HX_OS_INVALID);
	CHECK(hx_os_pages_alloc(context, page + 1,
		      HX_OS_PAGE_READ | HX_OS_PAGE_WRITE, &pages,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_pages_size(pages) == page * 2);
	view = hx_os_pages_data(pages);
	CHECK((uintptr_t)view % page == 0);
	for (i = 0; i < page * 2; ++i)
		CHECK(view[i] == 0);
	memset(view, 0x5a, page * 2);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);
	CHECK(hx_os_pages_protect(pages, HX_OS_PAGE_READ, NULL) == HX_OS_OK);
	CHECK(view[page] == 0x5a);
	{
		hx_os_error native;
		hx_os_status executable = hx_os_pages_protect(
			pages, HX_OS_PAGE_READ | HX_OS_PAGE_EXECUTE, &native);
		CHECK(executable == HX_OS_OK ||
			native.domain != HX_OS_ERROR_NONE);
	}
	CHECK(hx_os_pages_protect(pages, HX_OS_PAGE_NONE, NULL) == HX_OS_OK);
	CHECK(hx_os_pages_protect(pages, HX_OS_PAGE_READ | HX_OS_PAGE_WRITE,
		      NULL) == HX_OS_OK);
	CHECK(view[page] == 0x5a);
	CHECK(hx_os_pages_free(&pages, NULL) == HX_OS_OK && !pages);
	CHECK(hx_os_pages_free(&pages, NULL) == HX_OS_OK);
	(void)snprintf(path, sizeof(path), "hx-map-%llx-中文.bin",
		(unsigned long long)hx_os_monotonic_ns());
	for (i = 0; i < sizeof(data); ++i)
		data[i] = (unsigned char)(i % 251);
	CHECK(hx_os_file_open(context, path,
		      HX_OS_FILE_READ | HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
			      HX_OS_FILE_EXCLUSIVE,
		      &file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_map_create(context, file, 0, 0, HX_OS_MAP_READ, &map,
		      NULL) == HX_OS_EOF &&
		!map);
	CHECK(hx_os_file_write_all(
		      file, data, sizeof(data), &completed, NULL) == HX_OS_OK);
	CHECK(hx_os_file_seek(file, 7, HX_OS_SEEK_BEGIN, &cursor, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_map_create(failed_context, file, 0, 0, HX_OS_MAP_READ,
		      &map, NULL) == HX_OS_NO_MEMORY &&
		!map);
	CHECK(hx_os_file_map_create(context, file, UINT64_MAX, 1,
		      HX_OS_MAP_READ, &map, NULL) == HX_OS_RANGE &&
		!map);
	CHECK(hx_os_file_map_create(context, file, 0, sizeof(data) + 1,
		      HX_OS_MAP_READ, &map, NULL) == HX_OS_RANGE &&
		!map);
	CHECK(hx_os_file_map_create(context, file, 3, 4097, HX_OS_MAP_READ,
		      &map, NULL) == HX_OS_OK);
	CHECK(hx_os_file_seek(file, 0, HX_OS_SEEK_CURRENT, &cursor, NULL) ==
			HX_OS_OK &&
		cursor == 7);
	CHECK(!hx_os_file_map_write_view(map));
	CHECK(hx_os_file_map_flush(map, 0, NULL) == HX_OS_NOT_SUPPORTED);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);
	CHECK(hx_os_file_map_size(map) == 4097);
	CHECK(!memcmp(hx_os_file_map_view(map), data + 3, 4097));
	CHECK(hx_os_file_map_destroy(&map, NULL) == HX_OS_OK && !map);
	CHECK(hx_os_file_open(NULL, path, HX_OS_FILE_READ, &file, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_map_create(context, file, 0, 0, HX_OS_MAP_SHARED_WRITE,
		      &map, NULL) == HX_OS_ACCESS_DENIED);
	CHECK(hx_os_file_map_create(context, file, 1, 33,
		      HX_OS_MAP_PRIVATE_COPY, &map, NULL) == HX_OS_OK);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	view = hx_os_file_map_write_view(map);
	CHECK(view);
	memset(view, 0xee, 33);
	CHECK(hx_os_file_map_flush(map, 1, NULL) == HX_OS_NOT_SUPPORTED);
	CHECK(hx_os_file_map_destroy(&map, NULL) == HX_OS_OK);
	CHECK(hx_os_file_open(NULL, path, HX_OS_FILE_READ | HX_OS_FILE_WRITE,
		      &file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_read_some(file, readback, sizeof(readback), &completed,
		      NULL) == HX_OS_OK &&
		completed == sizeof(data));
	CHECK(!memcmp(data, readback, sizeof(data)));
	CHECK(hx_os_file_map_create(context, file, 2, 10,
		      HX_OS_MAP_SHARED_WRITE, &map, NULL) == HX_OS_OK);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	memset(hx_os_file_map_write_view(map), 0xdd, 10);
	CHECK(hx_os_file_map_flush(map, 1, NULL) == HX_OS_OK);
	CHECK(hx_os_file_map_destroy(&map, NULL) == HX_OS_OK);
	CHECK(hx_os_file_map_open(path, HX_OS_MAP_READ, &map, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_map_size(map) == sizeof(data));
	view = (unsigned char *)hx_os_file_map_view(map);
	CHECK(view[1] == data[1] && view[2] == 0xdd && view[11] == 0xdd &&
		view[12] == data[12]);
	CHECK(hx_os_file_map_destroy(&map, NULL) == HX_OS_OK);
	CHECK(hx_os_file_open(NULL, path,
		      HX_OS_FILE_READ | HX_OS_FILE_WRITE | HX_OS_FILE_APPEND,
		      &file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_map_create(context, file, 0, 1, HX_OS_MAP_SHARED_WRITE,
		      &map, NULL) == HX_OS_NOT_SUPPORTED);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	if (hx_os_capabilities() & HX_OS_CAP_PIPE) {
		CHECK(hx_os_pipe_pair_anonymous(NULL, &pipe_reader,
			      &pipe_writer, NULL) == HX_OS_OK);
		CHECK(hx_os_file_map_create(context, pipe_reader, 0, 1,
			      HX_OS_MAP_READ, &map,
			      NULL) == HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_file_close(&pipe_reader, NULL) == HX_OS_OK);
		CHECK(hx_os_file_close(&pipe_writer, NULL) == HX_OS_OK);
	}
	CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&failed_context, NULL) == HX_OS_OK);
	puts("pages and file mapping contracts passed");
	return 0;
}
