#if !defined(_WIN32)
#define _GNU_SOURCE
#endif
#include <hx_os.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

static int probe_lock(const char *path, int blocked, int shared)
{
	hx_os_file *file = NULL;
	hx_os_error error;
	hx_os_status status;
	uint64_t before;
	CHECK(hx_os_file_open(NULL, path, HX_OS_FILE_READ | HX_OS_FILE_WRITE,
		      &file, NULL) == HX_OS_OK);
	before = hx_os_monotonic_ns();
	status = hx_os_file_lock_at(
		file, 0, 16, shared, hx_os_deadline_after_ms(5), &error);
	if (blocked) {
		CHECK(status == HX_OS_TIMEOUT &&
			error.domain != HX_OS_ERROR_NONE);
		CHECK(hx_os_monotonic_ns() - before >= UINT64_C(5000000));
		CHECK(hx_os_file_lock_at(file, 0, 16, shared,
			      HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT);
	}
	else {
		CHECK(status == HX_OS_OK);
		CHECK(hx_os_file_unlock_at(file, 0, 16, NULL) == HX_OS_OK);
	}
	/* Timeout kept this handle usable, including an unrelated lock range.
	 */
	CHECK(hx_os_file_lock_at(file, 128, 1, 0, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_unlock_at(file, 128, 1, NULL) == HX_OS_OK);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	return 0;
}

static void child_probe(const char *path, int blocked, int shared)
{
#if defined(_WIN32)
	wchar_t executable[1024], filename[512], command[2048];
	STARTUPINFOW startup;
	PROCESS_INFORMATION process;
	DWORD count, code;
	memset(&startup, 0, sizeof(startup));
	startup.cb = sizeof(startup);
	memset(&process, 0, sizeof(process));
	count = GetModuleFileNameW(NULL, executable, 1024);
	CHECK(count && count < 1024);
	CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
		      filename, 512) > 0);
	CHECK(swprintf(command, 2048, L"\"%ls\" probe \"%ls\" %d %d",
		      executable, filename, blocked, shared) > 0);
	CHECK(CreateProcessW(executable, command, NULL, NULL, FALSE,
		CREATE_NO_WINDOW, NULL, NULL, &startup, &process));
	CHECK(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0);
	CHECK(GetExitCodeProcess(process.hProcess, &code) && code == 0);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
#else
	pid_t child = fork(), waited;
	int code;
	CHECK(child >= 0);
	if (!child)
		_exit(probe_lock(path, blocked, shared));
	do {
		waited = waitpid(child, &code, 0);
	} while (waited < 0 && errno == EINTR);
	CHECK(waited == child && WIFEXITED(code) && WEXITSTATUS(code) == 0);
#endif
}

int main(int argc, char **argv)
{
	hx_os_file *file = NULL;
	hx_os_file_times times = HX_OS_FILE_TIMES_INIT;
	hx_os_file_info info;
	hx_os_file_info_extended extended = HX_OS_FILE_INFO_EXTENDED_INIT;
	char path[256];
	uint64_t cursor;
	size_t completed;
	struct {
		hx_os_file_times times;
		unsigned char canary[8];
	} storage;
	if (argc == 5 && !strcmp(argv[1], "probe")) {
#if defined(_WIN32)
		wchar_t **wide;
		char utf8[1536];
		int wide_count;
		/* The native child command line is Unicode; narrow main argv
		 * uses the CRT's active code page, not the library's UTF-8 ABI.
		 */
		wide = CommandLineToArgvW(GetCommandLineW(), &wide_count);
		CHECK(wide && wide_count == 5);
		CHECK(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			      wide[2], -1, utf8, sizeof(utf8), NULL, NULL) > 0);
		LocalFree(wide);
		return probe_lock(utf8, atoi(argv[3]), atoi(argv[4]));
#else
		return probe_lock(argv[2], atoi(argv[3]), atoi(argv[4]));
#endif
	}
	CHECK(argc == 1);
	memset(&storage, 0xa5, sizeof(storage));
	CHECK(hx_os_file_times_init(&storage.times, sizeof(times) - 1) ==
			HX_OS_INVALID &&
		storage.canary[0] == 0xa5);
	CHECK(hx_os_file_times_init(&storage.times, sizeof(storage)) ==
		HX_OS_OK);
	CHECK(storage.times.size == sizeof(times) && !storage.times.fields &&
		storage.canary[0] == 0xa5 && storage.canary[7] == 0xa5);
	if (!(hx_os_capabilities() & HX_OS_CAP_FILE_EXTRA)) {
		CHECK(hx_os_file_lock_at(NULL, 0, 1, 0, HX_OS_DEADLINE_NOW,
			      NULL) == HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_file_set_times("unused", &times, 1, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_path_set_permissions("unused", 3, 1, NULL) ==
			HX_OS_NOT_SUPPORTED);
		return 0;
	}
#if defined(_WIN32)
	(void)snprintf(path, sizeof(path), "hx-extra-%llx-中文.bin",
		(unsigned long long)hx_os_monotonic_ns());
#else
	(void)snprintf(path, sizeof(path), "/tmp/hx-extra-%llx-中文.bin",
		(unsigned long long)hx_os_monotonic_ns());
#endif
	CHECK(hx_os_file_open(NULL, path,
		      HX_OS_FILE_READ | HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
			      HX_OS_FILE_EXCLUSIVE,
		      &file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_write_all(file, "data", 4, &completed, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_lock_at(file, 0, 0, 0, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_INVALID);
	CHECK(hx_os_file_lock_at(file, UINT64_MAX, 1, 0, HX_OS_DEADLINE_NOW,
		      NULL) == HX_OS_RANGE);
	CHECK(hx_os_file_lock_at(file, 0, 16, 0, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_OK);
	child_probe(path, 1, 0);
	child_probe(path, 1, 1);
	CHECK(hx_os_file_seek(file, 0, HX_OS_SEEK_CURRENT, &cursor, NULL) ==
			HX_OS_OK &&
		cursor == 4);
	CHECK(hx_os_file_unlock_at(file, 0, 16, NULL) == HX_OS_OK);
	child_probe(path, 0, 0);
	CHECK(hx_os_file_lock_at(file, 0, 16, 1, HX_OS_DEADLINE_INFINITE,
		      NULL) == HX_OS_OK);
	child_probe(path, 0, 1);
	child_probe(path, 1, 0);
	CHECK(hx_os_file_unlock_at(file, 0, 16, NULL) == HX_OS_OK);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	{
		char missing[280];
		hx_os_error error;
		hx_os_file_times value = HX_OS_FILE_TIMES_INIT;
		(void)snprintf(missing, sizeof(missing), "%s-missing", path);
		value.fields = HX_OS_TIME_MODIFIED;
		value.modified_seconds = 1600000000;
		CHECK(hx_os_file_set_times(missing, &value, 1, NULL) ==
			HX_OS_NOT_FOUND);
		CHECK(hx_os_file_set_times(missing, &value, 1, &error) ==
				HX_OS_NOT_FOUND &&
			error.domain != HX_OS_ERROR_NONE);
		CHECK(hx_os_path_set_permissions(missing, 3, 1, NULL) ==
			HX_OS_NOT_FOUND);
	}
	times.fields = HX_OS_TIME_ACCESSED | HX_OS_TIME_MODIFIED;
	times.accessed_seconds = 1600000000;
	times.modified_seconds = 1600000100;
	times.accessed_nanoseconds = 123456700;
	times.modified_nanoseconds = 456789100;
	CHECK(hx_os_file_set_times(path, &times, 1, NULL) == HX_OS_OK);
	CHECK(hx_os_path_stat(path, 1, &info, NULL) == HX_OS_OK);
	CHECK(info.modified_seconds == times.modified_seconds &&
		info.modified_nanoseconds == times.modified_nanoseconds);
	if (hx_os_capabilities() & HX_OS_CAP_FILE_INFO) {
		CHECK(hx_os_path_stat_extended(path, 1, &extended, NULL) ==
			HX_OS_OK);
		CHECK(extended.accessed_seconds == times.accessed_seconds &&
			extended.accessed_nanoseconds ==
				times.accessed_nanoseconds);
	}
	times.fields = HX_OS_TIME_MODIFIED;
	times.modified_seconds += 10;
	CHECK(hx_os_file_set_times(path, &times, 0, NULL) == HX_OS_OK);
	if (hx_os_capabilities() & HX_OS_CAP_FILE_INFO) {
		CHECK(hx_os_path_stat_extended(path, 1, &extended, NULL) ==
			HX_OS_OK);
		CHECK(extended.accessed_seconds == times.accessed_seconds &&
			extended.accessed_nanoseconds ==
				times.accessed_nanoseconds);
	}
	times.modified_nanoseconds = 1000000000;
	CHECK(hx_os_file_set_times(path, &times, 1, NULL) == HX_OS_RANGE);
	times.fields = 0;
	CHECK(hx_os_file_set_times("missing-no-op", &times, 1, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_path_set_permissions(
		      path, HX_OS_PERMISSION_OWNER_READ, 1, NULL) == HX_OS_OK);
#if defined(_WIN32)
	if (hx_os_capabilities() & HX_OS_CAP_FILE_INFO) {
		CHECK(hx_os_path_stat_extended(path, 1, &extended, NULL) ==
			HX_OS_OK);
		CHECK(extended.read_only);
	}
	CHECK(hx_os_path_set_permissions(path, 0, 1, NULL) ==
		HX_OS_NOT_SUPPORTED);
	CHECK(hx_os_path_set_permissions(path, 5, 1, NULL) ==
		HX_OS_NOT_SUPPORTED);
#else
	{
		struct stat native, link_info;
		char link[280];
		CHECK(stat(path, &native) == 0 &&
			(native.st_mode & 0700) == 0400);
		CHECK(chmod(path, 0640) == 0);
		CHECK(hx_os_path_set_permissions(path, 5, 1, NULL) == HX_OS_OK);
		CHECK(stat(path, &native) == 0 &&
			(native.st_mode & 0777) == 0540);
		CHECK(hx_os_path_set_permissions(path, 0, 1, NULL) == HX_OS_OK);
		CHECK(hx_os_path_set_permissions(path, 3, 1, NULL) == HX_OS_OK);
		(void)snprintf(link, sizeof(link), "%s-link", path);
		CHECK(symlink(path, link) == 0);
		times.fields = HX_OS_TIME_MODIFIED;
		times.modified_seconds = 1500000000;
		times.modified_nanoseconds = 0;
		CHECK(hx_os_file_set_times(link, &times, 0, NULL) == HX_OS_OK);
		CHECK(lstat(link, &link_info) == 0 &&
			link_info.st_mtime == times.modified_seconds);
		CHECK(stat(path, &native) == 0 &&
			native.st_mtime == 1600000110);
		{
			hx_os_status status = hx_os_path_set_permissions(
				link, HX_OS_PERMISSION_OWNER_READ, 0, NULL);
			CHECK(status == HX_OS_OK ||
				status == HX_OS_NOT_SUPPORTED);
			CHECK(stat(path, &native) == 0 &&
				(native.st_mode & 0777) == 0640);
		}
		CHECK(hx_os_file_remove(link, NULL) == HX_OS_OK);
	}
#endif
	CHECK(hx_os_path_set_permissions(path,
		      HX_OS_PERMISSION_OWNER_READ |
			      HX_OS_PERMISSION_OWNER_WRITE,
		      1, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
	puts("range lock, file times and permissions contracts passed");
	return 0;
}
