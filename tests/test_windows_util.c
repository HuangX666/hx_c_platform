#include "utility_test.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static void test_version(void)
{
	wchar_t path[32768];
	DWORD units = GetModuleFileNameW(NULL, path, 32768);
	hx_string file = HX_STRING_INIT, text = HX_STRING_INIT;

	struct version_probe {
		hx_os_windows_version_info info;
		unsigned char tail[16];
	} probe, saved;

	hx_os_error error;
	test_memory memory = {0};
	hx_os_context *context = test_context(&memory);
	size_t step;
	hx_os_status status;

	CHECK(units > 0 && units < 32768);
	CHECK(hx_string_assign_wide(&file, path, units, NULL) == HX_OS_OK);
	memset(&probe, 0x5a, sizeof(probe));
	saved = probe;
	CHECK(hx_os_windows_version_query(hx_string_c_str(&file), &probe.info,
		      sizeof(probe.info) - 1, NULL) == HX_OS_OUTPUT_FULL);
	CHECK(!memcmp(&probe, &saved, sizeof(probe)));
	CHECK(hx_os_windows_version_query(hx_string_c_str(&file), &probe.info,
		      sizeof(probe), NULL) == HX_OS_OK);
	CHECK(probe.info.size == sizeof(probe.info) && probe.info.version == 1);
	CHECK(probe.info.file[0] == 1 && probe.info.file[1] == 2 &&
		probe.info.file[2] == 345 && probe.info.file[3] == 678);
	CHECK(probe.info.product[0] == 9 && probe.info.product[1] == 8 &&
		probe.info.product[2] == 7 && probe.info.product[3] == 6);
	CHECK(probe.info.flags == 3 && probe.info.type == 1);
	CHECK(!memcmp(probe.tail, saved.tail, sizeof(probe.tail)));
	saved = probe;
	CHECK(hx_os_windows_version_query("hx-missing-version-file.dll",
		      &probe.info, sizeof(probe.info),
		      &error) == HX_OS_NOT_FOUND);
	CHECK(error.domain == HX_OS_ERROR_WIN32 && error.native_code);
	CHECK(!memcmp(&probe, &saved, sizeof(probe)));
	CHECK(hx_os_windows_version_query("\xff", &probe.info,
		      sizeof(probe.info), NULL) == HX_OS_INVALID_ENCODING);

	CHECK(hx_os_windows_version_string(hx_string_c_str(&file),
		      "FileDescription", &text, NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "Window helper fixture"));
	CHECK(hx_os_windows_version_string_ex(hx_string_c_str(&file),
		      "FileDescription", 0x0804, 1200, &text,
		      NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "窗口工具测试"));
	CHECK(hx_os_windows_version_string(hx_string_c_str(&file),
		      "MissingField", &text, NULL) == HX_OS_NOT_FOUND);
	CHECK(!strcmp(hx_string_c_str(&text), "窗口工具测试"));
	CHECK(hx_os_windows_version_string(hx_string_c_str(&file),
		      "../FileDescription", &text, NULL) == HX_OS_INVALID);
	CHECK(hx_os_windows_version_string_ex(hx_string_c_str(&file),
		      "FileDescription", 0x1234, 1, &text,
		      NULL) == HX_OS_NOT_FOUND);
	hx_string_destroy(&text);

	{
		wchar_t directory[MAX_PATH], copied[MAX_PATH];
		DWORD length = GetTempPathW(MAX_PATH, directory);

		CHECK(length && length < MAX_PATH);
		CHECK(GetTempFileNameW(directory, L"测", 0, copied));
		CHECK(CopyFileW(path, copied, FALSE));
		CHECK(hx_string_assign_wide(
			      &text, copied, wcslen(copied), NULL) == HX_OS_OK);
		CHECK(hx_os_windows_version_query(hx_string_c_str(&text),
			      &probe.info, sizeof(probe.info),
			      NULL) == HX_OS_OK);
		CHECK(probe.info.file[2] == 345);
		CHECK(DeleteFileW(copied));
		hx_string_destroy(&text);
	}

	for (step = 1; step < 32; ++step) {
		hx_string_init(&text, context);
		CHECK(hx_string_assign_cstr(&text, "old", NULL) == HX_OS_OK);
		memory.fail = memory.calls + step;
		status = hx_os_windows_version_string(
			hx_string_c_str(&file), "FileDescription", &text, NULL);
		memory.fail = 0;
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		if (status != HX_OS_OK)
			CHECK(!strcmp(hx_string_c_str(&text), "old"));
		hx_string_destroy(&text);
		CHECK(hx_os_context_allocations(context) == 0);
		if (status == HX_OS_OK)
			break;
	}
	CHECK(step < 32);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(memory.live == 0);
	hx_string_destroy(&file);
}

static void *HX_OS_CALL read_foreign_thread_window(void *user)
{
	hx_string text = HX_STRING_INIT;

	CHECK(hx_os_windows_window_title((uintptr_t)user, &text, NULL) ==
		HX_OS_WOULD_BLOCK);
	CHECK(!text.size);
	hx_string_destroy(&text);

	return NULL;
}

static void test_title_growth(HWND window)
{
	static const size_t lengths[] = {255, 1024, 32767, 32768};
	wchar_t *wide = malloc(32769 * sizeof(*wide));
	hx_string text = HX_STRING_INIT;
	test_memory memory = {0};
	hx_os_context *context = test_context(&memory);
	size_t i, j, step;
	hx_os_status status;

	CHECK(wide);
	for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
		for (j = 0; j < lengths[i]; ++j)
			wide[j] = L'界';
		wide[j] = 0;
		CHECK(SetWindowTextW(window, wide));
		CHECK(hx_string_assign_cstr(&text, "before", NULL) == HX_OS_OK);
		status = hx_os_windows_window_title(
			(uintptr_t)window, &text, NULL);
		if (j <= 32767)
			CHECK(status == HX_OS_OK && text.size == j * 3);
		else
			CHECK(status == HX_OS_LIMIT &&
				!strcmp(hx_string_c_str(&text), "before"));
	}
	free(wide);
	hx_string_destroy(&text);
	CHECK(SetWindowTextW(window, L"Allocation check"));
	for (step = 1; step < 16; ++step) {
		hx_string_init(&text, context);
		CHECK(hx_string_assign_cstr(&text, "before", NULL) == HX_OS_OK);
		memory.fail = memory.calls + step;
		status = hx_os_windows_window_title(
			(uintptr_t)window, &text, NULL);
		memory.fail = 0;
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		if (status == HX_OS_NO_MEMORY)
			CHECK(!strcmp(hx_string_c_str(&text), "before"));
		hx_string_destroy(&text);
		CHECK(hx_os_context_allocations(context) == 0);
		if (status == HX_OS_OK)
			break;
	}
	CHECK(step < 16);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(!memory.live);
}

static void test_window(void)
{
	HWND window = CreateWindowExW(0, L"STATIC", L"窗口 \xd83d\xde80",
		WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, NULL, NULL,
		GetModuleHandleW(NULL), NULL);
	hx_os_windows_window_info info, old;
	hx_os_windows_monitor_info monitor;
	hx_os_windows_gpu_info gpu;
	hx_string text = HX_STRING_INIT;
	hx_os_thread *thread = NULL;
	hx_os_status status;

	CHECK(window);
	memset(&info, 0xa5, sizeof(info));
	old = info;
	CHECK(hx_os_windows_window_query((uintptr_t)window, &info,
		      sizeof(info) - 1, NULL) == HX_OS_OUTPUT_FULL);
	CHECK(!memcmp(&info, &old, sizeof(info)));
	CHECK(hx_os_windows_window_query((uintptr_t)window, &info, sizeof(info),
		      NULL) == HX_OS_OK);
	CHECK(info.process_id == GetCurrentProcessId());
	CHECK(info.thread_id == GetCurrentThreadId());
	CHECK(!(info.flags & HX_OS_WINDOWS_WINDOW_VISIBLE));
	CHECK(hx_os_windows_window_title((uintptr_t)window, &text, NULL) ==
		HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "窗口 🚀"));
	CHECK(hx_os_windows_window_class((uintptr_t)window, &text, NULL) ==
		HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "Static"));
	CHECK(hx_os_windows_window_process_path(
		      (uintptr_t)window, &text, NULL) == HX_OS_OK);
	CHECK(text.size > 4 && strstr(hx_string_c_str(&text), ".exe"));
	CHECK(hx_os_thread_create(NULL, read_foreign_thread_window, window,
		      &thread, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(3000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	CHECK(SetWindowTextW(window, L""));
	CHECK(hx_os_windows_window_title((uintptr_t)window, &text, NULL) ==
			HX_OS_OK &&
		!text.size);
	test_title_growth(window);

	if (info.monitor) {
		CHECK(hx_os_windows_monitor_query(info.monitor, &monitor,
			      sizeof(monitor), NULL) == HX_OS_OK);
		CHECK(monitor.bounds.right > monitor.bounds.left);
		CHECK(monitor.device_name[0]);
		status = hx_os_windows_monitor_gpu_query(
			info.monitor, &gpu, sizeof(gpu), NULL);
		CHECK(status == HX_OS_OK || status == HX_OS_NOT_FOUND ||
			status == HX_OS_NOT_SUPPORTED || status == HX_OS_BUSY);
		if (status == HX_OS_OK)
			printf("Display GPU: %s\n", gpu.description);
		else
			printf("Display GPU unavailable: %s\n",
				hx_os_status_name(status));
	}

	CHECK(DestroyWindow(window));
	CHECK(hx_string_assign_cstr(&text, "keep", NULL) == HX_OS_OK);
	CHECK(hx_os_windows_window_title((uintptr_t)window, &text, NULL) !=
		HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "keep"));
	hx_string_destroy(&text);
}

static void test_gpus(void)
{
	hx_os_windows_gpu_info gpu, old;
	hx_os_status status;
	uint32_t index;

	memset(&gpu, 0xa5, sizeof(gpu));
	old = gpu;
	CHECK(hx_os_windows_gpu_query(0, &gpu, sizeof(gpu) - 1, NULL) ==
		HX_OS_OUTPUT_FULL);
	CHECK(!memcmp(&gpu, &old, sizeof(gpu)));
	CHECK(hx_os_windows_gpu_query(256, &gpu, sizeof(gpu), NULL) ==
		HX_OS_LIMIT);
	CHECK(!memcmp(&gpu, &old, sizeof(gpu)));

	for (index = 0; index < 256; ++index) {
		status =
			hx_os_windows_gpu_query(index, &gpu, sizeof(gpu), NULL);
		if (status == HX_OS_NOT_FOUND ||
			status == HX_OS_NOT_SUPPORTED || status == HX_OS_BUSY)
			break;
		CHECK(status == HX_OS_OK);
		CHECK(gpu.size == sizeof(gpu) && gpu.version == 1);
		CHECK(gpu.description[0]);
		printf("GPU %u: %s, LUID %llu\n", index, gpu.description,
			(unsigned long long)gpu.luid);
	}
	CHECK(index < 256);
}
#endif

int main(void)
{
	hx_os_windows_version_info version = {0};
	hx_os_windows_window_info window = {0};
	hx_os_windows_monitor_info monitor = {0};
	hx_os_windows_gpu_info gpu = {0};
	hx_string text = HX_STRING_INIT;

	if (!hx_os_windows_version_available()) {
		CHECK(hx_os_windows_version_query("file", &version,
			      sizeof(version), NULL) == HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_windows_version_string("file", "ProductName", &text,
			      NULL) == HX_OS_NOT_SUPPORTED);
	}
	if (!hx_os_windows_desktop_available()) {
		CHECK(hx_os_windows_window_query(1, &window, sizeof(window),
			      NULL) == HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_windows_window_title(1, &text, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_windows_window_class(1, &text, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_windows_window_process_path(1, &text, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_windows_monitor_query(1, &monitor, sizeof(monitor),
			      NULL) == HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_windows_gpu_query(0, &gpu, sizeof(gpu), NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_windows_monitor_gpu_query(1, &gpu, sizeof(gpu),
			      NULL) == HX_OS_NOT_SUPPORTED);
	}

#if defined(_WIN32)
	if (hx_os_windows_version_available())
		test_version();
	if (hx_os_windows_desktop_available()) {
		test_window();
		test_gpus();
	}
#endif
	hx_string_destroy(&text);

	return 0;
}
