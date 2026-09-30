#ifndef HX_C_PLATFORM_CPP_IO_HPP
#define HX_C_PLATFORM_CPP_IO_HPP

#include <hx_os_pipe.h>
#include <hx/Files.hpp>
#include <functional>
#include <memory>
#include <utility>

namespace hx {
namespace detail {
struct IoCallback {
	std::function<void(const hx_os_completion &)> function;
	std::exception_ptr exception;
	explicit IoCallback(std::function<void(const hx_os_completion &)> fn)
	    : function(std::move(fn))
	{
	}
};
inline void HX_OS_CALL io_completed(
	hx_os_request *, const hx_os_completion *completion, void *user)
{
	std::unique_ptr<std::shared_ptr<IoCallback>> owner(
		static_cast<std::shared_ptr<IoCallback> *>(user));
	try {
		if ((*owner)->function)
			(*owner)->function(*completion);
	} catch (...) {
		(*owner)->exception = std::current_exception();
	}
}
} // namespace detail

class IoRequest {
public:
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
	~IoRequest()
	{
		hx_os_request_release(m_request_);
	}
	void cancel() const noexcept
	{
		hx_os_request_cancel(m_request_);
	}
	hx_os_status waitUntil(hx_os_deadline deadline,
		hx_os_completion *completion = NULL,
		hx_os_error *error = NULL) const noexcept
	{
		return hx_os_request_wait_until(
			m_request_, deadline, completion, error);
	}
	void rethrowCallbackError() const
	{
		hx_os_error error;
		detail::CheckStatus(
			waitUntil(HX_OS_DEADLINE_NOW, NULL, &error), error);
		if (m_callback_->exception)
			std::rethrow_exception(m_callback_->exception);
	}
	hx_os_request *nativeHandle() const noexcept
	{
		return m_request_;
	}

private:
	friend class AsyncPipe;
	hx_os_request *m_request_;
	std::shared_ptr<detail::IoCallback> m_callback_;
	IoRequest(hx_os_request *request,
		std::shared_ptr<detail::IoCallback> callback)
	    : m_request_(request)
	    , m_callback_(std::move(callback))
	{
	}
};

/* A host-owned pointer remains available after facade destruction/timeouts. */
class IoLoop {
public:
	explicit IoLoop(hx_os_loop *&owner) noexcept
	    : m_owner_(&owner)
	{
	}
	static IoLoop create(hx_os_loop *&owner, hx_os_context *context = NULL,
		const hx_os_loop_options *options = NULL)
	{
		if (owner)
			throw StatusError(HX_OS_BUSY);
		hx_os_error error;
		detail::CheckStatus(hx_os_loop_create_local(
					    context, options, &owner, &error),
			error);
		return IoLoop(owner);
	}
	hx_os_status runUntil(hx_os_deadline deadline,
		hx_os_error *error = NULL) const noexcept
	{
		return hx_os_loop_run_until(*m_owner_, deadline, error);
	}
	hx_os_status closeUntil(
		hx_os_deadline deadline, hx_os_error *error = NULL) noexcept
	{
		if (!*m_owner_) {
			hx_os_error_clear(error);
			return HX_OS_OK;
		}
		hx_os_status status =
			hx_os_loop_close_until(*m_owner_, deadline, error);
		return status == HX_OS_OK ? hx_os_loop_destroy(m_owner_, error)
					  : status;
	}
	hx_os_loop *nativeHandle() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_os_loop **m_owner_;
};

class AsyncPipe {
public:
	explicit AsyncPipe(hx_os_pipe *&owner) noexcept
	    : m_owner_(&owner)
	{
	}
	static void createPair(hx_os_pipe *&reader, hx_os_pipe *&writer,
		hx_os_context *context = NULL)
	{
		if (reader || writer)
			throw StatusError(HX_OS_BUSY);
		hx_os_error error;
		detail::CheckStatus(hx_os_pipe_pair_async(
					    context, &reader, &writer, &error),
			error);
	}
	IoRequest read(IoLoop &loop, void *data, size_t bytes,
		std::function<void(const hx_os_completion &)> callback,
		hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) const
	{
		return submit(loop, HX_OS_PIPE_READ, data, bytes,
			std::move(callback), deadline);
	}
	IoRequest write(IoLoop &loop, const void *data, size_t bytes,
		std::function<void(const hx_os_completion &)> callback,
		hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) const
	{
		return submit(loop, HX_OS_PIPE_WRITE, const_cast<void *>(data),
			bytes, std::move(callback), deadline);
	}
	IoRequest write(IoLoop &loop, const std::string &text,
		std::function<void(const hx_os_completion &)> callback,
		hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) const
	{
		return write(loop, text.data(), text.size(),
			std::move(callback), deadline);
	}
	hx_os_status closeUntil(
		hx_os_deadline deadline, hx_os_error *error = NULL) noexcept
	{
		return hx_os_pipe_close_until(m_owner_, deadline, error);
	}
	hx_os_pipe *nativeHandle() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_os_pipe **m_owner_;
	IoRequest submit(IoLoop &loop, int kind, void *data, size_t bytes,
		std::function<void(const hx_os_completion &)> function,
		hx_os_deadline deadline) const
	{
		auto callback = std::make_shared<detail::IoCallback>(
			std::move(function));
		std::unique_ptr<std::shared_ptr<detail::IoCallback>> user(
			new std::shared_ptr<detail::IoCallback>(callback));
		hx_os_pipe_operation op;
		(void)hx_os_pipe_operation_init(&op, sizeof(op), kind);
		op.pipe = *m_owner_;
		op.data = data;
		op.bytes = bytes;
		op.deadline = deadline;
		hx_os_request *request = NULL;
		hx_os_error error;
		detail::CheckStatus(hx_os_loop_submit_pipe(loop.nativeHandle(),
					    &op, detail::io_completed,
					    user.get(), &request, &error),
			error);
		(void)user.release();
		return IoRequest(request, std::move(callback));
	}
};

class Pipes {
public:
	static std::pair<File, File> anonymous(hx_os_context *context = NULL)
	{
		hx_os_file *reader = NULL, *writer = NULL;
		hx_os_error error;
		detail::CheckStatus(hx_os_pipe_pair_anonymous(
					    context, &reader, &writer, &error),
			error);
		return std::make_pair(File::adopt(reader), File::adopt(writer));
	}
};
} // namespace hx
#endif
