#ifndef HX_C_PLATFORM_CPP_IO_HPP
#define HX_C_PLATFORM_CPP_IO_HPP

#include <hx_os_pipe.h>
#include <hx/Files.hpp>
#include <hx/Callback.hpp>

namespace hx {

class AsyncPipe;
class IoRequest {
public:
	IoRequest() noexcept
	    : m_request_(NULL)
	{
	}

	IoRequest(IoRequest &&other) noexcept
	    : m_request_(other.m_request_)
	    , m_callback_(std::move(other.m_callback_))
	{
		other.m_request_ = NULL;
	}

	IoRequest &operator=(IoRequest &&other) noexcept
	{
		if (this != &other) {
			hx_os_request_release(m_request_);
			m_request_ = other.m_request_;
			other.m_request_ = NULL;
			m_callback_ = std::move(other.m_callback_);
		}

		return *this;
	}

	IoRequest(const IoRequest &) = delete;

	IoRequest &operator=(const IoRequest &) = delete;

	~IoRequest() noexcept
	{
		hx_os_request_release(m_request_);
	}

	void cancel() const noexcept
	{
		hx_os_request_cancel(m_request_);
	}

	/**
	 * \brief Wait failure has no value; completed I/O failure retains byte
	 * progress.
	 */
	Result<hx_os_completion> waitUntil(
		hx_os_deadline deadline) const noexcept
	{
		hx_os_completion completion = {};
		hx_os_error error = {};

		auto status = hx_os_request_wait_until(
			m_request_, deadline, &completion, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return Result<hx_os_completion>::withValue(
			completion, completion.error.status, completion.error);
	}

	Result<void> callbackResult() const noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_request_wait_until(
			m_request_, HX_OS_DEADLINE_NOW, NULL, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return m_callback_.result();
	}

	hx_os_request *nativeHandle() const noexcept
	{
		return m_request_;
	}

private:
	friend class AsyncPipe;
	hx_os_request *m_request_;
	detail::CallbackOwner m_callback_;

	IoRequest(
		hx_os_request *request, detail::CallbackOwner callback) noexcept
	    : m_request_(request)
	    , m_callback_(std::move(callback))
	{
	}
};

/**
 * \brief Native owner outlives facade and every associated request/resource.
 */
class IoLoop {
public:
	explicit IoLoop(
		hx_os_loop *&owner, hx_os_context *context = NULL) noexcept
	    : m_owner_(&owner)
	    , m_context_(context)
	{
	}

	static Result<IoLoop> create(hx_os_loop *&owner,
		hx_os_context *context = NULL,
		const hx_os_loop_options *options = NULL) noexcept
	{
		if (owner)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};

		auto status = hx_os_loop_create_local(
			context, options, &owner, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return IoLoop(owner, context);
	}

	Result<void> runUntil(hx_os_deadline deadline) const noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_loop_run_until(*m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> closeUntil(hx_os_deadline deadline) noexcept
	{
		if (!*m_owner_)
			return Result<void>();

		hx_os_error error = {};
		auto status =
			hx_os_loop_close_until(*m_owner_, deadline, &error);

		if (status == HX_OS_OK)
			status = hx_os_loop_destroy(m_owner_, &error);

		return detail::Status(status, error);
	}

	hx_os_loop *nativeHandle() const noexcept
	{
		return *m_owner_;
	}

	hx_os_context *context() const noexcept
	{
		return m_context_;
	}

private:
	hx_os_loop **m_owner_;
	hx_os_context *m_context_;
};

class AsyncPipe {
public:
	explicit AsyncPipe(hx_os_pipe *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	static Result<void> createPair(hx_os_pipe *&reader, hx_os_pipe *&writer,
		hx_os_context *context = NULL) noexcept
	{
		if (reader || writer)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};

		auto status = hx_os_pipe_pair_async(
			context, &reader, &writer, &error);

		return detail::Status(status, error);
	}

	template <typename F>
	Result<IoRequest> read(IoLoop &loop, void *data, size_t bytes,
		F &&callback, hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE)
		const noexcept
	{
		return submit(loop, HX_OS_PIPE_READ, data, bytes,
			std::forward<F>(callback), deadline);
	}

	template <typename F>
	Result<IoRequest> write(IoLoop &loop, const void *data, size_t bytes,
		F &&callback, hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE)
		const noexcept
	{
		return submit(loop, HX_OS_PIPE_WRITE, const_cast<void *>(data),
			bytes, std::forward<F>(callback), deadline);
	}

	template <typename F>
	Result<IoRequest> write(IoLoop &loop, StringView text, F &&callback,
		hx_os_deadline deadline =
			HX_OS_DEADLINE_INFINITE) const noexcept
	{
		return write(loop, text.data(), text.size(),
			std::forward<F>(callback), deadline);
	}

	Result<void> closeUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_pipe_close_until(m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

	hx_os_pipe *nativeHandle() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_os_pipe **m_owner_;
	template <typename F>
	static void HX_OS_CALL completed(hx_os_request *,
		const hx_os_completion *completion, void *user) noexcept
	{
		auto *state = static_cast<detail::CallbackModel<F> *>(
			static_cast<detail::CallbackState *>(user));

		state->record(
			detail::InvokeCallback(state->function(), *completion));

		detail::callback_cleanup(state);
	}

	template <typename F>
	Result<IoRequest> submit(IoLoop &loop, int kind, void *data,
		size_t bytes, F &&function,
		hx_os_deadline deadline) const noexcept
	{
		typedef typename std::decay<F>::type Function;

		auto callback = detail::CallbackOwner::create(
			loop.context(), std::forward<F>(function));
		if (!callback)
			return Failure(callback.error());

		auto *state = callback.value().nativeLease();

		hx_os_pipe_operation operation;

		(void)hx_os_pipe_operation_init(
			&operation, sizeof(operation), kind);
		operation.pipe = *m_owner_;
		operation.data = data;
		operation.bytes = bytes;
		operation.deadline = deadline;

		hx_os_request *request = NULL;
		hx_os_error error = {};
		auto status =
			hx_os_loop_submit_pipe(loop.nativeHandle(), &operation,
				completed<Function>, state, &request, &error);

		if (status != HX_OS_OK) {
			state->release();

			return Failure(status, error);
		}

		return IoRequest(request, std::move(callback.value()));
	}
};

class Pipes {
public:
	static Result<std::pair<File, File>> anonymous(
		hx_os_context *context = NULL) noexcept
	{
		hx_os_file *reader = NULL, *writer = NULL;
		hx_os_error error = {};

		auto status = hx_os_pipe_pair_anonymous(
			context, &reader, &writer, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return std::make_pair(File::adopt(reader), File::adopt(writer));
	}
};
} // namespace hx
#endif
