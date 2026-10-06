#define emit
#include <hx/Signal.hpp>
#include "cpp_check.hpp"

#ifndef emit
#error "The caller's emit macro must be restored."
#endif

int main()
{
	hx_os_signal_handler *owner = NULL;
	hx::SignalHandler signal(owner);

	if (!(hx_os_capabilities() & HX_OS_CAP_SIGNALS)) {
		CHECK(signal.create().status() == HX_OS_NOT_SUPPORTED);

		return 0;
	}
	Take(signal.create());
	int calls = 0;
	Take(signal.connectAll([&calls](hx::StringView name,
				       hx::SignalArguments arguments) noexcept {
		CHECK(name.size() == 5 &&
			!std::memcmp(name.data(), "macro", 5) &&
			!arguments.size());
		++calls;
	}));
	CHECK(Take(signal.notify("macro")) == 1 && calls == 1);
	Take(signal.closeUntil(HX_OS_DEADLINE_NOW));

	return 0;
}
