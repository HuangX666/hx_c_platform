#define emit
#define signals hx_forbidden_signals_macro
#define slots hx_forbidden_slots_macro
#include <hx/Procedure.hpp>
#include "cpp_check.hpp"

static void test_signals()
{
	hx_os_signal_handler *owner = NULL;
	hx::SignalHandler handler(owner);
	int calls = 0;

	if (!hx_os_params_available() ||
		!(hx_os_capabilities() & HX_OS_CAP_SIGNALS)) {
		CHECK(handler.connectMutable("volume",
				     [](hx::ParametersRef) noexcept {})
				.status() == HX_OS_NOT_SUPPORTED);
		CHECK(handler.notifyMutable("volume", hx::ParametersRef(NULL))
				.status() == HX_OS_NOT_SUPPORTED);

		return;
	}
	Take(handler.create());
	auto params = Take(hx::Parameters::create());
	Take(params.setInt("volume", 80));
	Take(handler.connectMutable("volume",
		[](hx::ParametersRef data) noexcept -> hx::Result<void> {
			Take(data.setInt("volume", 60));

			return hx::Failure(HX_OS_IO_ERROR);
		}));
	Take(handler.observe("volume",
		[&calls](hx::StringView, hx::SignalArguments data) noexcept {
			CHECK(Take(data.integer("volume")) == 60);
			++calls;
		}));
	Take(handler.connectAllMutable(
		[](hx::StringView name,
			hx::ParametersRef data) noexcept -> hx::Result<void> {
			CHECK(name.size() == 6);

			return data.edit()
				.setBytes("payload", "\0\xff", 2)
				.setUnsigned("id", UINT64_MAX)
				.finish();
		}));
	Take(handler.connectAll(
		[&calls](hx::StringView, hx::SignalArguments data) noexcept {
			CHECK(Take(data.unsignedInteger("id")) == UINT64_MAX);
			CHECK(Take(data.bytesView("payload")).size == 2);
			++calls;
		}));
	auto notified = handler.notifyMutable("volume", params);
	CHECK(notified.status() == HX_OS_IO_ERROR && notified.hasValue());
	CHECK(notified.value() == 4 && calls == 2);
	CHECK(Take(params.integer("volume")) == 60);
	CHECK(Take(params.bytesView("payload")).size == 2);
	Take(handler.closeUntil(HX_OS_DEADLINE_NOW));
}

static void test_procedures()
{
	hx_os_proc_handler *owner = NULL;
	hx::ProcedureHandler procedures(owner);

	if (!hx_os_params_available() ||
		!(hx_os_capabilities() & HX_OS_CAP_PROCEDURES)) {
		CHECK(procedures.addParams("echo",
					[](hx::ParametersRef) noexcept {})
				.status() == HX_OS_NOT_SUPPORTED);
		CHECK(procedures.callParams("echo").status() ==
			HX_OS_NOT_SUPPORTED);

		return;
	}
	Take(procedures.create());
	auto input = Take(hx::Parameters::create());
	Take(input.setString("name", "device😀"));
	Take(procedures.addParams("echo", [](hx::ParametersRef data) noexcept {
		return data.setBytes("reply", "\0\xff", 2);
	}));
	Take(procedures.addParams("fail",
		[](hx::ParametersRef data) noexcept -> hx::Result<void> {
			Take(data.setInt("partial", 1));

			return hx::Failure(HX_OS_ACCESS_DENIED);
		}));
	auto output = Take(procedures.callParams("echo", input));
	CHECK(Equal(output.string("name"), "device😀"));
	CHECK(Take(output.bytesView("reply")).size == 2);
	CHECK(input.bytesView("reply").status() == HX_OS_NOT_FOUND);
	CHECK(procedures.callParams("fail", input).status() ==
		HX_OS_ACCESS_DENIED);
	CHECK(input.integer("partial").status() == HX_OS_NOT_FOUND);
	Take(procedures.closeUntil(HX_OS_DEADLINE_NOW));
	CHECK(Take(output.bytesView("reply")).size == 2);
}

int main()
{
	test_signals();
	test_procedures();

	return 0;
}
