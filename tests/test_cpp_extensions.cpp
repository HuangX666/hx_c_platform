#include <hx_os.h>
#include <hx/Registry.hpp>
#include <hx/Power.hpp>
#include <hx/Obfuscate.hpp>
#include <hx/Calendar.hpp>
#include <hx/TextLookup.hpp>
#include <hx/Bitstream.hpp>
#include <hx/Signal.hpp>
#include <hx/Settings.hpp>
#include <hx/Profiler.hpp>
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
	if (capabilities & HX_OS_CAP_PROFILER) {
		hx_os_profiler *owner = NULL;
		hx::Profiler profiler(owner);
		profiler.create();
		{
			hx::ProfileScope root(
				profiler.native(), "cpp_root", true);
			hx::ProfileScope child(profiler.native(), "cpp_child");
			CHECK(child.finish() == HX_OS_OK);
		}
		CHECK(profiler.dump().find("\"cpp_child\"") !=
			std::string::npos);
		profiler.restartThread();
		CHECK(profiler.closeUntil(HX_OS_DEADLINE_NOW) == HX_OS_OK &&
			!owner);
	}
	if (capabilities & HX_OS_CAP_SETTINGS) {
		hx_settings *owner = NULL;
		hx::Settings settings(owner);
		settings.create();
		settings.setDefaultValue("port", hx::SettingValue::integer(80));
		CHECK(settings.integer("port") == 80);
		settings.setInt("port", 8080).setString("label", "配置😀");
		settings.setValue("items",
			hx::SettingValue::array({hx::SettingValue::boolean(
							 true),
				hx::SettingValue::real(0.5),
				hx::SettingValue::object({{"n",
					hx::SettingValue::integer(7)}})}));
		auto snapshot = settings.snapshot();
		settings.setInt("port", 9090);
		CHECK(snapshot.member("port").asInt() == 8080);
		CHECK(snapshot.member("items").at(2).member("n").asInt() == 7);
		CHECK(settings.string("label") == "配置😀");
		std::string key("a\0b", 3), data("x\0y", 3);
		settings.setString(key, data);
		CHECK(settings.string(key) == data);
		if ((capabilities & HX_OS_CAP_CONFIG) &&
			(capabilities & HX_OS_CAP_CONFIG_SERIALIZE))
			CHECK(!settings.json().empty());
		CHECK(settings.closeUntil(HX_OS_DEADLINE_NOW) == HX_OS_OK &&
			!owner);
		CHECK(snapshot.member("label").asString() == "配置😀");
	}
	if (capabilities & HX_OS_CAP_SIGNALS) {
		hx_os_signal_handler *owner = NULL;
		hx::SignalHandler signal(owner);
		signal.create();
		int count = 0;
		auto lifetime = std::make_shared<int>(42);
		std::weak_ptr<int> weak = lifetime;
		auto connection = signal.connect(
			"ready", [lifetime, &count](hx::CallDataView data) {
				CHECK(data.integer("number") == *lifetime);
				CHECK(data.string("文字") == "值😀");
				++count;
			});
		lifetime.reset();
		hx::CallData data;
		data.setInt("number", 42).setString("文字", "值😀");
		auto copied = data;
		CHECK(signal.emit("ready", copied) == 1 && count == 1);
		CHECK(signal.disconnectUntil(connection, HX_OS_DEADLINE_NOW) ==
				HX_OS_OK &&
			weak.expired());
		CHECK(signal.emit("ready") == 0);
		signal.connect("throw", [](hx::CallDataView) { throw 42; });
		try {
			signal.emit("throw");
			CHECK(false);
		} catch (const hx::StatusError &error) {
			CHECK(error.status() == HX_OS_IO_ERROR);
		}
		CHECK(signal.closeUntil(HX_OS_DEADLINE_NOW) == HX_OS_OK &&
			!owner);
	}
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
	hx_os_atomic32 count = HX_OS_ATOMIC32_INIT(0);
	static_assert(sizeof(count) == 4 && alignof(hx_os_atomic32) == 4,
		"32-bit atomics retain 4-byte size and alignment");
	static_assert(sizeof(hx_os_atomic) == 8 && alignof(hx_os_atomic) == 8,
		"existing 64-bit atomic ABI");
	CHECK(hx_os_atomic32_fetch_add(&count, 1, HX_OS_RELAXED) == 0);
	int32_t expected = 1;
	CHECK(hx_os_atomic32_compare_exchange(
		&count, &expected, 2, HX_OS_ACQ_REL, HX_OS_ACQUIRE));
	return 0;
}
