#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

static void test_bom(void)
{
	unsigned char data[5];
	size_t bytes, prefix;
	hx_os_encoding encoding;
	int i;

	if (!(hx_os_capabilities() & HX_OS_CAP_BOM)) {
		CHECK(hx_os_text_bom_bytes(HX_OS_UTF8, data, &bytes) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_text_bom_detect("x", 1, &encoding, &prefix) ==
			HX_OS_NOT_SUPPORTED);

		return;
	}

	for (i = HX_OS_UTF8; i <= HX_OS_UTF32BE; ++i) {
		memset(data, 0xa5, sizeof(data));
		CHECK(hx_os_text_bom_bytes((hx_os_encoding)i, data, &bytes) ==
			HX_OS_OK);
		CHECK(bytes >= 2 && bytes <= 4 && data[4] == 0xa5);
		CHECK(hx_os_text_bom_detect(data, bytes, &encoding, &prefix) ==
			HX_OS_OK);
		CHECK(encoding == (hx_os_encoding)i && prefix == bytes);
	}

	CHECK(hx_os_text_bom_detect("\xff\xfe\0\0", 4, &encoding, &prefix) ==
			HX_OS_OK &&
		encoding == HX_OS_UTF32LE && prefix == 4);

	encoding = HX_OS_ASCII;
	CHECK(hx_os_text_bom_detect("\xef\xbb", 2, &encoding, &prefix) ==
			HX_OS_NOT_FOUND &&
		!prefix && encoding == HX_OS_ASCII);
	CHECK(hx_os_text_bom_detect(NULL, 0, &encoding, &prefix) ==
		HX_OS_NOT_FOUND);
	CHECK(hx_os_text_bom_detect(NULL, 1, &encoding, &prefix) ==
		HX_OS_INVALID);

	memset(data, 0xa5, sizeof(data));
	bytes = 99;
	CHECK(hx_os_text_bom_bytes(HX_OS_GBK, data, &bytes) ==
		HX_OS_NOT_SUPPORTED);
	CHECK(bytes == 99 && data[0] == 0xa5 && data[3] == 0xa5);
}

static void test_metadata(void)
{
	hx_os_file_info_extended info = HX_OS_FILE_INFO_EXTENDED_INIT;
	hx_os_file_info_extended previous;

	struct {
		hx_os_file_info_extended value;
		unsigned char guard[8];
	} capacity;

	hx_os_file *file = NULL;
	hx_os_directory *directory = NULL;
	char root[96], path[128], subdir[128], name[256];
	size_t required, count;
	unsigned kind;
	int seen_file = 0, seen_directory = 0;
	hx_os_status status;

	memset(&capacity, 0xa5, sizeof(capacity));
	CHECK(hx_os_file_info_extended_init(&capacity.value,
		      sizeof(capacity.value) - 1) == HX_OS_INVALID);
	CHECK(((unsigned char *)&capacity.value)[0] == 0xa5);
	CHECK(hx_os_file_info_extended_init(
		      &capacity.value, sizeof(capacity)) == HX_OS_OK);
	CHECK(capacity.guard[0] == 0xa5 && capacity.guard[7] == 0xa5);
	if (!(hx_os_capabilities() & HX_OS_CAP_FILE_INFO)) {
		CHECK(hx_os_path_stat_extended(".", 0, &info, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_directory_next_info(NULL, name, sizeof(name),
			      &required, &kind, NULL) == HX_OS_NOT_SUPPORTED);

		return;
	}

	(void)snprintf(root, sizeof(root),
#if defined(_WIN32)
		"hx-metadata-%llx",
#else
		"/tmp/hx-metadata-%llx",
#endif
		(unsigned long long)hx_os_monotonic_ns());
	(void)snprintf(path, sizeof(path), "%s/中文.txt", root);
	(void)snprintf(subdir, sizeof(subdir), "%s/sub", root);
	CHECK(hx_os_directory_create(root, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_create(subdir, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_file_open(NULL, path,
		      HX_OS_FILE_CREATE | HX_OS_FILE_EXCLUSIVE |
			      HX_OS_FILE_WRITE,
		      &file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_write_all(file, "hello", 5, &count, NULL) == HX_OS_OK);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	CHECK(hx_os_path_stat_extended(path, 0, &info, NULL) == HX_OS_OK);
	CHECK(info.basic.kind == HX_OS_FILE_REGULAR && info.basic.size == 5);
	CHECK(info.basic.modified_nanoseconds < 1000000000);
	CHECK(info.accessed_nanoseconds < 1000000000);
	CHECK(info.changed_nanoseconds < 1000000000);
#if defined(_WIN32)
	CHECK(info.known_permissions == 0);

	{
		wchar_t wide[128];

		CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path,
			      -1, wide, 128) > 0);
		CHECK(SetFileAttributesW(wide, FILE_ATTRIBUTE_READONLY));
		CHECK(hx_os_path_stat_extended(path, 0, &info, NULL) ==
			HX_OS_OK);
		CHECK(info.read_only == 1 && !info.known_permissions);
		CHECK(SetFileAttributesW(wide, FILE_ATTRIBUTE_NORMAL));
	}
#else
	CHECK(info.known_permissions ==
		(HX_OS_PERMISSION_OWNER_READ | HX_OS_PERMISSION_OWNER_WRITE));
	CHECK(chmod(path, 0400) == 0);
	CHECK(hx_os_path_stat_extended(path, 0, &info, NULL) == HX_OS_OK);
	CHECK(info.read_only &&
		info.owner_permissions == HX_OS_PERMISSION_OWNER_READ);
	CHECK(chmod(path, 0600) == 0);

	{
		char link[128];

		(void)snprintf(link, sizeof(link), "%s/link", root);
		CHECK(symlink("中文.txt", link) == 0);
		CHECK(hx_os_path_stat_extended(link, 0, &info, NULL) ==
			HX_OS_OK);
		CHECK(info.basic.kind == HX_OS_FILE_LINK);
		CHECK(hx_os_path_stat_extended(link, 1, &info, NULL) ==
			HX_OS_OK);
		CHECK(info.basic.kind == HX_OS_FILE_REGULAR);
		CHECK(hx_os_file_remove(link, NULL) == HX_OS_OK);
	}
#endif
	memcpy(&previous, &info, sizeof(info));
	CHECK(hx_os_path_stat_extended(path, 2, &info, NULL) == HX_OS_INVALID);
	CHECK(!memcmp(&info, &previous, sizeof(info)));
	CHECK(hx_os_path_stat_extended("\xff", 0, &info, NULL) ==
		HX_OS_INVALID_ENCODING);
	CHECK(!memcmp(&info, &previous, sizeof(info)));
	CHECK(hx_os_directory_open(NULL, root, &directory, NULL) == HX_OS_OK);

	for (;;) {
		kind = 99;

		status = hx_os_directory_next_info(
			directory, NULL, 0, &required, &kind, NULL);
		if (status == HX_OS_EOF)
			break;

		CHECK(status == HX_OS_OUTPUT_FULL && kind == 99);
		CHECK(required <= sizeof(name));
		CHECK(hx_os_directory_next_info(directory, name, sizeof(name),
			      &required, &kind, NULL) == HX_OS_OK);
		if (!strcmp(name, "中文.txt")) {
			CHECK(kind == HX_OS_FILE_REGULAR);

			++seen_file;
		}
		else if (!strcmp(name, "sub")) {
			CHECK(kind == HX_OS_FILE_DIRECTORY);

			++seen_directory;
		}
		else
			CHECK(0);
	}

	CHECK(seen_file == 1 && seen_directory == 1);

	hx_os_directory_close(directory);
	CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
	CHECK(hx_os_path_stat_extended(path, 0, &info, NULL) ==
		HX_OS_NOT_FOUND);
	CHECK(!memcmp(&info, &previous, sizeof(info)));
	CHECK(hx_os_directory_remove(subdir, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_remove(root, NULL) == HX_OS_OK);
}

int main(void)
{
	test_bom();
	test_metadata();
	puts("file information and BOM contracts passed");

	return 0;
}
