#include <hx/Strings.hpp>
#include <hx/Lexer.hpp>
#include <hx/Utilities.hpp>
#include <hx/Procedure.hpp>
#include <hx/TextLookup.hpp>
#include <hx/Calendar.hpp>
#include <hx/Paths.hpp>
#include <hx/Files.hpp>
#include <hx/ThreadPool.hpp>
#include <hx/Settings.hpp>
#include <hx/Parameters.hpp>
#include <hx/Preprocessor.hpp>
#include <hx/Windows.hpp>
#include "cpp_check.hpp"

static void TestResults() noexcept
{
	hx::Result<int> first(42);
	CHECK(first.ok() && first.hasValue() && first.value() == 42);

	auto moved = std::move(first);
	CHECK(moved && !first.hasValue() && first.status() == HX_OS_CLOSED);

	hx_os_error native = {HX_OS_IO_ERROR, HX_OS_ERROR_ERRNO, 17, 23};

	hx::Result<int> failure(hx::Failure(HX_OS_IO_ERROR, native));
	CHECK(!failure && !failure.hasValue() && !failure.get());
	CHECK(failure.error().native_code == 17);

	auto partial = hx::Result<size_t>::withValue(7, HX_OS_IO_ERROR, native);
	CHECK(!partial && partial.hasValue() && partial.value() == 7);
	CHECK(partial.error().cause_code == 23);

	int referred = 9;

	hx::Result<int &> reference(referred);
	CHECK(reference && &reference.value() == &referred);

	hx::Result<void> empty;

	CHECK(empty);

	hx_os_context *context = NULL;

	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);

	{
		auto value =
			hx::String::copy(hx::StringView("a\0b", 3), context);

		CHECK(value && value.value().size() == 3);

		auto destination = hx::String::copy("old", context);
		CHECK(destination);

		destination = std::move(value);
		CHECK(!value.hasValue() &&
			Equal(destination.value(), hx::StringView("a\0b", 3)));
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);
	}

	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
}

struct FailingAllocator {
	size_t attempts;
	size_t fail;
	size_t live;
};

static void *fail_allocate(size_t bytes, void *user)
{
	auto *state = static_cast<FailingAllocator *>(user);

	if (++state->attempts == state->fail)
		return NULL;

	void *memory = std::malloc(bytes);
	if (memory)
		++state->live;

	return memory;
}

static void fail_free(void *memory, void *user)
{
	--static_cast<FailingAllocator *>(user)->live;
	std::free(memory);
}

static void TestAllocationFailure()
{
	FailingAllocator state = {};
	hx_os_context_options options;

	hx_os_context_options_init(&options);
	options.allocator = {fail_allocate, fail_free, &state};

	hx_os_context *context = NULL;

	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);

	{
		auto string = Take(hx::String::copy("preserved", context));
		state.fail = state.attempts + 1;

		char large[4096];

		std::memset(large, 'a', sizeof(large));
		CHECK(string.assign({large, sizeof(large)}).status() ==
			HX_OS_NO_MEMORY);
		CHECK(Equal(string, "preserved"));

		state.fail = state.attempts + 1;
		CHECK(hx::Buffer::allocate(4096, context).status() ==
			HX_OS_NO_MEMORY);

		state.fail = 0;
	}

	if (hx_os_capabilities() & HX_OS_CAP_LEXER) {
		auto lexer = Take(hx::Lexer::create("first second", context));
		state.fail = state.attempts + 1;
		CHECK(lexer.next().status() == HX_OS_NO_MEMORY);

		state.fail = 0;
		CHECK(Equal(Take(lexer.next()).value(), "first"));
	}

	if (hx_os_capabilities() & HX_OS_CAP_PROCEDURES) {
		hx_os_proc_handler *owner = NULL;

		hx::ProcedureHandler procedures(owner);
		Take(procedures.create(context));
		hx_os_atomic captures = HX_OS_ATOMIC_INIT(0);

		CaptureProbe token(captures);

		const size_t baseline = state.live;
		bool succeeded = false;

		for (size_t failure = 1; failure < 12; ++failure) {
			state.fail = state.attempts + failure;

			auto added = procedures.add(
				"probe", [token](hx::CallData &) noexcept {});
			if (added) {
				state.fail = 0;
				Take(procedures.removeUntil(
					added.value(), HX_OS_DEADLINE_NOW));
				succeeded = true;
			}
			else {
				CHECK(added.status() == HX_OS_NO_MEMORY);
			}

			CHECK(state.live == baseline);
			CHECK(hx_os_atomic_load(&captures, HX_OS_ACQUIRE) == 1);
			if (succeeded)
				break;
		}

		CHECK(succeeded);

		Take(procedures.closeUntil(HX_OS_DEADLINE_NOW));
	}

	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(state.live == 0);
}

static void TestDirectCalls()
{
	FailingAllocator state = {};
	hx_os_context_options options;

	hx_os_context_options_init(&options);
	options.allocator = {fail_allocate, fail_free, &state};

	hx_os_context *context = NULL;

	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);

	{
		auto text = Take(hx::String::copy(" \tAb中文😀\r\n", context));

		const size_t before = state.attempts;

		state.fail = before + 1;
		text.trimAscii().lowerAscii();
		CHECK(Equal(text, "ab中文😀"));

		text.upperAscii();
		CHECK(text.startsWith("AB") && text.endsWith("😀"));
		CHECK(text.find("中文") == 2 && state.attempts == before);

		state.fail = 0;
		Take(text.append("!"));
		CHECK(text.endsWith("!"));

		text.clear().trimAscii();
		CHECK(text.empty());
	}

	if (hx_os_capabilities() & (HX_OS_CAP_SIGNALS | HX_OS_CAP_PROCEDURES)) {
		hx::CallData data;

		Take(data.edit().setInt("number", 42)
				.setString("label", {"a\0b", 3})
				.finish());

		auto view = Take(data.view().stringView("label"));
		CHECK(view.size() == 3 && !std::memcmp(view.data(), "a\0b", 3));

		auto failed = data.edit()
				      .setInt("number", 43)
				      .setString("invalid", "\xff")
				      .setInt("skipped", 1)
				      .finish();
		CHECK(failed.status() == HX_OS_INVALID_ENCODING);
		CHECK(Take(data.view().integer("number")) == 43);
		CHECK(data.view().integer("skipped").status() ==
			HX_OS_NOT_FOUND);
	}

	if (hx_os_capabilities() & HX_OS_CAP_SETTINGS) {
		hx_settings *owner = NULL;

		hx::Settings settings(owner);
		Take(settings.create(context));
		CHECK(Take(settings.update()
				      .setInt("port", 8080)
				      .setString("label", {"x\0y", 3})
				      .finish()) == 2);

		const size_t before = state.attempts;

		state.fail = before + 1;
		{
			auto label = Take(settings.stringView("label"));

			auto copy = label;

			auto moved = std::move(label);
			CHECK(copy.size() == 3 && moved.size() == 3);
			CHECK(!std::memcmp(copy.data(), "x\0y", 3));
			CHECK(state.attempts == before);

			state.fail = 0;
			Take(settings.setString("label", "replacement"));
			CHECK(!std::memcmp(moved.data(), "x\0y", 3));

			auto failed = settings.update()
					      .setInt("port", 9090)
					      .setString("invalid", "\xff")
					      .setInt("skipped", 1)
					      .finish();
			CHECK(failed.status() == HX_OS_INVALID_ENCODING &&
				failed.hasValue() && failed.value() == 1);
			CHECK(Take(settings.integer("port")) == 9090);
			CHECK(Take(settings.integerOr("absent", 80)) == 80);
			CHECK(settings.integerOr("label", 80).status() ==
				HX_OS_TYPE_MISMATCH);
			CHECK(Equal(settings.stringOr("absent", "default"),
				"default"));

			hx_settings *source_owner = NULL;
			hx::Settings source(source_owner);
			Take(source.create(context));
			Take(source.setInt("port", 7070));
			CHECK(Take(settings.apply(source)) == 1);
			auto patch =
				Take(source.snapshot(HX_SETTINGS_EXPLICIT));
			CHECK(Take(settings.apply(patch)) == 1);
			CHECK(Take(settings.integer("port")) == 7070);
			Take(source.closeUntil(HX_OS_DEADLINE_NOW));

			Take(settings.closeUntil(HX_OS_DEADLINE_NOW));
			CHECK(!std::memcmp(copy.data(), "x\0y", 3));
			CHECK(hx_os_context_destroy(&context, NULL) ==
				HX_OS_BUSY);
		}
	}

	if ((hx_os_capabilities() & HX_OS_CAP_TEXT_LOOKUP) &&
		(hx_os_capabilities() & HX_OS_CAP_LEXER)) {
		auto table = Take(hx::TextLookup::create(context));
		Take(table.add("title", "标题😀"));

		const size_t before = state.attempts;

		state.fail = before + 1;

		auto view = Take(table.getView("title"));
		CHECK(view.size() == std::strlen("标题😀"));
		CHECK(state.attempts == before);

		state.fail = 0;
	}

	if (hx_os_capabilities() & HX_OS_CAP_CONFIG) {
		auto document =
			Take(hx::ConfigDocument::parse("{\"label\":\"中文😀\"}",
				HX_OS_CONFIG_JSON, NULL, context));
		const size_t before = state.attempts;

		state.fail = before + 1;

		auto view = Take(document.stringView("/label"));
		CHECK(view.size() == std::strlen("中文😀"));
		CHECK(state.attempts == before);

		state.fail = 0;
	}

	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK && !state.live);
}

static hx::Result<size_t> FindPoolFiles(
	hx_os_thread_pool *owner, hx::String folder) noexcept
{
	return hx::Directory::findFiles(
		folder.c_str(), "*.txt", [owner](hx::StringView) noexcept {
			int inside = 0;
			CHECK(hx_os_thread_pool_inside(owner, &inside) ==
				HX_OS_OK);
			CHECK(inside);
		});
}

static void TestDirectoryCalls()
{
	char root[80], path[96], bytes[4];

	std::snprintf(root, sizeof(root), "hx-cpp-visit-%llx",
		static_cast<unsigned long long>(hx_os_monotonic_ns()));
	std::snprintf(path, sizeof(path), "%s/中文😀.txt", root);
	CHECK(hx_os_directory_create(root, 0, NULL) == HX_OS_OK);

	{
		auto file = Take(hx::File::open(path,
			HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
				HX_OS_FILE_EXCLUSIVE));
		CHECK(Take(file.writeAll("ok", 2)) == 2);
	}

	{
		auto file = Take(hx::File::open(path));
		CHECK(Take(file.read(bytes, sizeof(bytes))).bytes == 2);

		auto end = file.read(bytes, sizeof(bytes));
		CHECK(end && end.value().eof && !end.value().bytes);
		CHECK(file.readSome(bytes, sizeof(bytes)).status() ==
			HX_OS_EOF);
	}

	size_t names = 0;

	CHECK(Take(hx::Directory::forEach(root,
		      [&names](hx::StringView name) noexcept {
			      CHECK(!name.empty());
			      ++names;
		      })) == 1 &&
		names == 1);

	CHECK(Take(hx::Directory::forEach(root,
		      [](hx::StringView) noexcept { return false; })) == 1);

	auto failed = hx::Directory::forEach(
		root, [](hx::StringView) noexcept -> hx::Result<void> {
			hx_os_error error = {
				HX_OS_CANCELLED, HX_OS_ERROR_ERRNO, 17, 23};
			return hx::Failure(error);
		});

	CHECK(failed.status() == HX_OS_CANCELLED && failed.value() == 1);
	CHECK(failed.error().native_code == 17 &&
		failed.error().cause_code == 23);
	if (hx_os_capabilities() & HX_OS_CAP_FILE_SEARCH) {
		CHECK(Equal(hx::Directory::findFirstFile(root, "*.txt"), path));
		CHECK(hx::Directory::findFirstFile(root, "absent").status() ==
			HX_OS_NOT_FOUND);
		CHECK(Take(hx::Directory::findFiles(root, "*.txt",
			      [](hx::StringView fullPath) noexcept {
				      CHECK(fullPath.size() > 4);
				      return false;
			      })) == 1);

		auto searchError = hx::Directory::findFiles(root, "*.txt",
			[](hx::StringView) noexcept -> hx::Result<void> {
				hx_os_error error = {HX_OS_CANCELLED,
					HX_OS_ERROR_ERRNO, 31, 47};
				return hx::Failure(error);
			});

		CHECK(searchError.status() == HX_OS_CANCELLED &&
			searchError.hasValue() && searchError.value() == 1);
		CHECK(searchError.error().native_code == 31 &&
			searchError.error().cause_code == 47);
		if (hx_os_capabilities() & HX_OS_CAP_THREAD_POOL) {
			hx_os_thread_pool *owner = NULL;
			auto pool =
				Take(hx::ThreadPool::withThreads(owner, 1, 4));

			auto copied = Take(hx::String::copy(root));

			auto task = Take(pool.submit(
				FindPoolFiles, owner, std::move(copied)));
			CHECK(Take(task.getUntil(
				      hx_os_deadline_after_ms(2000))) == 1);

			Take(pool.shutdownUntil(TestJoinDeadline()));
			CHECK(!owner);
		}
	}
	else {
		auto unsupported = hx::Directory::findFiles(
			root, "*.txt", [](hx::StringView) noexcept {});
		CHECK(unsupported.status() == HX_OS_NOT_SUPPORTED);
	}

	CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_remove(root, NULL) == HX_OS_OK);
}

int main()
{
	if (hx_os_windows_desktop_available()) {
		CHECK(hx::Windows::window(0).status() == HX_OS_INVALID);
		CHECK(hx::Windows::monitor(0).status() == HX_OS_INVALID);
		CHECK(hx::Windows::monitorGpu(0).status() == HX_OS_INVALID);
		CHECK(hx::Windows::gpu(256).status() == HX_OS_LIMIT);
		CHECK(hx::Windows::windowTitle(0).status() == HX_OS_INVALID);
		CHECK(hx::Windows::windowClass(0).status() == HX_OS_INVALID);
		CHECK(hx::Windows::windowProcessPath(0).status() ==
			HX_OS_INVALID);
	}
	else {
		CHECK(hx::Windows::gpu().status() == HX_OS_NOT_SUPPORTED);
		CHECK(hx::Windows::windowTitle(0).status() ==
			HX_OS_NOT_SUPPORTED);
	}
	if (hx_os_windows_version_available()) {
		CHECK(hx::Windows::fileVersion("\xff").status() ==
			HX_OS_INVALID_ENCODING);
		CHECK(hx::Windows::versionString("x", "../x").status() ==
			HX_OS_INVALID);
	}
	else
		CHECK(hx::Windows::fileVersion("x").status() ==
			HX_OS_NOT_SUPPORTED);

	TestResults();
	if (hx_os_preprocess_available()) {
		hx::PreprocessDiagnostic diagnostic;
		auto text = Take(hx::Preprocessor::text(
			"#define TITLE 'Camera'\nTITLE", NULL, &diagnostic));
		hx_os_lexer_options options = HX_OS_LEXER_OPTIONS_INIT;
		options.flags |= HX_OS_LEXER_SKIP_NEWLINES;
		auto lexer =
			Take(hx::Lexer::create(text.view(), NULL, &options));
		CHECK(Equal(lexer.readString(), "Camera"));
		auto failed = hx::Preprocessor::text(
			"#if (\n", NULL, &diagnostic, NULL, "example.cfg");
		CHECK(!failed && diagnostic.line() == 1);
		CHECK(diagnostic.source().size() == 11);
		CHECK(diagnostic.description()[0]);
	}
	else
		CHECK(hx::Preprocessor::text("x").status() ==
			HX_OS_NOT_SUPPORTED);
	if (hx_os_params_available()) {
		auto parameters = Take(hx::Parameters::create());
		const unsigned char binary[] = {0, 0xff, 0x80};

		Take(parameters.setInt("port", 8080));
		Take(parameters.setBool("enabled", true));
		Take(parameters.setUnsigned("sequence", UINT64_MAX));
		Take(parameters.setDouble("ratio", 1.5));
		Take(parameters.setString("label", "device"));
		Take(parameters.setPointer("user", &parameters));
		Take(parameters.setBytes("data", binary, sizeof(binary)));
		CHECK(Take(parameters.integer("port")) == 8080);
		CHECK(Take(parameters.boolean("enabled")));
		CHECK(Take(parameters.unsignedInteger("sequence")) ==
			UINT64_MAX);
		CHECK(Take(parameters.real("ratio")) == 1.5);
		CHECK(Take(parameters.pointer("user")) == &parameters);
		CHECK(Take(parameters.bytesView("data")).size ==
			sizeof(binary));
		CHECK(Equal(parameters.string("label"), "device"));
		CHECK(parameters.integer("label").status() ==
			HX_OS_TYPE_MISMATCH);

		auto copy = Take(parameters.clone());
		hx::Parameters moved = std::move(copy);
		CHECK(!copy.native() && moved.size() == 7);
		Take(parameters.clear());
		CHECK(parameters.size() == 0);
		hx::ParametersView view(moved.native());
		auto label = Take(view.stringView("label"));
		CHECK(label.size() == 6 &&
			!std::memcmp(label.data(), "device", 6));
		Take(moved.remove("port"));
		CHECK(moved.integer("port").status() == HX_OS_NOT_FOUND);
		parameters = std::move(moved);
		CHECK(parameters.size() == 6 && !moved.native());
		auto batch = Take(hx::Parameters::create());
		auto edited =
			batch.edit()
				.setInt("partial", 7)
				.setString("broken", hx::StringView("\xff", 1))
				.setInt("never", 3)
				.finish();
		CHECK(edited.status() == HX_OS_INVALID_ENCODING);
		CHECK(batch.size() == 1 && Take(batch.integer("partial")) == 7);
	}
	else
		CHECK(hx::Parameters::create().status() == HX_OS_NOT_SUPPORTED);
	TestAllocationFailure();
	TestDirectCalls();
	TestDirectoryCalls();

	uint64_t caps = hx_os_capabilities();
	CHECK(Equal(hx::Strings::trim(" \t中文😀  "), "中文😀"));
	CHECK(Equal(hx::Strings::lowerAscii("AB中文"), "ab中文"));
	CHECK(Equal(hx::Strings::replace("a.a", "a", "中文"), "中文.中文"));

	auto parts = Take(hx::Strings::split("one,,two", ","));
	CHECK(parts.size() == 3 && Take(parts.at(1)).size() == 0);

	hx::StringView views[] = {
		Take(parts.at(0)), Take(parts.at(1)), Take(parts.at(2))};

	CHECK(Equal(hx::Strings::join(views, 3, ":"), "one::two"));
	if (caps & HX_OS_CAP_LEXER) {
		auto parser = Take(
			hx::Lexer::create("name = 'value' { [1, 2] }; tail"));
		CHECK(!Take(parser.accept("different")));
		Take(parser.expect("name"));
		Take(parser.expect("="));
		CHECK(Equal(parser.readString(), "value"));
		Take(parser.skipGroup());
		Take(parser.skipUntil("tail"));
		Take(parser.expectKind(HX_OS_TOKEN_END));
		CHECK(parser.readString().status() == HX_OS_TRUNCATED_INPUT);
		hx::Lexer empty;
		CHECK(empty.accept("x").status() == HX_OS_CLOSED);
		CHECK(empty.expect("x").status() == HX_OS_CLOSED);
		CHECK(empty.skipGroup().status() == HX_OS_CLOSED);
		CHECK(empty.skipUntil("x").status() == HX_OS_CLOSED);
		CHECK(empty.readString().status() == HX_OS_CLOSED);
		CHECK(empty.expectKind(HX_OS_TOKEN_END).status() ==
			HX_OS_CLOSED);

		hx::Token token;

		{
			auto lexer =
				Take(hx::Lexer::create("\"\\uD83D\\uDE00\""));

			token = Take(lexer.next());
			CHECK(Equal(token.value(), "😀"));

			auto end = lexer.next();
			CHECK(end.status() == HX_OS_EOF && end.hasValue());
		}

		CHECK(Equal(token.value(), "😀"));
		if (caps & HX_OS_CAP_TEXT_LOOKUP) {
			auto lookup = Take(hx::TextLookup::create());
			Take(lookup.addText("hello=\"你好\""));
			CHECK(Equal(lookup.get("hello"), "你好"));
			CHECK(Equal(lookup.get("missing"), "missing"));
		}
	}

	if (caps & HX_OS_CAP_WIDE_MATH) {
		CHECK(Take(hx::Wide::multiplyDivide(1001, 1000000000, 30000)) ==
			33366666);
		CHECK(hx::Wide::multiplyDivide(1, 1, 0).status() ==
			HX_OS_INVALID);
	}

	if (caps & HX_OS_CAP_PRECISE_WAIT)
		Take(hx::Wait::precise(HX_OS_DEADLINE_NOW));

	size_t calls = 0;

	CHECK(Take(hx::Wait::repeat(1, 10,
		      [&calls]() noexcept { return ++calls < 2; })) == 2);
	if (caps & HX_OS_CAP_CALENDAR) {
		CHECK(Equal(hx::Calendar::filename(0, "txt"),
			"1970-01-01_00-00-00.txt"));
		CHECK(Equal(hx::Calendar::utc(0, "%a_%L"), "Thu_000"));
		CHECK(Equal(hx::Calendar::localFilename(0, "txt", "fixed_%L"),
			"fixed_000.txt"));
		CHECK(!Take(hx::Calendar::local(0)).empty());
		CHECK(!Take(hx::Calendar::filenameNow("txt")).empty());
		CHECK(!Take(hx::Calendar::localFilenameNow("txt")).empty());
		Take(hx::Calendar::localOffsetAt(0));
	}

	if (caps & HX_OS_CAP_PROCEDURES) {
		hx_os_proc_handler *owner = NULL;

		hx::ProcedureHandler procedures(owner);
		Take(procedures.create());

		auto id = Take(procedures.add("double",
			[](hx::CallData &data) noexcept -> hx::Result<void> {
				auto value = data.view().integer("value");
				if (!value)
					return hx::Failure(value.error());
				return data.setInt("result", value.value() * 2);
			}));

		hx::CallData input;

		Take(input.setInt("value", 21));

		auto result = Take(procedures.call("double", input));
		CHECK(Take(result.view().integer("result")) == 42);

		Take(procedures.add("fail",
			[](hx::CallData &) noexcept -> hx::Result<void> {
				hx_os_error error = {HX_OS_ACCESS_DENIED,
					HX_OS_ERROR_ERRNO, 13, 0};
				return hx::Failure(error);
			}));

		auto failed = procedures.call("fail");
		CHECK(failed.status() == HX_OS_ACCESS_DENIED);
		CHECK(failed.error().native_code == 13);

		Take(procedures.removeUntil(id, HX_OS_DEADLINE_NOW));
		Take(procedures.closeUntil(HX_OS_DEADLINE_NOW));
		CHECK(!owner);
	}

	if (caps & HX_OS_CAP_THREAD_POOL) {
		hx_os_thread_pool *owner = NULL;

		auto pool = Take(hx::ThreadPool::withThreads(owner, 1, 4));

		auto task = Take(pool.submit([]() noexcept { return 42; }));
		CHECK(Take(task.get()) == 42);

		Take(pool.shutdownUntil(TestJoinDeadline()));
		CHECK(!owner);
	}

	return 0;
}
