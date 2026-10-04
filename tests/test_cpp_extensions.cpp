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
#include "cpp_check.hpp"
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
int main()
{
	const auto capabilities = hx_os_capabilities();
	if (capabilities & HX_OS_CAP_PROFILER) {
		hx_os_profiler *owner = NULL;

		hx::Profiler profiler(owner);
		Take(profiler.create());
		{
			hx::ProfileScope root(profiler.native());
			Take(root.start("cpp_root", true));

			hx::ProfileScope child(profiler.native());
			Take(child.start("cpp_child"));
			Take(child.finish());
		}

		auto dump = Take(profiler.dump());
		CHECK(std::strstr(dump.c_str(), "\"cpp_child\""));

		Take(profiler.restartThread());
		Take(profiler.closeUntil(HX_OS_DEADLINE_NOW));
		CHECK(!owner);
	}

	if (capabilities & HX_OS_CAP_SETTINGS) {
		hx_settings *owner = NULL;

		hx::Settings settings(owner);
		Take(settings.create());
		Take(settings.setDefaultValue(
			"port", Take(hx::SettingValue::integer(80))));
		CHECK(Take(settings.integer("port")) == 80);

		Take(settings.setInt("port", 8080));
		Take(settings.setString("label", "配置😀"));

		auto nested = Take(hx::SettingValue::object(
			{{"n", Take(hx::SettingValue::integer(7))}}));

		auto items = Take(hx::SettingValue::array(
			{Take(hx::SettingValue::boolean(true)),
				Take(hx::SettingValue::real(0.5)), nested}));
		Take(settings.setValue("items", items));

		auto snapshot = Take(settings.snapshot());
		Take(settings.setInt("port", 9090));
		CHECK(Take(Take(snapshot.member("port")).asInt()) == 8080);

		auto third = Take(Take(snapshot.member("items")).at(2));
		CHECK(Take(Take(third.member("n")).asInt()) == 7);
		CHECK(Equal(settings.string("label"), "配置😀"));

		hx::StringView key("a\0b", 3), data("x\0y", 3);
		Take(settings.setString(key, data));
		CHECK(Equal(settings.string(key), data));
		if ((capabilities & HX_OS_CAP_CONFIG) &&
			(capabilities & HX_OS_CAP_CONFIG_SERIALIZE))
			CHECK(!Take(settings.json()).empty());
		Take(settings.closeUntil(HX_OS_DEADLINE_NOW));
		CHECK(!owner);
		CHECK(Equal(
			Take(snapshot.member("label")).asString(), "配置😀"));
	}

	if (capabilities & HX_OS_CAP_SIGNALS) {
		hx_os_signal_handler *owner = NULL;

		hx::SignalHandler signal(owner);
		Take(signal.create());

		int count = 0;

		hx_os_atomic captures = HX_OS_ATOMIC_INIT(0);

		uint64_t connection = 0;

		{
			CaptureProbe token(captures);
			connection = Take(signal.connect("ready",
				[token, &count](
					hx::CallDataView data) noexcept {
					CHECK(Take(data.integer("number")) ==
						42);
					CHECK(Equal(
						data.string("文字"), "值😀"));
					++count;
				}));
		}

		CHECK(hx_os_atomic_load(&captures, HX_OS_ACQUIRE) == 1);

		hx::CallData data;

		Take(data.setInt("number", 42));
		Take(data.setString("文字", "值😀"));

		auto copied = data;

		CHECK(Take(signal.emit("ready", copied)) == 1 && count == 1);

		Take(signal.disconnectUntil(connection, HX_OS_DEADLINE_NOW));
		CHECK(hx_os_atomic_load(&captures, HX_OS_ACQUIRE) == 0);
		CHECK(Take(signal.emit("ready")) == 0);

		Take(signal.connect("failure",
			[](hx::CallDataView) noexcept -> hx::Result<void> {
				hx_os_error error = {HX_OS_IO_ERROR,
					HX_OS_ERROR_ERRNO, 5, 0};
				return hx::Failure(error);
			}));

		auto failed = signal.emit("failure");
		CHECK(failed.status() == HX_OS_IO_ERROR);
		CHECK(failed.error().native_code == 5);

		Take(signal.closeUntil(HX_OS_DEADLINE_NOW));
		CHECK(!owner);
	}

	if (capabilities & HX_OS_CAP_CALENDAR)
		CHECK(Equal(hx::Calendar::utc(-1), "1969-12-31T23:59:59Z"));

	if (capabilities & HX_OS_CAP_TEXT_LOOKUP) {
		auto lookup = Take(hx::TextLookup::create());
		Take(lookup.add("hello", "你好😀"));

		auto moved = std::move(lookup);
		CHECK(Equal(moved.get("hello"), "你好😀"));
		CHECK(Equal(moved.get("missing"), "missing"));
	}

	if (capabilities & HX_OS_CAP_BITSTREAM) {
		unsigned char data[4] = {};

		auto writer = Take(hx::BitWriter::create(data, sizeof(data)));
		Take(writer.write(3, 5));
		Take(writer.align());
		Take(writer.write(9, 257));
		CHECK(writer.bytesWritten() == 3);

		auto reader = Take(
			hx::BitReader::create(data, writer.bytesWritten()));
		CHECK(Take(reader.read(3)) == 5);

		Take(reader.align());
		CHECK(Take(reader.read(9)) == 257 && reader.remaining() == 7);
	}

	if (capabilities & HX_OS_CAP_OBFUSCATION) {
		constexpr auto text = hx::obfuscate<0x5a>("C++ 中文😀");

		CHECK(Equal(text.decode(), "C++ 中文😀"));

		constexpr auto empty = hx::obfuscate<1>("");

		CHECK(Take(empty.decode()).empty());
	}

	if (capabilities & HX_OS_CAP_SLEEP_INHIBIT) {
		auto first =
			Take(hx::SleepInhibit::create("hx platform unit test"));

		hx::SleepInhibit second(std::move(first));
		CHECK(second.active());

		Take(second.setActive(false));
		CHECK(!second.active());

		Take(second.close());
	}

	if (capabilities & HX_OS_CAP_PERFORMANCE_REQUEST) {
		auto first =
			hx::PerformanceRequest::create("hx platform unit test");

		CHECK(first || first.status() == HX_OS_NOT_SUPPORTED);
		if (first) {
			auto second = std::move(first).value();
			Take(second.close());
		}
	}
#if defined(_WIN32)
	if (capabilities & HX_OS_CAP_REGISTRY) {
		std::snprintf(hx_owned_path, sizeof(hx_owned_path),
			"Software\\hx_cpp_registry_%lu_%llx",
			static_cast<unsigned long>(GetCurrentProcessId()),
			static_cast<unsigned long long>(hx_os_monotonic_ns()));
		CHECK(std::atexit(hx_registry_cleanup) == 0);

		auto key = Take(hx::Registry::user(hx_owned_path,
			HX_OS_REGISTRY_READ | HX_OS_REGISTRY_WRITE |
				HX_OS_REGISTRY_CREATE |
				HX_OS_REGISTRY_EXCLUSIVE));
		hx_owned_key = true;
		Take(key.writeString("name", "中文😀"));
		Take(key.writeU32("number", 42));

		auto moved = std::move(key);
		CHECK(Equal(moved.readString("name"), "中文😀"));
		CHECK(Take(moved.readU32("number")) == 42);

		Take(moved.close());
		CHECK(RegDeleteKeyA(HKEY_CURRENT_USER, hx_owned_path) ==
			ERROR_SUCCESS);

		hx_owned_key = false;
	}
#endif
	hx_os_atomic32 count = HX_OS_ATOMIC32_INIT(0);
	static_assert(sizeof(count) == 4 && alignof(hx_os_atomic32) == 4,
		"32-bit atomic ABI");
	static_assert(sizeof(hx_os_atomic) == 8 && alignof(hx_os_atomic) == 8,
		"64-bit atomic ABI");
	CHECK(hx_os_atomic32_fetch_add(&count, 1, HX_OS_RELAXED) == 0);

	int32_t expected = 1;

	CHECK(hx_os_atomic32_compare_exchange(
		&count, &expected, 2, HX_OS_ACQ_REL, HX_OS_ACQUIRE));

	return 0;
}
