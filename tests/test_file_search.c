#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

typedef struct search_memory {
	size_t calls;
	size_t fail;
	size_t live;
	size_t peak;
} search_memory;
typedef union search_allocation {
	long double number;
	void *pointer;
	size_t bytes;
} search_allocation;
static void *HX_OS_CALL search_allocate(size_t bytes, void *user)
{
	search_memory *state = user;
	search_allocation *allocation;
	if (++state->calls == state->fail)
		return NULL;
	allocation = malloc(sizeof(*allocation) + bytes);
	if (!allocation)
		return NULL;
	allocation->bytes = bytes;
	state->live += bytes;
	if (state->live > state->peak)
		state->peak = state->live;
	return allocation + 1;
}
static void HX_OS_CALL search_free(void *memory, void *user)
{
	search_allocation *allocation = (search_allocation *)memory - 1;
	search_memory *state = user;
	CHECK(state->live >= allocation->bytes);
	state->live -= allocation->bytes;
	free(allocation);
}
typedef struct search_seen {
	const char *root;
	size_t calls;
	hx_os_status next;
} search_seen;
static hx_os_status HX_OS_CALL search_visit(
	hx_string_view path, void *user, hx_os_error *error)
{
	search_seen *seen = user;
	hx_os_file_info info;
	CHECK(error && error->status == HX_OS_OK);
	CHECK(path.data && path.data[path.size] == 0);
	CHECK(path.size > strlen(seen->root));
	CHECK(!memcmp(path.data, seen->root, strlen(seen->root)));
	CHECK(hx_os_path_stat(path.data, 0, &info, NULL) == HX_OS_OK);
	CHECK(info.kind == HX_OS_FILE_REGULAR);
	++seen->calls;
	if (seen->next != HX_OS_OK)
		*error = (hx_os_error){seen->next, HX_OS_ERROR_ERRNO, 71, 19};
	return seen->next;
}
static void search_create_file(const char *path)
{
	hx_os_file *file = NULL;
	CHECK(hx_os_file_open(NULL, path,
		      HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
			      HX_OS_FILE_EXCLUSIVE,
		      &file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
}
static void search_child_file(const char *root, const char *name)
{
	char path[2048];
	int size = snprintf(path, sizeof(path), "%s/%s", root, name);
	CHECK(size > 0 && (size_t)size < sizeof(path));
	search_create_file(path);
}
static void search_child_directory(const char *root, const char *name)
{
	char path[2048];
	int size = snprintf(path, sizeof(path), "%s/%s", root, name);
	CHECK(size > 0 && (size_t)size < sizeof(path));
	CHECK(hx_os_directory_create(path, 0, NULL) == HX_OS_OK);
}
static void search_basic(hx_os_context *context, const char *root)
{
	search_seen seen = {root, 0, HX_OS_OK};
	hx_os_walk_options options = HX_OS_WALK_OPTIONS_INIT;
	hx_os_error error;
	hx_string first;
	size_t completed = 999;
	hx_string_init(&first, context);
	CHECK(hx_string_assign_cstr(&first, "unchanged", NULL) == HX_OS_OK);
	CHECK(hx_os_directory_find_first_file(root, "absent", &first, NULL) ==
		HX_OS_NOT_FOUND);
	CHECK(!strcmp(hx_string_c_str(&first), "unchanged"));
	CHECK(hx_os_directory_find_first_file(root, "中文.txt", &first, NULL) ==
		HX_OS_OK);
	CHECK(strstr(hx_string_c_str(&first), "/sub/中文.txt") != NULL);
	hx_string_destroy(&first);
	CHECK(hx_os_directory_find_files_ex(context, root, "*.txt", NULL,
		      search_visit, &seen, &completed, NULL) == HX_OS_OK);
	CHECK(completed == 4 && seen.calls == 4);
	seen.calls = 0;
	CHECK(hx_os_directory_find_files(
		      root, "?.txt", search_visit, &seen, NULL) == HX_OS_OK &&
		seen.calls == 1);
	seen.calls = 0;
	CHECK(hx_os_directory_find_files(
		      root, ".*.txt", search_visit, &seen, NULL) == HX_OS_OK &&
		seen.calls == 1);
	seen.calls = 0;
	CHECK(hx_os_directory_find_files(root, "no-match", search_visit, &seen,
		      NULL) == HX_OS_OK &&
		!seen.calls);
	seen.next = HX_OS_EOF;
	CHECK(hx_os_directory_find_files_ex(context, root, "*.txt", NULL,
		      search_visit, &seen, &completed, &error) == HX_OS_OK);
	CHECK(completed == 1 && error.status == HX_OS_OK && !error.native_code);
	seen.next = HX_OS_CANCELLED;
	CHECK(hx_os_directory_find_files_ex(context, root, "*.txt", NULL,
		      search_visit, &seen, &completed,
		      &error) == HX_OS_CANCELLED);
	CHECK(completed == 1 && error.native_code == 71 &&
		error.cause_code == 19);
	seen.next = HX_OS_OK;
	options.max_results = 1;
	CHECK(hx_os_directory_find_files_ex(context, root, "*.txt", &options,
		      search_visit, &seen, &completed, NULL) == HX_OS_LIMIT);
	CHECK(completed == 1);
	options.max_results = 16;
	options.max_entries = 1;
	CHECK(hx_os_directory_find_files_ex(context, root, "no-match", &options,
		      search_visit, &seen, &completed, NULL) == HX_OS_LIMIT);
	CHECK(completed == 0);
	options.max_entries = 100;
	options.max_depth = 1;
	CHECK(hx_os_directory_find_files_ex(context, root, "no-match", &options,
		      search_visit, &seen, &completed, NULL) == HX_OS_LIMIT);
	options.max_depth = 4;
	options.max_bytes = 1;
	CHECK(hx_os_directory_find_files_ex(context, root, "*.txt", &options,
		      search_visit, &seen, &completed, NULL) == HX_OS_LIMIT);
	CHECK(completed == 0);
	CHECK(hx_os_directory_find_files(root, "**/*.txt", search_visit, &seen,
		      NULL) == HX_OS_NOT_SUPPORTED);
	CHECK(hx_os_directory_find_files(root, "[ab]", search_visit, &seen,
		      NULL) == HX_OS_NOT_SUPPORTED);
	CHECK(hx_os_directory_find_files(root, "", search_visit, &seen, NULL) ==
		HX_OS_INVALID);
	CHECK(hx_os_directory_find_files(root, "*", NULL, NULL, NULL) ==
		HX_OS_INVALID);
	CHECK(hx_os_directory_find_files(root, "\xc0\xaf", search_visit, &seen,
		      NULL) == HX_OS_INVALID_ENCODING);
}
static void search_failure(hx_os_context *context, search_memory *memory,
	const char *root, size_t baseline)
{
	size_t failure;
	for (failure = 1; failure < 80; ++failure) {
		search_seen seen = {root, 0, HX_OS_OK};
		hx_os_status status;
		memory->fail = memory->calls + failure;
		status = hx_os_directory_find_files_ex(context, root, "*.txt",
			NULL, search_visit, &seen, NULL, NULL);
		CHECK(memory->live == baseline);
		CHECK(!hx_os_context_allocations(context));
		if (status == HX_OS_OK)
			break;
		CHECK(status == HX_OS_NO_MEMORY);
	}
	CHECK(failure < 80);
	memory->fail = 0;
}
static void search_wide(hx_os_context *context, search_memory *memory,
	const char *root, size_t baseline)
{
	const size_t entries = 2048;
	search_seen seen = {root, 0, HX_OS_OK};
	size_t i, before = memory->calls, small, large, small_peak;
	hx_os_deadline start;
	memory->peak = baseline;
	CHECK(hx_os_directory_find_files_ex(context, root, "*.txt", NULL,
		      search_visit, &seen, NULL, NULL) == HX_OS_OK);
	small = memory->calls - before;
	small_peak = memory->peak;
	for (i = 0; i < entries; ++i) {
		char name[64];
		(void)snprintf(name, sizeof(name), "entry-%04zu.txt", i);
		search_child_file(root, name);
	}
	seen.calls = 0;
	memory->peak = baseline;
	before = memory->calls;
	start = hx_os_monotonic_ns();
	CHECK(hx_os_directory_find_files_ex(context, root, "*.txt", NULL,
		      search_visit, &seen, NULL, NULL) == HX_OS_OK);
	large = memory->calls - before;
	CHECK(seen.calls == entries + 4);
	CHECK(large <= small + 2);
	CHECK(memory->peak <= small_peak + 1024);
	CHECK(memory->live == baseline);
	printf("search: matches=%zu elapsed_ns=%llu allocations=%zu/%zu "
	       "peak_bytes=%zu/%zu (includes callback stat)\n",
		seen.calls, (unsigned long long)(hx_os_monotonic_ns() - start),
		small, large, small_peak - baseline, memory->peak - baseline);
}
static void search_deep(hx_os_context *context, const char *root)
{
	hx_os_walk_options options = HX_OS_WALK_OPTIONS_INIT;
	char path[2048];
	search_seen seen = {root, 0, HX_OS_OK};
	size_t length, i, completed;
	length = (size_t)snprintf(path, sizeof(path), "%s/deep", root);
	CHECK(hx_os_directory_create(path, 0, NULL) == HX_OS_OK);
	for (i = 0; i < 96; ++i) {
		memcpy(path + length, "/d", 3);
		length += 2;
		CHECK(hx_os_directory_create(path, 0, NULL) == HX_OS_OK);
	}
	search_child_file(path, "target.bin");
	CHECK(hx_os_directory_find_files(root, "target.bin", search_visit,
		      &seen, NULL) == HX_OS_LIMIT);
	CHECK(seen.calls == 0);
	options.max_depth = 128;
	CHECK(hx_os_directory_find_files_ex(context, root, "target.bin",
		      &options, search_visit, &seen, &completed,
		      NULL) == HX_OS_OK);
	CHECK(completed == 1 && seen.calls == 1);
}
static void search_links_and_encoding(const char *root)
{
#if !defined(_WIN32)
	char link[2048], invalid[2048], outside[2048];
	search_seen seen = {root, 0, HX_OS_OK};
	size_t length;
	(void)snprintf(link, sizeof(link), "%s/cycle", root);
	CHECK(symlink(".", link) == 0);
	(void)snprintf(outside, sizeof(outside), "%s-outside", root);
	CHECK(hx_os_directory_create(outside, 0, NULL) == HX_OS_OK);
	search_child_file(outside, "escaped.txt");
	(void)snprintf(link, sizeof(link), "%s/outside", root);
	{
		char target[sizeof(outside) + 3];
		(void)snprintf(target, sizeof(target), "%s%s",
			outside[0] == '/' ? "" : "../", outside);
		CHECK(symlink(target, link) == 0);
	}
	CHECK(hx_os_directory_find_files(root, "escaped.txt", search_visit,
		      &seen, NULL) == HX_OS_OK &&
		seen.calls == 0);
	length = (size_t)snprintf(invalid, sizeof(invalid), "%s/bad-", root);
	invalid[length++] = (char)0xff;
	invalid[length] = 0;
	{
		FILE *file = fopen(invalid, "wb");
		CHECK(file && !fclose(file));
	}
	CHECK(hx_os_directory_find_files(root, "*", search_visit, &seen,
		      NULL) == HX_OS_INVALID_ENCODING);
	CHECK(unlink(invalid) == 0);
	CHECK(hx_os_directory_remove_recursive(outside, NULL) == HX_OS_OK);
#else
	(void)root;
#endif
}
int main(void)
{
	hx_os_context_options options;
	hx_os_context *context = NULL;
	search_memory memory = {0};
	hx_os_walk_options cleanup = HX_OS_WALK_OPTIONS_INIT;
	char root[128];
	size_t baseline, completed = 19;
	if (!(hx_os_capabilities() & HX_OS_CAP_FILE_SEARCH)) {
		hx_string first = HX_STRING_INIT;
		CHECK(hx_string_assign_cstr(&first, "unchanged", NULL) ==
			HX_OS_OK);
		CHECK(hx_os_directory_find_first_file(".", "*", &first, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(!strcmp(hx_string_c_str(&first), "unchanged"));
		hx_string_destroy(&first);
		CHECK(hx_os_directory_find_files_ex(NULL, ".", "*", NULL,
			      search_visit, NULL, &completed,
			      NULL) == HX_OS_NOT_SUPPORTED);
		CHECK(completed == 0);
		return 0;
	}
	(void)snprintf(root, sizeof(root),
#if !defined(_WIN32) && !defined(__ANDROID__)
		"/tmp/hx-search-%llx",
#else
		"hx-search-%llx",
#endif
		(unsigned long long)hx_os_monotonic_ns());
	CHECK(hx_os_directory_create(root, 0, NULL) == HX_OS_OK);
	search_child_file(root, "alpha.txt");
	search_child_file(root, ".hidden.txt");
	search_child_file(root, "upper.TXT");
	search_child_directory(root, "sub");
	search_child_directory(root, "sub/inner");
	search_child_directory(root, ".hidden");
	search_child_file(root, "sub/inner/😀.txt");
	search_child_file(root, "sub/中文.txt");
	search_child_file(root, ".hidden/config.txt");
	hx_os_context_options_init(&options);
	options.allocator =
		(hx_os_allocator){search_allocate, search_free, &memory};
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);
	baseline = memory.live;
	search_basic(context, root);
	search_failure(context, &memory, root, baseline);
	search_links_and_encoding(root);
	search_wide(context, &memory, root, baseline);
	search_deep(context, root);
	cleanup.max_depth = 128;
	CHECK(hx_os_directory_remove_recursive_with_options(
		      root, &cleanup, NULL) == HX_OS_OK);
	CHECK(memory.live == baseline && !hx_os_context_allocations(context));
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!memory.live);
	puts("Recursive file search contracts passed");
	return 0;
}
