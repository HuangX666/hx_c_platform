#include <hx_os.h>
#include <hx/Registry.hpp>
#include <hx/Power.hpp>
#include <hx/Obfuscate.hpp>
#include <hx/Calendar.hpp>
#include <hx/TextLookup.hpp>
#include <hx/Bitstream.hpp>
#include <cstdio>
#include <cstdlib>
#include <utility>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
static char hx_owned_path[128];
static bool hx_owned_key;
static void hx_registry_cleanup()
{
	if (hx_owned_key)
		(void)RegDeleteKeyA(HKEY_CURRENT_USER, hx_owned_path);
}
#endif
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
	const auto capabilities = hx_os_capabilities();
	if (capabilities & HX_OS_CAP_CALENDAR)
		CHECK(hx::Calendar::utc(-1) == "1969-12-31T23:59:59Z");
	if (capabilities & HX_OS_CAP_TEXT_LOOKUP) {
		hx::TextLookup lookup;
		lookup.add("hello", "你好😀");
		auto moved = std::move(lookup);
		CHECK(moved.get("hello") == "你好😀");
		CHECK(moved.get("missing") == "missing");
	}
	if (capabilities & HX_OS_CAP_BITSTREAM) {
		unsigned char data[4] = {};
		hx::BitWriter writer(data, sizeof(data));
		writer.write(3, 5);
		writer.align();
		writer.write(9, 257);
		CHECK(writer.bytesWritten() == 3);
		hx::BitReader reader(data, writer.bytesWritten());
		CHECK(reader.read(3) == 5);
		reader.align();
		CHECK(reader.read(9) == 257 && reader.remaining() == 7);
	}
	if (capabilities & HX_OS_CAP_OBFUSCATION) {
		constexpr auto text = hx::obfuscate<0x5a>("C++ 中文😀");
		CHECK(text.decode() == "C++ 中文😀");
		constexpr auto empty = hx::obfuscate<1>("");
		CHECK(empty.decode().empty());
	}
	if (capabilities & HX_OS_CAP_SLEEP_INHIBIT) {
		hx::SleepInhibit first("hx platform unit test");
		hx::SleepInhibit second(std::move(first));
		CHECK(second.active());
		second.setActive(false);
		CHECK(!second.active());
		second.close();
	}
	if (capabilities & HX_OS_CAP_PERFORMANCE_REQUEST) {
		try {
			hx::PerformanceRequest first("hx platform unit test");
			hx::PerformanceRequest second(std::move(first));
			second.close();
		} catch (const hx::StatusError &error) {
			CHECK(error.status() == HX_OS_NOT_SUPPORTED);
		}
	}
#if defined(_WIN32)
	if (capabilities & HX_OS_CAP_REGISTRY) {
		std::snprintf(hx_owned_path, sizeof(hx_owned_path),
			"Software\\hx_cpp_registry_%lu_%llx",
			static_cast<unsigned long>(GetCurrentProcessId()),
			static_cast<unsigned long long>(hx_os_monotonic_ns()));
		CHECK(std::atexit(hx_registry_cleanup) == 0);
		auto key = hx::Registry::user(hx_owned_path,
			HX_OS_REGISTRY_READ | HX_OS_REGISTRY_WRITE |
				HX_OS_REGISTRY_CREATE |
				HX_OS_REGISTRY_EXCLUSIVE);
		hx_owned_key = true;
		key.writeString("name", "中文😀");
		key.writeU32("number", 42);
		auto moved = std::move(key);
		CHECK(moved.readString("name") == "中文😀");
		CHECK(moved.readU32("number") == 42);
		moved.close();
		CHECK(RegDeleteKeyA(HKEY_CURRENT_USER, hx_owned_path) ==
			ERROR_SUCCESS);
		hx_owned_key = false;
	}
#endif
	return 0;
}
