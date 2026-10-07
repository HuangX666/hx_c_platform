#include <hx/Archive.hpp>
#include <hx/Files.hpp>
#include "cpp_check.hpp"
#include <cstdio>

int main()
{
	if (!hx_os_archive_available()) {
		CHECK(hx::Archive::pack("missing", "out.zip").status() ==
			HX_OS_NOT_SUPPORTED);
		return 0;
	}
	char name[100], archive[100], output[100];
	unsigned long long id = hx_os_monotonic_ns();

	std::snprintf(name, sizeof(name), "cpp-archive-%llu.txt", id);
	std::snprintf(archive, sizeof(archive), "cpp-archive-%llu.zip", id);
	std::snprintf(output, sizeof(output), "cpp-extracted-%llu.txt", id);
	CHECK(hx_os_file_save(name, "payload", 7, 0, NULL) == HX_OS_OK);
	auto packed = hx::Archive::pack(name, archive);
	CHECK(packed && packed.value().published == 1);
	size_t count = 0;
	CHECK(hx::Archive::forEach(
		archive, [&count](const hx_os_archive_entry &entry) noexcept {
			CHECK(entry.bytes == 7);
			++count;
		}));
	CHECK(count == 1);
	CHECK(hx::Archive::extractFile(archive, name, output));
	auto text = hx::Files::readText(output);
	CHECK(text && text.value() == "payload");
	CHECK(hx_os_file_remove(output, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(archive, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(name, NULL) == HX_OS_OK);
	return 0;
}
