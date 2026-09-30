#include <hx/SystemInfo.hpp>
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
#include <hx/SocketOptions.hpp>
#include <hx/Text.hpp>
#include <hx/Config.hpp>
#include <hx/ThreadPool.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

static void *thread_sample(void *user)
{
	return user;
}

static void test_cpp_pipe()
{
	if (hx_os_capabilities() & HX_OS_CAP_PIPE) {
		auto pipes = hx::Pipes::anonymous();
		pipes.second.writeAll("x", 1);
		pipes.second.close();
		char data[4];
		CHECK(pipes.first.readSome(data, sizeof(data)).bytes == 1);
		CHECK(pipes.first.readSome(data, sizeof(data)).eof);
	}
	if (!(hx_os_capabilities() & HX_OS_CAP_ASYNC_PIPE))
		return;
	hx_os_loop *loopOwner = NULL;
	hx_os_pipe *readOwner = NULL, *writeOwner = NULL;
	auto loop = hx::IoLoop::create(loopOwner);
	hx::AsyncPipe::createPair(readOwner, writeOwner);
	hx::AsyncPipe reader(readOwner), writer(writeOwner);
	char data[8];
	int reads = 0;
	auto read = reader.read(loop, data, sizeof(data),
		[&](const hx_os_completion &completion) {
			CHECK(completion.error.status == HX_OS_OK &&
				completion.bytes == 4);
			++reads;
		});
	auto write = writer.write(loop, std::string("text"),
		[](const hx_os_completion &completion) {
			CHECK(completion.error.status == HX_OS_OK);
			throw hx::StatusError(HX_OS_CANCELLED);
		});
	CHECK(loop.runUntil(hx_os_deadline_after_ms(2000)) == HX_OS_OK);
	CHECK(reads == 1 && !std::memcmp(data, "text", 4));
	read.rethrowCallbackError();
	try {
		write.rethrowCallbackError();
		CHECK(false);
	} catch (const hx::StatusError &error) {
		CHECK(error.status() == HX_OS_CANCELLED);
	}
	CHECK(reader.closeUntil(HX_OS_DEADLINE_NOW) == HX_OS_OK);
	CHECK(writer.closeUntil(HX_OS_DEADLINE_NOW) == HX_OS_OK);
	CHECK(loop.closeUntil(HX_OS_DEADLINE_NOW) == HX_OS_OK);
}

int main(int argc, char **argv)
{
	CHECK(argc == 2);
	{
		hx_os_error native = {HX_OS_IO_ERROR, HX_OS_ERROR_NONE, 123, 0};
		hx::StatusError error(native);
		CHECK(error.message() == "I/O error [domain=0 code=123]");
		CHECK(std::string(error.what()) == "I/O error");
	}
	TestSystemInfo();
	test_cpp_pipe();
	if (hx_os_capabilities() & HX_OS_CAP_VIRTUAL_MEMORY) {
		hx::Pages original(17);
		hx::Pages pages(std::move(original));
		CHECK(pages.size() >= 17 && pages.data() && !original.data());
		std::memset(pages.data(), 0x5a, 17);
		pages.protect(HX_OS_PAGE_READ);
		CHECK(static_cast<const unsigned char *>(pages.data())[0] ==
			0x5a);
		pages.close();
		CHECK(!pages.data());
	}
	if (hx_os_capabilities() & HX_OS_CAP_RANDOM) {
		auto uuid = hx::Random::uuid();
		CHECK(uuid.size() == 36 && uuid[14] == '4');
		CHECK(uuid[8] == '-' && uuid[13] == '-' && uuid[18] == '-' &&
			uuid[23] == '-');
		CHECK(hx::Random::bytes(513).size() == 513);
		CHECK(hx::Random::bytes(0).empty());
	}
	if (hx_os_capabilities() & HX_OS_CAP_TIMER) {
		hx_os_loop *loopOwner = NULL;
		hx_os_timer *timerOwner = NULL;
		auto loop = hx::IoLoop::create(loopOwner);
		auto token = std::make_shared<int>(0);
		std::weak_ptr<int> observer = token;
		auto timer = hx::PeriodicTimer::every(timerOwner, loop,
			std::chrono::milliseconds(1), [token]() {
				++*token;
				throw hx::StatusError(HX_OS_CANCELLED);
			});
		token.reset();
		CHECK(!observer.expired());
		CHECK(loop.runUntil(hx_os_deadline_after_ms(2000)) == HX_OS_OK);
		CHECK(observer.expired());
		try {
			timer.rethrowCallbackError();
			CHECK(false);
		} catch (const hx::StatusError &error) {
			CHECK(error.status() == HX_OS_CANCELLED);
		}
		CHECK(timer.closeUntil(HX_OS_DEADLINE_NOW) == HX_OS_OK);
		CHECK(!timerOwner);
		CHECK(loop.closeUntil(HX_OS_DEADLINE_NOW) == HX_OS_OK);
	}
	if (hx_os_capabilities() & HX_OS_CAP_TEXT_NUMBER) {
		CHECK(hx::Text::parseDouble("1.5") == 1.5);
		CHECK(hx::Text::formatDouble(1.5) == "1.5");
	}
	if (hx_os_capabilities() & HX_OS_CAP_CONFIG_SERIALIZE) {
		auto original = hx::ConfigDocument::parse(
			"{\"name\":\"value\",\"number\":9007199254740993}");
		auto copy = original;
		auto restored = hx::ConfigDocument::parse(copy.serialize());
		CHECK(restored.integer("/number") == INT64_C(9007199254740993));
		CHECK(restored.string("/name") == "value");
		CHECK(restored.stringOr("/missing", "fallback") == "fallback");
		try {
			(void)restored.stringOr("/number", "fallback");
			CHECK(false);
		} catch (const hx::StatusError &error) {
			CHECK(error.status() == HX_OS_TYPE_MISMATCH);
		}
		auto moved = std::move(restored);
		CHECK(moved.string("/name") == "value");
	}
	if (hx_os_capabilities() & HX_OS_CAP_BOM) {
		const auto bytes = hx::Text::bomBytes(HX_OS_UTF32LE);
		const auto bom =
			hx::Text::detectBom(bytes.data(), bytes.size());
		CHECK(bom.found && bom.bytes == 4 &&
			bom.encoding == HX_OS_UTF32LE);
		CHECK(!hx::Text::detectBom("text", 4).found);
	}
	if (hx_os_capabilities() & HX_OS_CAP_NET_OPTIONS) {
		hx_os_network *network = NULL;
		hx_os_socket *socket = NULL;
		CHECK(hx_os_network_create(NULL, &network, NULL) == HX_OS_OK);
		CHECK(hx_os_socket_create(network, HX_OS_IPV4, HX_OS_TCP,
			      &socket, NULL) == HX_OS_OK);
		hx::SocketOptions options(socket);
		options.noDelay();
		options.keepAlive();
		options.reuseAddress(false);
		CHECK(hx_os_socket_close(&socket, NULL) == HX_OS_OK);
		CHECK(hx_os_network_destroy(&network, NULL) == HX_OS_OK);
	}
	{
		hx_os_thread *owner = NULL;
		auto thread = hx::Thread::create(owner, thread_sample);
		CHECK(thread.closeUntil(hx_os_deadline_after_ms(2000)) ==
			HX_OS_OK);
		CHECK(owner == NULL);
	}
	if (hx_os_capabilities() & HX_OS_CAP_THREAD_EX) {
		hx::Semaphore first(1, 1);
		hx::Semaphore second(std::move(first));
		CHECK(second.waitUntil(HX_OS_DEADLINE_NOW) == HX_OS_OK);
		CHECK(second.waitUntil(HX_OS_DEADLINE_NOW) == HX_OS_TIMEOUT);
		second.post();
		CHECK(second.waitUntil(HX_OS_DEADLINE_NOW) == HX_OS_OK);
	}
	if (hx_os_capabilities() & HX_OS_CAP_PATHS) {
		CHECK(!hx::Paths::configHome().empty());
		CHECK(!hx::Paths::dataHome().empty());
		CHECK(!hx::Paths::programData().empty());
		CHECK(!hx::Paths::executable().empty());
	}
	if (hx_os_capabilities() & HX_OS_CAP_ENV) {
		const char *name = "HX_CPP_ENV_TEST_ONLY";
		hx::Environment::set(name, "value");
		CHECK(hx::Environment::get(name) == "value");
		hx::Environment::set(name, "kept", false);
		CHECK(hx::Environment::get(name) == "value");
		hx::Environment::remove(name);
		CHECK(hx::Environment::getOr(name, "fallback") == "fallback");
	}
	if (hx_os_capabilities() & HX_OS_CAP_FILE_UTILS) {
		char root[128];
		(void)std::snprintf(root, sizeof(root), "hx-cpp-files-%llx",
			(unsigned long long)hx_os_monotonic_ns());
		CHECK(hx_os_directory_create(root, 0, NULL) == HX_OS_OK);
		const std::string path = std::string(root) + "/text.txt";
		hx::Files::writeText(path.c_str(), "hello");
		if (hx_os_capabilities() & HX_OS_CAP_FILE_EXTRA) {
			hx::File file(path.c_str(),
				HX_OS_FILE_READ | HX_OS_FILE_WRITE);
			hx::FileRangeLock lock(file, 0, 1);
			hx::FileRangeLock moved(std::move(lock));
			moved.unlock();
			hx_os_file_times times = HX_OS_FILE_TIMES_INIT;
			times.fields = HX_OS_TIME_MODIFIED;
			times.modified_seconds = 1600000000;
			hx::FileAttributes::setTimes(path.c_str(), times);
			hx::FileAttributes::permissions(path.c_str(),
				HX_OS_PERMISSION_OWNER_READ |
					HX_OS_PERMISSION_OWNER_WRITE);
		}
		if (hx_os_capabilities() & HX_OS_CAP_VIRTUAL_MEMORY) {
			hx::MappedFile original(path.c_str());
			hx::MappedFile mapped(std::move(original));
			CHECK(mapped.size() == 5 &&
				!std::memcmp(mapped.data(), "hello", 5));
			try {
				(void)mapped.writableData();
				CHECK(false);
			} catch (const hx::StatusError &error) {
				CHECK(error.status() == HX_OS_ACCESS_DENIED);
			}
			mapped.close();
			hx::MappedFile copy(
				path.c_str(), HX_OS_MAP_PRIVATE_COPY);
			static_cast<char *>(copy.writableData())[0] = 'H';
			copy.close();
		}
		CHECK(hx::Files::readText(path.c_str()) == "hello");
		CHECK(hx::Files::load(path.c_str()).size() == 5);
		CHECK(hx::Files::exists(path.c_str()));
		CHECK(!hx::Files::absolutePath(path.c_str()).empty());
		CHECK(!hx::Files::currentPath().empty());
		{
			hx::File file(path.c_str());
			hx::File moved(std::move(file));
			char bytes[8];
			CHECK(moved.size() == 5);
			if (hx_os_capabilities() & HX_OS_CAP_POSITIONED_IO) {
				CHECK(moved.readAt(1, bytes, 3).bytes == 3);
				CHECK(!std::memcmp(bytes, "ell", 3));
				CHECK(moved.readAt(5, bytes, 1).eof);
			}
			CHECK(moved.readSome(bytes, sizeof(bytes)).bytes == 5);
			CHECK(moved.readSome(bytes, sizeof(bytes)).eof);
		}
		{
			hx::Directory directory(root);
			std::string name;
			if (hx_os_capabilities() & HX_OS_CAP_FILE_INFO) {
				unsigned kind;
				CHECK(directory.next(name, kind) &&
					name == "text.txt");
				CHECK(kind == HX_OS_FILE_REGULAR);
				CHECK(hx::Files::info(path.c_str())
						.basic.size == 5);
			}
			else
				CHECK(directory.next(name) &&
					name == "text.txt");
			CHECK(!directory.next(name));
		}
		hx::Files::removeTree(root);
	}
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
