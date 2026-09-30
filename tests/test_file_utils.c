#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <unistd.h>
#include <sys/stat.h>
#else
#include <windows.h>
#endif

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

static void check_view(hx_string_view value, const char *expected)
{
	CHECK(value.size == strlen(expected));
	CHECK(!memcmp(value.data, expected, value.size));
}

static void check_paths(void)
{
	hx_string_view view;
	CHECK(hx_os_path_basename("a/b.txt/", &view, NULL) == HX_OS_OK);
	check_view(view, "b.txt");
	CHECK(hx_os_path_dirname("a/b.txt/", &view, NULL) == HX_OS_OK);
	check_view(view, "a");
	CHECK(hx_os_path_extension("a/b.txt", &view, NULL) == HX_OS_OK);
	check_view(view, ".txt");
	CHECK(hx_os_path_extension(".hidden", &view, NULL) == HX_OS_OK);
	check_view(view, "");
	CHECK(hx_os_path_dirname("name", &view, NULL) == HX_OS_OK);
	check_view(view, ".");
	CHECK(hx_os_path_dirname("C:\\name", &view, NULL) == HX_OS_OK);
	check_view(view, "C:\\");
	CHECK(hx_os_path_basename("/", &view, NULL) == HX_OS_OK);
	check_view(view, "/");
	CHECK(hx_os_path_basename("", &view, NULL) == HX_OS_INVALID);
}

static void check_glob(const char *root)
{
	char pattern[384];
	hx_os_vector *matches = NULL;
	hx_os_walk_options options = HX_OS_WALK_OPTIONS_INIT;
	(void)snprintf(pattern, sizeof(pattern), "%s/?.txt", root);
	if (!(hx_os_capabilities() & HX_OS_CAP_GLOB)) {
		CHECK(hx_os_path_glob(NULL, pattern, &matches, NULL) ==
			HX_OS_NOT_SUPPORTED);
		return;
	}
	CHECK(hx_os_path_glob(NULL, pattern, &matches, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_size(matches) == 1);
	CHECK(hx_os_sequence_destroy(&matches, NULL) == HX_OS_OK);
	(void)snprintf(pattern, sizeof(pattern), "%s/*/*.txt", root);
	CHECK(hx_os_path_glob(NULL, pattern, &matches, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_size(matches) == 1);
	CHECK(hx_os_sequence_destroy(&matches, NULL) == HX_OS_OK);
	options.max_results = 1;
	(void)snprintf(pattern, sizeof(pattern), "%s/*", root);
	CHECK(hx_os_path_glob_with_options(
		      NULL, pattern, &options, &matches, NULL) == HX_OS_LIMIT &&
		!matches);
	(void)snprintf(pattern, sizeof(pattern), "%s/no-such/*", root);
	CHECK(hx_os_path_glob(NULL, pattern, &matches, NULL) == HX_OS_OK);
	CHECK(hx_os_sequence_size(matches) == 0);
	CHECK(hx_os_sequence_destroy(&matches, NULL) == HX_OS_OK);
}

static void check_links(const char *root, const char *outside)
{
	char link_path[384], nested[384];
	(void)snprintf(nested, sizeof(nested), "%s/tree", root);
	CHECK(hx_os_directory_create(nested, 0, NULL) == HX_OS_OK);
	(void)snprintf(link_path, sizeof(link_path), "%s/tree/link", root);
#if defined(_WIN32)
	{
		wchar_t wide_link[384], wide_target[384];
		CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			      link_path, -1, wide_link, 384) > 0);
		CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			      outside, -1, wide_target, 384) > 0);
		if (!CreateSymbolicLinkW(wide_link, wide_target,
			    SYMBOLIC_LINK_FLAG_DIRECTORY)) {
			CHECK(GetLastError() == ERROR_PRIVILEGE_NOT_HELD);
			puts("SKIP: creating Windows symlinks needs privilege");
		}
	}
#else
	CHECK(symlink(outside, link_path) == 0);
#endif
	CHECK(hx_os_directory_remove_recursive(nested, NULL) == HX_OS_OK);
}

static void check_long_path(const char *root)
{
	hx_string path = HX_STRING_INIT, text = HX_STRING_INIT;
	char component[151];
	memset(component, 'a', sizeof(component) - 1);
	component[sizeof(component) - 1] = 0;
	CHECK(hx_string_assign_cstr(&path, root, NULL) == HX_OS_OK);
	CHECK(hx_string_append_cstr(&path, "/", NULL) == HX_OS_OK);
	CHECK(hx_string_append_cstr(&path, component, NULL) == HX_OS_OK);
	CHECK(hx_string_append_cstr(&path, "/", NULL) == HX_OS_OK);
	CHECK(hx_string_append_cstr(&path, component, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_create(hx_string_c_str(&path), 1, NULL) ==
		HX_OS_OK);
	CHECK(hx_string_append_cstr(&path, "/长路径.txt", NULL) == HX_OS_OK);
	CHECK(hx_os_file_save_atomic(hx_string_c_str(&path), "long", 4, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_read_text(hx_string_c_str(&path), &text, NULL) ==
		HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "long"));
	hx_string_destroy(&text);
	hx_string_destroy(&path);
}

static void check_replace_and_binary(const char *root)
{
	char target[256], source[256], backup[256];
	hx_string text = HX_STRING_INIT;
	unsigned char *payload = malloc(131072);
	void *loaded;
	size_t bytes, index;
	int mode, exists;
	CHECK(payload);
	for (index = 0; index < 131072; ++index)
		payload[index] = (unsigned char)(index % 251);
	(void)snprintf(target, sizeof(target), "%s/replace-target", root);
	(void)snprintf(source, sizeof(source), "%s/replace-source", root);
	(void)snprintf(backup, sizeof(backup), "%s/replace-backup", root);
	CHECK(hx_os_file_save(source, payload, 131072, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_file_copy(source, target, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_file_load(NULL, target, 131072, &loaded, &bytes, NULL) ==
		HX_OS_OK);
	CHECK(bytes == 131072 && !memcmp(loaded, payload, bytes));
	hx_os_memory_free(loaded);
	free(payload);
	CHECK(hx_os_file_remove(source, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(target, NULL) == HX_OS_OK);
	for (mode = 0; mode < 4; ++mode) {
		if (mode & 1)
			CHECK(hx_os_file_save(target, "old", 3, 0, NULL) ==
				HX_OS_OK);
		CHECK(hx_os_file_save(source, "new", 3, 0, NULL) == HX_OS_OK);
		CHECK(hx_os_file_replace(target, source,
			      (mode & 2) ? backup : NULL, NULL) == HX_OS_OK);
		CHECK(hx_os_file_read_text(target, &text, NULL) == HX_OS_OK);
		CHECK(!strcmp(hx_string_c_str(&text), "new"));
		CHECK(hx_os_file_exists(source, &exists, NULL) == HX_OS_OK &&
			!exists);
		CHECK(hx_os_file_exists(backup, &exists, NULL) == HX_OS_OK);
		CHECK(exists == (mode == 3));
		if (exists)
			CHECK(hx_os_file_remove(backup, NULL) == HX_OS_OK);
		CHECK(hx_os_file_remove(target, NULL) == HX_OS_OK);
	}
#if !defined(_WIN32)
	{
		char temporary[] = "/tmp/hx-platform-cross-XXXXXX";
		struct stat first, second;
		int descriptor = mkstemp(temporary);
		CHECK(descriptor >= 0);
		CHECK(close(descriptor) == 0);
		CHECK(stat(root, &first) == 0 && stat(temporary, &second) == 0);
		if (first.st_dev != second.st_dev) {
			CHECK(hx_os_file_save(target, "old", 3, 0, NULL) ==
				HX_OS_OK);
			CHECK(hx_os_file_replace(target, temporary, backup,
				      NULL) == HX_OS_CROSS_DEVICE);
			CHECK(hx_os_file_read_text(target, &text, NULL) ==
				HX_OS_OK);
			CHECK(!strcmp(hx_string_c_str(&text), "old"));
			CHECK(hx_os_file_remove(target, NULL) == HX_OS_OK);
		}
		else
			puts("SKIP: cross-device check needs a second "
			     "filesystem");
		CHECK(hx_os_file_remove(temporary, NULL) == HX_OS_OK);
	}
#endif
	hx_string_destroy(&text);
}

int main(void)
{
	char root[128], source[256], copy[256], backup[256], sub[256];
	char outside[256], sentinel[384], path[384], buffer[4096];
	hx_string text = HX_STRING_INIT, current = HX_STRING_INIT;
	hx_string absolute = HX_STRING_INIT, outside_absolute = HX_STRING_INIT;
	hx_os_file_save_result result;
	hx_os_error native_error;
	hx_os_status native_status;
	hx_os_walk_options options = HX_OS_WALK_OPTIONS_INIT;
	hx_os_context *context = NULL;
	void *data = NULL;
	size_t bytes, required;
	int exists;
	check_paths();
	if (!(hx_os_capabilities() & HX_OS_CAP_FILE_UTILS)) {
		CHECK(hx_os_file_read_text("unused", &text, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_file_save("unused", "x", 1, 1, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_file_load(NULL, "unused", 10, &data, &bytes,
			      NULL) == HX_OS_NOT_SUPPORTED &&
			!data && !bytes);
		CHECK(hx_os_directory_remove_recursive("unused", NULL) ==
			HX_OS_NOT_SUPPORTED);
		return 0;
	}
	(void)snprintf(root, sizeof(root), "hx-file-utils-%llx",
		(unsigned long long)hx_os_monotonic_ns());
	/* v142's format checker rejects non-ASCII format text under /utf-8. */
	(void)snprintf(source, sizeof(source), "%s/%s", root, "文.txt");
	(void)snprintf(copy, sizeof(copy), "%s/copy.txt", root);
	(void)snprintf(backup, sizeof(backup), "%s/backup", root);
	(void)snprintf(sub, sizeof(sub), "%s/sub", root);
	(void)snprintf(outside, sizeof(outside), "%s-outside", root);
	(void)snprintf(sentinel, sizeof(sentinel), "%s/keep", outside);
	CHECK(hx_os_directory_create(root, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_create(sub, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_create(outside, 0, NULL) == HX_OS_OK);
	check_replace_and_binary(root);
	CHECK(hx_os_file_save(sentinel, "keep", 4, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_file_save(source, "\xef\xbb\xbftext", 7, 0, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_save(source, "bad", 3, 0, NULL) == HX_OS_EXISTS);
	CHECK(hx_os_file_read_text(source, &text, NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "text"));
	CHECK(hx_os_file_load_utf8(NULL, source, 7, &text, 0, NULL) ==
		HX_OS_OK);
	CHECK(text.size == 7);
	CHECK(hx_string_assign_cstr(&text, "preserve", NULL) == HX_OS_OK);
	CHECK(hx_os_file_load_utf8(NULL, source, 6, &text, 1, NULL) ==
		HX_OS_LIMIT);
	CHECK(!strcmp(hx_string_c_str(&text), "preserve"));
	CHECK(hx_os_file_copy(source, source, 1, NULL) == HX_OS_INVALID);
	CHECK(hx_os_file_copy(source, copy, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_file_copy(source, copy, 0, NULL) == HX_OS_EXISTS);
	CHECK(hx_os_file_copy(source, copy, 1, NULL) == HX_OS_OK);
	CHECK(hx_os_file_backup(source, backup, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_file_replace(source, copy, backup, NULL) == HX_OS_EXISTS);
	CHECK(hx_os_file_remove(backup, NULL) == HX_OS_OK);
	native_status = hx_os_file_replace(source, copy, backup, &native_error);
	if (native_status != HX_OS_OK)
		fprintf(stderr, "replace: %s, domain %u, code %lld\n",
			hx_os_status_name(native_status), native_error.domain,
			(long long)native_error.native_code);
	CHECK(native_status == HX_OS_OK);
	CHECK(hx_os_file_exists(copy, &exists, NULL) == HX_OS_OK && !exists);
	CHECK(hx_os_file_save_atomic_ex(source, "\xff", 1, &result, NULL) ==
		HX_OS_OK);
	CHECK(result.published);
	CHECK(hx_os_file_read_text(source, &text, NULL) ==
		HX_OS_INVALID_ENCODING);
	CHECK(!strcmp(hx_string_c_str(&text), "preserve"));
	CHECK(hx_os_file_write_text(source, &text, NULL) == HX_OS_OK);
	CHECK(hx_os_file_save(copy, NULL, 0, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_file_load(NULL, copy, 0, &data, &bytes, NULL) == HX_OS_OK);
	CHECK(bytes == 0 && data);
	hx_os_memory_free(data);
	CHECK(hx_os_file_save_atomic(copy, "new", 3, NULL) == HX_OS_OK);
	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	CHECK(hx_os_file_load(context, copy, 3, &data, &bytes, NULL) ==
		HX_OS_OK);
	CHECK(bytes == 3 && !memcmp(data, "new", 3));
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);
	hx_os_memory_free(data);
	CHECK(hx_os_file_load(context, copy, 2, &data, &bytes, NULL) ==
		HX_OS_LIMIT);
	CHECK(!data && !bytes && hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	(void)snprintf(path, sizeof(path), "%s/deep.txt", sub);
	CHECK(hx_os_file_save(path, "deep", 4, 0, NULL) == HX_OS_OK);
	check_glob(root);
	CHECK(hx_os_path_current_string(&current, NULL) == HX_OS_OK);
	CHECK(hx_os_path_current(NULL, 0, &required, NULL) ==
		HX_OS_OUTPUT_FULL);
	CHECK(hx_os_path_current(buffer, sizeof(buffer), &required, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_path_absolute_string(root, &absolute, NULL) == HX_OS_OK);
	CHECK(hx_os_path_absolute(root, buffer, sizeof(buffer), &required,
		      NULL) == HX_OS_OK);
	CHECK(!strcmp(buffer, hx_string_c_str(&absolute)));
	CHECK(hx_os_path_change_current(hx_string_c_str(&absolute), NULL) ==
		HX_OS_OK);
	CHECK(hx_os_path_change_current(hx_string_c_str(&current), NULL) ==
		HX_OS_OK);
	native_status = hx_os_path_absolute_string(
		outside, &outside_absolute, &native_error);
	if (native_status != HX_OS_OK)
		fprintf(stderr, "absolute: %s, domain %u, code %lld\n",
			hx_os_status_name(native_status), native_error.domain,
			(long long)native_error.native_code);
	CHECK(native_status == HX_OS_OK);
	check_links(root, hx_string_c_str(&outside_absolute));
	CHECK(hx_os_file_read_text(sentinel, &text, NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "keep"));
	check_long_path(root);
	options.max_depth = 1;
	CHECK(hx_os_directory_remove_recursive_with_options(
		      root, &options, NULL) == HX_OS_LIMIT);
	CHECK(hx_os_directory_remove_recursive(root, NULL) == HX_OS_OK);
	CHECK(hx_os_file_exists(root, &exists, NULL) == HX_OS_OK && !exists);
	CHECK(hx_os_directory_remove_recursive(outside, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_remove_recursive(".", NULL) == HX_OS_INVALID);
	CHECK(hx_os_directory_remove_recursive("/", NULL) == HX_OS_INVALID);
	hx_string_destroy(&text);
	hx_string_destroy(&current);
	hx_string_destroy(&absolute);
	hx_string_destroy(&outside_absolute);
	puts("file convenience contracts passed");
	return 0;
}
