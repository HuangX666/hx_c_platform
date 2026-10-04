#include <hx/Files.hpp>
#include <hx/Settings.hpp>
#include <hx/Signal.hpp>
#include <cstdio>

static int report_failure(hx_os_status status)
{
	std::fprintf(stderr, "%s\n", hx_os_status_name(status));

	return 1;
}

int main()
{
	auto text = hx::String::copy("  Hello 中文😀  ");
	if (!text)
		return report_failure(text.status());

	text.value().trimAscii().lowerAscii();
	std::puts(text.value().c_str());
	if (hx_os_capabilities() & HX_OS_CAP_SETTINGS) {
		hx_settings *owner = NULL;

		hx::Settings settings(owner);

		auto created = settings.create();
		if (!created)
			return report_failure(created.status());

		auto changed = settings.update()
				       .setInt("port", 8080)
				       .setString("label", "服务😀")
				       .finish();

		auto label = settings.stringView("label");

		auto closed = settings.closeUntil(HX_OS_DEADLINE_NOW);
		if (!closed)
			return report_failure(closed.status());

		if (!changed)
			return report_failure(changed.status());

		if (!label)
			return report_failure(label.status());

		/* The lease owns immutable bytes after the settings close. */
		std::puts(label.value().c_str());
	}

	if (hx_os_capabilities() & (HX_OS_CAP_SIGNALS | HX_OS_CAP_PROCEDURES)) {
		hx::CallData data;

		auto prepared = data.edit()
					.setInt("number", 42)
					.setString("label", text.value().view())
					.finish();
		if (!prepared)
			return report_failure(prepared.status());

		auto label = data.view().stringView("label");
		if (!label)
			return report_failure(label.status());

		std::printf("%.*s\n", static_cast<int>(label.value().size()),
			label.value().data());
	}

	auto listed =
		hx::Directory::forEach(".", [](hx::StringView name) noexcept {
			std::printf("Entry: %.*s\n",
				static_cast<int>(name.size()), name.data());
			return false;
		});

	return listed ? 0 : report_failure(listed.status());
}
