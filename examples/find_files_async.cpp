#include <hx/Files.hpp>
#include <hx/System.hpp>
#include <hx/ThreadPool.hpp>
#include <cstdio>
#include "example_arguments.h"

static void PrintFile(hx::StringView path) noexcept
{
	std::printf("%.*s\n", static_cast<int>(path.size()), path.data());
}

static hx::Result<size_t> SearchFiles(
	hx::String root, hx::String pattern) noexcept
{
	return hx::Directory::findFiles(
		root.c_str(), pattern.c_str(), PrintFile);
}

static int hx_example_main(int argc, const char *const *argv)
{
	uint64_t needed = HX_OS_CAP_FILE_SEARCH | HX_OS_CAP_THREAD_POOL;

	if ((hx_os_capabilities() & needed) != needed) {
		std::puts("File search or thread pools are disabled");

		return 0;
	}

	if (argc < 2) {
		std::puts(
			"Usage: hx_os_cpp_find_files ROOT [BASENAME_PATTERN]");

		return 2;
	}

	auto root = hx::String::copy(argv[1]);

	auto pattern = hx::String::copy(argc > 2 ? argv[2] : "*");
	if (!root || !pattern)
		return 1;

	hx_os_thread_pool *owner = NULL;

	auto created = hx::ThreadPool::withThreads(owner, 1, 4);
	if (!created)
		return 1;

	auto &pool = created.value();

	auto submitted = pool.submit(SearchFiles, std::move(root).value(),
		std::move(pattern).value());

	hx_os_status work = submitted.status();
	if (submitted) {
		// A GUI continues its event loop here; it must not block on
		// get.
		auto result = submitted.value().getUntil(
			hx_os_deadline_after_ms(2000));
		work = result.status();
		if (result.hasValue())
			std::printf("Files: %zu\n", result.value());
	}

	if (work != HX_OS_OK)
		std::fprintf(stderr, "Search: %s\n", hx_os_status_name(work));

	auto closed = pool.shutdownUntil(
		hx_os_deadline_after_ms(2000), HX_OS_POOL_CANCEL_PENDING);
	if (!closed) {
		// A service retains owner/task state and retries close later.
		std::fprintf(stderr, "Close pending: %s\n",
			hx_os_status_name(closed.status()));

		return 1;
	}

	return work == HX_OS_OK ? 0 : 1;
}
