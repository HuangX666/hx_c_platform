#include <hx/SystemInfo.hpp>
#include <hx/Library.hpp>
#include <hx/ThreadPool.hpp>
#include <cstdio>
#include <cstdlib>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			std::fprintf(                                          \
				stderr, "line %d: %s\n", __LINE__, #value);    \
			std::exit(1);                                          \
		}                                                              \
	} while (0)

static void TestSystemInfo()
{
	if (!(hx_os_capabilities() & HX_OS_CAP_SYSINFO)) {
		try {
			(void)hx::SystemInfo::totalMemory();
			CHECK(false);
		} catch (const hx::StatusError &error) {
			CHECK(error.status() == HX_OS_NOT_SUPPORTED);
		}
		return;
	}
	CHECK(hx::SystemInfo::totalMemory() > 0);
	CHECK(hx::SystemInfo::logicalCores() > 0);
	CHECK(hx::SystemInfo::pageSize() > 0);
	CHECK(hx::SystemInfo::processMemory().resident_bytes > 0);
	CHECK(hx::SystemInfo::diskSpace(".").total_bytes > 0);
	CHECK(!hx::SystemInfo::version().empty());
	hx::CpuUsage first;
	hx::CpuUsage second(std::move(first));
	CHECK(hx_os_sleep_until(hx_os_deadline_after_ms(10), NULL) == HX_OS_OK);
	CHECK(second.query() >= 0);
}

int main(int argc, char **argv)
{
	CHECK(argc == 2);
	TestSystemInfo();
	if (hx_os_capabilities() & HX_OS_CAP_LIBRARY) {
		hx::Library source(argv[1]);
		hx::Library library(std::move(source));
		using Abi = uint32_t(HX_OS_CALL *)(void);
		CHECK(library.symbol<Abi>("hx_plugin_abi_version")() ==
			hx_os_abi_version());
		try {
			(void)library.symbol<Abi>("hx_missing_symbol");
			CHECK(false);
		} catch (const hx::LibraryError &error) {
			CHECK(error.status() == HX_OS_NOT_FOUND &&
				error.what()[0]);
		}
		library.close();
	}
	std::puts("C++ platform convenience contracts passed");
	return 0;
}
