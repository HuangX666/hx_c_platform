#include <hx/SystemInfo.hpp>
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

int main()
{
	if (!(hx_os_capabilities() & HX_OS_CAP_SYSINFO)) {
		try {
			(void)hx::SystemInfo::totalMemory();
			return 1;
		} catch (const hx::StatusError &error) {
			CHECK(error.status() == HX_OS_NOT_SUPPORTED);
		}
		return 0;
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
	std::puts("C++ platform convenience contracts passed");
	return 0;
}
