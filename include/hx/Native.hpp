#ifndef HX_C_PLATFORM_CPP_NATIVE_HPP
#define HX_C_PLATFORM_CPP_NATIVE_HPP

#include <hx_os_native.h>
#include <hx/Status.hpp>

#if !defined(_WIN32)
#include <pthread.h>
#endif

namespace hx {

#if defined(_WIN32)
typedef void *NativeThread;
#else
typedef pthread_t NativeThread;
#endif

/* Non-owning platform values; inspect hx_os_native.h before using them. */
class Native {
public:
	static Result<uintptr_t> file(const hx_os_file *file) noexcept
	{
		return value(hx_os_file_native_borrow, file);
	}

	static Result<uintptr_t> socket(const hx_os_socket *socket) noexcept
	{
		return value(hx_os_socket_native_borrow, socket);
	}

	static Result<uintptr_t> process(const hx_os_process *process) noexcept
	{
		return value(hx_os_process_native_borrow, process);
	}

	static Result<NativeThread> thread(const hx_os_thread *thread) noexcept
	{
		NativeThread native = {};
		hx_os_error error = {};

		auto status = hx_os_thread_native_borrow(
			thread, &native, sizeof(native), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return native;
	}

private:
	template <typename F, typename T>
	static Result<uintptr_t> value(F function, const T *owner) noexcept
	{
		uintptr_t native = 0;
		hx_os_error error = {};

		auto status = function(owner, &native, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return native;
	}
};
} // namespace hx
#endif
