#include <hx/SystemInfo.hpp>
#include <hx/System.hpp>
#include <hx/Process.hpp>
#include <hx/Local.hpp>
#include <hx/Library.hpp>
#include <hx/Files.hpp>
#include <hx/Environment.hpp>
#include <hx/Paths.hpp>
#include <hx/Thread.hpp>
#include <hx/Io.hpp>
#include <hx/Timer.hpp>
#include <hx/Random.hpp>
#include <hx/Mapping.hpp>
#include <hx/FileExtra.hpp>
#include <hx/Watch.hpp>
#include <hx/SocketOptions.hpp>
#include <hx/Text.hpp>
#include <hx/Config.hpp>
#include "cpp_check.hpp"
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

template <typename T> static void CheckEof(hx::Result<T> result)
{
	CHECK(result.status() == HX_OS_EOF && result.hasValue());
	CHECK(result.value().eof);
}
static void test_cpp_local()
{
	if (hx_os_capabilities() & HX_OS_CAP_DUPLEX_PIPE) {
		auto pair = Take(hx::DuplexPipe::pair());
		char buffer[8];
		CHECK(Take(pair.first.writeAll("pair", 4)) == 4);
		size_t total = 0;
		while (total < 4)
			total += Take(
				pair.second.readSome(buffer + total, 4 - total))
					 .bytes;
		CHECK(!std::memcmp(buffer, "pair", 4));
		Take(hx::DuplexPipe::shutdown(pair.first));
		CheckEof(pair.second.readSome(buffer, sizeof(buffer)));
	}
	if (!(hx_os_capabilities() & HX_OS_CAP_LOCAL_SOCKETS))
		return;
	hx_os_network *network = NULL;
	hx_os_socket *listener_owner = NULL, *client_owner = NULL,
		     *server_owner = NULL;
	hx::LocalSocket listener(listener_owner), client(client_owner),
		server(server_owner);
	char path[108], data[8];
#if defined(_WIN32)
	const char *prefix = "";
#else
	const char *prefix = "/tmp/";
#endif
	std::snprintf(path, sizeof(path), "%shx-cpp-local-%llx.sock", prefix,
		static_cast<unsigned long long>(hx_os_monotonic_ns()));
	CHECK(hx_os_network_create(NULL, &network, NULL) == HX_OS_OK);
	auto listening = listener.listen(network, path);
	if (!listening) {
		CHECK(listening.status() == HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_network_destroy(&network, NULL) == HX_OS_OK);
		return;
	}
	auto connecting = client.connect(network, path, HX_OS_DEADLINE_NOW);
	CHECK(connecting || connecting.status() == HX_OS_TIMEOUT);
	Take(client.finishConnect(hx_os_deadline_after_ms(2000)));
	Take(listener.acceptUntil(server, hx_os_deadline_after_ms(2000)));
	CHECK(Take(client.writeUntil(
		      "C++", 3, hx_os_deadline_after_ms(2000))) == 3);
	size_t total = 0;
	while (total < 3)
		total += Take(server.readUntil(data + total, 3 - total,
				      hx_os_deadline_after_ms(2000)))
				 .bytes;
	CHECK(!std::memcmp(data, "C++", 3));
	CHECK(!Take(client.address(true)).empty());
	Take(client.shutdown());
	CheckEof(server.readUntil(
		data, sizeof(data), hx_os_deadline_after_ms(2000)));
	Take(server.close());
	Take(client.close());
	Take(listener.close());
	CHECK(hx_os_network_destroy(&network, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
}
static void test_system_info()
{
	if (!(hx_os_capabilities() & HX_OS_CAP_SYSINFO)) {
		CHECK(hx::SystemInfo::totalMemory().status() ==
			HX_OS_NOT_SUPPORTED);
		return;
	}
	CHECK(Take(hx::SystemInfo::totalMemory()) > 0);
	CHECK(Take(hx::SystemInfo::logicalCores()) > 0);
	CHECK(Take(hx::SystemInfo::pageSize()) > 0);
	CHECK(Take(hx::SystemInfo::processMemory()).resident_bytes > 0);
	CHECK(Take(hx::SystemInfo::diskSpace(".")).total_bytes > 0);
	CHECK(!Take(hx::SystemInfo::version()).empty());
	auto first = Take(hx::CpuUsage::create());
	hx::CpuUsage second(std::move(first));
	CHECK(hx_os_sleep_until(hx_os_deadline_after_ms(10), NULL) == HX_OS_OK);
	CHECK(Take(second.query()) >= 0);
}
static void *thread_sample(void *user)
{
	return user;
}
static void test_cpp_pipe()
{
	if (hx_os_capabilities() & HX_OS_CAP_PIPE) {
		auto pipes = Take(hx::Pipes::anonymous());
		Take(pipes.second.writeAll("x", 1));
		Take(pipes.second.close());
		char data[4];
		CHECK(Take(pipes.first.readSome(data, sizeof(data))).bytes ==
			1);
		CheckEof(pipes.first.readSome(data, sizeof(data)));
	}
	if (!(hx_os_capabilities() & HX_OS_CAP_ASYNC_PIPE))
		return;
	hx_os_loop *loopOwner = NULL;
	hx_os_pipe *readOwner = NULL, *writeOwner = NULL;
	auto loop = Take(hx::IoLoop::create(loopOwner));
	Take(hx::AsyncPipe::createPair(readOwner, writeOwner));
	hx::AsyncPipe reader(readOwner), writer(writeOwner);
	char data[8];
	int reads = 0;
	auto read = Take(reader.read(loop, data, sizeof(data),
		[&](const hx_os_completion &completion) noexcept {
			CHECK(completion.error.status == HX_OS_OK &&
				completion.bytes == 4);
			++reads;
		}));
	auto write = Take(writer.write(
		loop, "text", [](const hx_os_completion &completion) noexcept {
			CHECK(completion.error.status == HX_OS_OK);
			return HX_OS_CANCELLED;
		}));
	Take(loop.runUntil(hx_os_deadline_after_ms(2000)));
	CHECK(reads == 1 && !std::memcmp(data, "text", 4));
	CHECK(Take(read.waitUntil(HX_OS_DEADLINE_NOW)).bytes == 4);
	Take(read.callbackResult());
	CHECK(write.callbackResult().status() == HX_OS_CANCELLED);
	/* Dropping the request facade must retain the native callback lease. */
	hx_os_atomic captures = HX_OS_ATOMIC_INIT(0);
	{
		CaptureProbe token(captures);
		auto discarded = Take(reader.read(loop, data, sizeof(data),
			[token](const hx_os_completion &completion) noexcept {
				CHECK(completion.error.status ==
					HX_OS_CANCELLED);
			}));
		discarded.cancel();
	}
	CHECK(hx_os_atomic_load(&captures, HX_OS_ACQUIRE) == 1);
	Take(loop.runUntil(hx_os_deadline_after_ms(2000)));
	CHECK(hx_os_atomic_load(&captures, HX_OS_ACQUIRE) == 0);
	Take(reader.closeUntil(HX_OS_DEADLINE_NOW));
	Take(writer.closeUntil(HX_OS_DEADLINE_NOW));
	Take(loop.closeUntil(HX_OS_DEADLINE_NOW));
}
int main(int argc, char **argv)
{
	CHECK(argc == 3);
	const auto capabilities = hx_os_capabilities();
	if (capabilities & HX_OS_CAP_SYSTEM_UTILS) {
		CHECK(!Take(hx::System::arguments()).empty());
		CHECK(!Take(hx::System::hostName()).empty());
		CHECK(Take(hx::System::processId()) != 0);
		Take(hx::System::cpuFeatures());
	}
	if (capabilities & HX_OS_CAP_PROCESS) {
		hx_os_process *owner = NULL;
		hx::Process process(owner);
		Take(process.spawn(argv[2], {"empty"}));
		CHECK(process.identifier());
		CHECK(!Take(process.waitUntil(hx_os_deadline_after_ms(5000)))
				.exit_code);
		Take(process.closeUntil(HX_OS_DEADLINE_NOW));
		CHECK(!owner);
		if (capabilities & HX_OS_CAP_ASYNC_PIPE) {
			hx_os_process_capture *captured = NULL;
			hx::ProcessCapture capture(captured);
			CHECK(capture.run(argv[2], {"delay"},
					     HX_OS_DEADLINE_NOW)
					.status() == HX_OS_TIMEOUT);
			CHECK(captured);
			Take(capture.waitUntil(hx_os_deadline_after_ms(5000)));
			CHECK(Equal(capture.text(), "finished"));
			CHECK(Take(capture.bytes()).size() == 8);
			Take(capture.closeUntil(hx_os_deadline_after_ms(1000)));
			CHECK(!captured);
		}
	}
	test_system_info();
	test_cpp_local();
	test_cpp_pipe();
#if !defined(_WIN32)
	if (capabilities & HX_OS_CAP_LOOP_FD) {
		int descriptors[2];
		CHECK(pipe(descriptors) == 0);
		CHECK(fcntl(descriptors[0], F_SETFL, O_NONBLOCK) == 0);
		hx_os_loop *loopOwner = NULL;
		hx_os_loop_watch *watchOwner = NULL;
		auto loop = Take(hx::IoLoop::create(loopOwner));
		int calls = 0;
		auto watch = Take(hx::DescriptorWatch::create(watchOwner, loop,
			descriptors[0], HX_OS_WAIT_READ,
			[&calls](
				hx_os_loop_watch *native, int events) noexcept {
				char data;
				CHECK(events & HX_OS_WAIT_READ);
				CHECK(read(hx_os_loop_watch_descriptor(native),
					      &data, 1) == 1);
				CHECK(data == 'x');
				++calls;
				hx_os_loop_watch_stop(native);
			}));
		CHECK(close(descriptors[0]) == 0);
		CHECK(write(descriptors[1], "x", 1) == 1);
		Take(loop.runUntil(hx_os_deadline_after_ms(2000)));
		Take(watch.callbackResult());
		CHECK(calls == 1);
		Take(watch.closeUntil(HX_OS_DEADLINE_NOW));
		Take(loop.closeUntil(HX_OS_DEADLINE_NOW));
		CHECK(close(descriptors[1]) == 0);
	}
#endif
	if (capabilities & HX_OS_CAP_VIRTUAL_MEMORY) {
		auto original = Take(hx::Pages::allocate(17));
		hx::Pages pages(std::move(original));
		CHECK(pages.size() >= 17 && pages.data() && !original.data());
		std::memset(pages.data(), 0x5a, 17);
		Take(pages.protect(HX_OS_PAGE_READ));
		CHECK(static_cast<const unsigned char *>(pages.data())[0] ==
			0x5a);
		Take(pages.close());
		CHECK(!pages.data());
	}
	if (capabilities & HX_OS_CAP_RANDOM) {
		auto uuid = Take(hx::Random::uuid());
		CHECK(uuid.size() == 36 && uuid.data()[14] == '4');
		CHECK(uuid.data()[8] == '-' && uuid.data()[13] == '-' &&
			uuid.data()[18] == '-' && uuid.data()[23] == '-');
		CHECK(Take(hx::Random::bytes(513)).size() == 513);
		CHECK(Take(hx::Random::bytes(0)).empty());
	}
	if (capabilities & HX_OS_CAP_TIMER) {
		hx_os_loop *loopOwner = NULL;
		hx_os_timer *timerOwner = NULL;
		auto loop = Take(hx::IoLoop::create(loopOwner));
		hx_os_atomic captures = HX_OS_ATOMIC_INIT(0);
		auto makeTimer = [&]() noexcept {
			CaptureProbe token(captures);
			return hx::PeriodicTimer::every(timerOwner, loop,
				std::chrono::milliseconds(1),
				[token]() noexcept { return HX_OS_CANCELLED; });
		};
		auto timer = Take(makeTimer());
		CHECK(hx_os_atomic_load(&captures, HX_OS_ACQUIRE) == 1);
		Take(loop.runUntil(hx_os_deadline_after_ms(2000)));
		CHECK(hx_os_atomic_load(&captures, HX_OS_ACQUIRE) == 0);
		CHECK(timer.callbackResult().status() == HX_OS_CANCELLED);
		Take(timer.closeUntil(HX_OS_DEADLINE_NOW));
		CHECK(!timerOwner);
		Take(loop.closeUntil(HX_OS_DEADLINE_NOW));
	}
	if (capabilities & HX_OS_CAP_TEXT_NUMBER) {
		CHECK(Take(hx::Text::parseDouble("1.5")) == 1.5);
		CHECK(Equal(hx::Text::formatDouble(1.5), "1.5"));
	}
	if (capabilities & HX_OS_CAP_CONFIG_SERIALIZE) {
		auto original = Take(hx::ConfigDocument::parse(
			"{\"name\":\"value\",\"number\":9007199254740993}"));
		auto copy = original;
		auto text = Take(copy.serialize());
		auto restored = Take(hx::ConfigDocument::parse(text));
		CHECK(Take(restored.integer("/number")) ==
			INT64_C(9007199254740993));
		CHECK(Equal(restored.string("/name"), "value"));
		CHECK(Equal(
			restored.stringOr("/missing", "fallback"), "fallback"));
		CHECK(restored.stringOr("/number", "fallback").status() ==
			HX_OS_TYPE_MISMATCH);
		auto moved = std::move(restored);
		CHECK(Equal(moved.string("/name"), "value"));
	}
	if (capabilities & HX_OS_CAP_BOM) {
		const auto bytes = Take(hx::Text::bomBytes(HX_OS_UTF32LE));
		const auto bom =
			Take(hx::Text::detectBom(bytes.data(), bytes.size()));
		CHECK(bom.found && bom.bytes == 4 &&
			bom.encoding == HX_OS_UTF32LE);
		CHECK(!Take(hx::Text::detectBom("text", 4)).found);
	}
	if (capabilities & HX_OS_CAP_NET_OPTIONS) {
		hx_os_network *network = NULL;
		hx_os_socket *socket = NULL;
		CHECK(hx_os_network_create(NULL, &network, NULL) == HX_OS_OK);
		CHECK(hx_os_socket_create(network, HX_OS_IPV4, HX_OS_TCP,
			      &socket, NULL) == HX_OS_OK);
		hx::SocketOptions options(socket);
		Take(options.noDelay());
		Take(options.keepAlive());
		Take(options.reuseAddress(false));
		CHECK(hx_os_socket_close(&socket, NULL) == HX_OS_OK);
		CHECK(hx_os_network_destroy(&network, NULL) == HX_OS_OK);
	}
	{
		hx_os_thread *owner = NULL;
		auto thread = Take(hx::Thread::create(owner, thread_sample));
		Take(thread.closeUntil(hx_os_deadline_after_ms(2000)));
		CHECK(owner == NULL);
	}
	if (capabilities & HX_OS_CAP_THREAD_EX) {
		auto first = Take(hx::Semaphore::create(1, 1));
		hx::Semaphore second(std::move(first));
		Take(second.waitUntil(HX_OS_DEADLINE_NOW));
		CHECK(second.waitUntil(HX_OS_DEADLINE_NOW).status() ==
			HX_OS_TIMEOUT);
		Take(second.post());
		Take(second.waitUntil(HX_OS_DEADLINE_NOW));
	}
	if (capabilities & HX_OS_CAP_PATHS) {
		CHECK(!Take(hx::Paths::configHome()).empty());
		CHECK(!Take(hx::Paths::dataHome()).empty());
		CHECK(!Take(hx::Paths::programData()).empty());
		CHECK(!Take(hx::Paths::executable()).empty());
	}
	if (capabilities & HX_OS_CAP_ENV) {
		const char *name = "HX_CPP_ENV_TEST_ONLY";
		Take(hx::Environment::set(name, "value"));
		CHECK(Equal(hx::Environment::get(name), "value"));
		Take(hx::Environment::set(name, "kept", false));
		CHECK(Equal(hx::Environment::get(name), "value"));
		Take(hx::Environment::remove(name));
		CHECK(Equal(
			hx::Environment::getOr(name, "fallback"), "fallback"));
	}
	if (capabilities & HX_OS_CAP_FILE_UTILS) {
		char root[128], path[144];
#if defined(_WIN32)
		const char *rootPattern = "hx-cpp-files-%llx";
#else
		const char *rootPattern = "/tmp/hx-cpp-files-%llx";
#endif
		std::snprintf(root, sizeof(root), rootPattern,
			static_cast<unsigned long long>(hx_os_monotonic_ns()));
		CHECK(hx_os_directory_create(root, 0, NULL) == HX_OS_OK);
		std::snprintf(path, sizeof(path), "%s/text.txt", root);
		Take(hx::Files::writeText(path, "hello"));
		if (capabilities & HX_OS_CAP_WATCH) {
			hx_os_watch *owner = NULL;
			hx_os_event *notified = NULL;
			CHECK(hx_os_event_create(NULL, 1, 0, &notified, NULL) ==
				HX_OS_OK);
			hx_os_atomic captures = HX_OS_ATOMIC_INIT(0);
			auto makeWatch = [&]() noexcept {
				CaptureProbe token(captures);
				return hx::DirectoryWatch::create(owner, root,
					[token, notified](hx_os_watch *,
						const hx_os_watch_event
							&event) noexcept {
						if (event.path &&
							!std::strcmp(event.path,
								"text.txt")) {
							hx_os_event_set(
								notified);
							return HX_OS_CANCELLED;
						}
						return HX_OS_OK;
					});
			};
			auto watch = Take(makeWatch());
			Take(hx::Files::writeText(path, "hello"));
			CHECK(hx_os_event_wait_until(notified,
				      hx_os_deadline_after_ms(5000),
				      NULL) == HX_OS_OK);
			Take(watch.closeUntil(hx_os_deadline_after_ms(2000)));
			CHECK(hx_os_atomic_load(&captures, HX_OS_ACQUIRE) == 0);
			CHECK(watch.callbackResult().status() ==
				HX_OS_CANCELLED);
			hx_os_event_destroy(notified);
		}
		if (capabilities & HX_OS_CAP_FILE_EXTRA) {
			auto file = Take(hx::File::open(
				path, HX_OS_FILE_READ | HX_OS_FILE_WRITE));
			auto lock =
				Take(hx::FileRangeLock::acquire(file, 0, 1));
			hx::FileRangeLock moved(std::move(lock));
			Take(moved.unlock());
			hx_os_file_times times = HX_OS_FILE_TIMES_INIT;
			times.fields = HX_OS_TIME_MODIFIED;
			times.modified_seconds = 1600000000;
			Take(hx::FileAttributes::setTimes(path, times));
			Take(hx::FileAttributes::permissions(path,
				HX_OS_PERMISSION_OWNER_READ |
					HX_OS_PERMISSION_OWNER_WRITE));
		}
		if (capabilities & HX_OS_CAP_VIRTUAL_MEMORY) {
			auto original = Take(hx::MappedFile::open(path));
			hx::MappedFile mapped(std::move(original));
			CHECK(mapped.size() == 5 &&
				!std::memcmp(mapped.data(), "hello", 5));
			CHECK(mapped.writableData().status() ==
				HX_OS_ACCESS_DENIED);
			Take(mapped.close());
			auto copy = Take(hx::MappedFile::open(
				path, HX_OS_MAP_PRIVATE_COPY));
			static_cast<char *>(Take(copy.writableData()))[0] = 'H';
			Take(copy.close());
		}
		CHECK(Equal(hx::Files::readText(path), "hello"));
		CHECK(Take(hx::Files::load(path)).size() == 5);
		CHECK(Take(hx::Files::exists(path)));
		CHECK(!Take(hx::Files::absolutePath(path)).empty());
		CHECK(!Take(hx::Files::currentPath()).empty());
		{
			auto file = Take(hx::File::open(path));
			hx::File moved(std::move(file));
			char bytes[8];
			CHECK(Take(moved.size()) == 5);
			if (capabilities & HX_OS_CAP_POSITIONED_IO) {
				CHECK(Take(moved.readAt(1, bytes, 3)).bytes ==
					3);
				CHECK(!std::memcmp(bytes, "ell", 3));
				CheckEof(moved.readAt(5, bytes, 1));
			}
			CHECK(Take(moved.readSome(bytes, sizeof(bytes)))
					.bytes == 5);
			CheckEof(moved.readSome(bytes, sizeof(bytes)));
		}
		{
			auto directory = Take(hx::Directory::open(root));
			const bool info =
				(capabilities & HX_OS_CAP_FILE_INFO) != 0;
			auto entry = Take(directory.next(info));
			CHECK(!std::strcmp(entry.name(), "text.txt"));
			CHECK(entry.kindKnown == info);
			if (info) {
				CHECK(entry.kind == HX_OS_FILE_REGULAR);
				CHECK(Take(hx::Files::info(path)).basic.size ==
					5);
			}
			CHECK(directory.next().status() == HX_OS_EOF);
		}
		Take(hx::Files::removeTree(root));
	}
	if (capabilities & HX_OS_CAP_LIBRARY) {
		auto source = Take(hx::Library::open(argv[1]));
		hx::Library library(std::move(source));
		using Abi = uint32_t(HX_OS_CALL *)(void);
		CHECK(Take(library.symbol<Abi>("hx_plugin_abi_version"))() ==
			hx_os_abi_version());
		hx::String diagnostic;
		auto missing =
			library.symbol<Abi>("hx_missing_symbol", &diagnostic);
		CHECK(missing.status() == HX_OS_NOT_FOUND &&
			!diagnostic.empty());
		Take(library.close());
	}
	std::puts("C++ platform convenience contracts passed");
	return 0;
}
