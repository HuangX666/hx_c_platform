#include <hx/Strings.hpp>
#include <hx/Lexer.hpp>
#include <hx/Utilities.hpp>
#include <hx/Procedure.hpp>
#include <hx/TextLookup.hpp>
#include <hx/Calendar.hpp>
#include <hx/Paths.hpp>
#include <hx/Files.hpp>
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
	uint64_t caps = hx_os_capabilities();
	CHECK(hx::Strings::trim(" \t中文😀  ") == "中文😀");
	CHECK(hx::Strings::lowerAscii("AB中文") == "ab中文");
	CHECK(hx::Strings::replace("a.a", "a", "中文") == "中文.中文");
	auto parts = hx::Strings::split("one,,two", ",");
	CHECK(parts.size() == 3 && parts[1].empty());
	CHECK(hx::Strings::join(parts, ":") == "one::two");
	if (caps & HX_OS_CAP_LEXER) {
		hx::Token token;
		{
			hx::Lexer lexer("\"\\uD83D\\uDE00\"");
			CHECK(lexer.next(token) && token.value() == "😀");
		}
		CHECK(token.value() == "😀");
		if (caps & HX_OS_CAP_TEXT_LOOKUP) {
			hx::TextLookup lookup;
			lookup.addText("hello=\"你好\"");
			CHECK(lookup.get("hello") == "你好");
			CHECK(lookup.get("missing") == "missing");
		}
	}
	if (caps & HX_OS_CAP_WIDE_MATH)
		CHECK(hx::Wide::multiplyDivide(1001, 1000000000, 30000) ==
			33366666);
	if (caps & HX_OS_CAP_PRECISE_WAIT)
		hx::Wait::precise(HX_OS_DEADLINE_NOW);
	size_t calls = 0;
	CHECK(hx::Wait::repeat(1, 10, [&calls] { return ++calls < 2; }) == 2);
	if (caps & HX_OS_CAP_CALENDAR)
		CHECK(hx::Calendar::filename(0, "txt") ==
			"1970-01-01_00-00-00.txt");
	if (caps & HX_OS_CAP_PROCEDURES) {
		hx_os_proc_handler *owner = NULL;
		hx::ProcedureHandler procedures(owner);
		procedures.create();
		auto id = procedures.add("double", [](hx::CallData &data) {
			data.setInt("result", data.view().integer("value") * 2);
		});
		auto result = procedures.call(
			"double", hx::CallData().setInt("value", 21));
		CHECK(result.view().integer("result") == 42);
		procedures.add("fail", [](hx::CallData &) {
			throw hx::StatusError(HX_OS_ACCESS_DENIED);
		});
		try {
			(void)procedures.call("fail");
			CHECK(false);
		} catch (const hx::StatusError &error) {
			CHECK(error.status() == HX_OS_ACCESS_DENIED);
		}
		CHECK(procedures.removeUntil(id, HX_OS_DEADLINE_NOW) ==
			HX_OS_OK);
		CHECK(procedures.closeUntil(HX_OS_DEADLINE_NOW) == HX_OS_OK);
		CHECK(!owner);
	}
	if (caps & HX_OS_CAP_THREAD_POOL) {
		hx_os_thread_pool *owner = NULL;
		auto pool = hx::ThreadPool::withThreads(owner, 1, 4);
		auto task = pool.submit([] { return 42; });
		CHECK(task.get() == 42);
		CHECK(pool.shutdownUntil(HX_OS_DEADLINE_INFINITE) == HX_OS_OK);
		CHECK(!owner);
	}
	return 0;
}
