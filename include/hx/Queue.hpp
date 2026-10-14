#ifndef HX_C_PLATFORM_CPP_QUEUE_HPP
#define HX_C_PLATFORM_CPP_QUEUE_HPP

#include <hx_os_queue.h>
#include <hx/Status.hpp>

namespace hx {

/* Owners must remain at a stable address while used by other threads. Close
 * the message queue, join its callers, then let the owner leave scope. Pointer
 * fields are borrowed; live owning C++ objects cannot be copied as messages.
 */
template <typename T> class MessageQueue {
	static_assert(std::is_trivially_copyable<T>::value,
		"Queue messages must support plain byte copying");

public:
	MessageQueue() noexcept
	    : m_queue_(NULL)
	{
	}

	MessageQueue(MessageQueue &&other) noexcept
	    : m_queue_(other.m_queue_)
	{
		other.m_queue_ = NULL;
	}

	MessageQueue &operator=(MessageQueue &&other) noexcept
	{
		std::swap(m_queue_, other.m_queue_);
		return *this;
	}

	MessageQueue(const MessageQueue &) = delete;
	MessageQueue &operator=(const MessageQueue &) = delete;

	~MessageQueue() noexcept
	{
		(void)hx_os_message_queue_destroy(&m_queue_, NULL);
	}

	static Result<MessageQueue> create(
		size_t capacity, hx_os_context *context = NULL) noexcept
	{
		MessageQueue queue;
		hx_os_error error = {};
		auto status = hx_os_message_queue_create(
			context, sizeof(T), capacity, &queue.m_queue_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return queue;
	}

	Result<void> send(const T &message,
		hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE,
		hx_os_cancel_token *token = NULL) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_message_queue_send_cancellable_until(
			m_queue_, &message, deadline, token, &error);

		return detail::Status(status, error);
	}

	Result<void> trySend(const T &message) noexcept
	{
		return send(message, HX_OS_DEADLINE_NOW);
	}

	Result<void> receive(T &message,
		hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE,
		hx_os_cancel_token *token = NULL) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_message_queue_receive_cancellable_until(
			m_queue_, &message, deadline, token, &error);

		return detail::Status(status, error);
	}

	Result<T> receive(hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE,
		hx_os_cancel_token *token = NULL) noexcept
	{
		static_assert(std::is_nothrow_default_constructible<T>::value,
			"Use receive(T&) for messages without a default "
			"constructor");
		T value = {};
		auto result = receive(value, deadline, token);
		if (!result)
			return Failure(result.error());

		return value;
	}

	Result<void> tryReceive(T &message) noexcept
	{
		return receive(message, HX_OS_DEADLINE_NOW);
	}

	/* Batch results always contain the completed count, also on failure. */
	Result<size_t> sendMany(const T *messages, size_t count,
		hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE,
		hx_os_cancel_token *token = NULL) noexcept
	{
		size_t completed = 0;
		hx_os_error error = {};
		auto status = hx_os_message_queue_send_many_cancellable_until(
			m_queue_, messages, count, deadline, token, &completed,
			&error);

		return Result<size_t>::withValue(completed, status, error);
	}

	Result<size_t> receiveMany(T *messages, size_t maximum,
		hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE,
		hx_os_cancel_token *token = NULL) noexcept
	{
		size_t completed = 0;
		hx_os_error error = {};
		auto status =
			hx_os_message_queue_receive_many_cancellable_until(
				m_queue_, messages, maximum, deadline, token,
				&completed, &error);

		return Result<size_t>::withValue(completed, status, error);
	}

	Result<size_t> trySendMany(const T *messages, size_t count) noexcept
	{
		return sendMany(messages, count, HX_OS_DEADLINE_NOW);
	}

	Result<size_t> tryReceiveMany(T *messages, size_t maximum) noexcept
	{
		return receiveMany(messages, maximum, HX_OS_DEADLINE_NOW);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_message_queue_close(m_queue_, &error);

		return detail::Status(status, error);
	}

	Result<size_t> size() const noexcept
	{
		size_t count = 0;
		hx_os_error error = {};
		auto status =
			hx_os_message_queue_size(m_queue_, &count, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return count;
	}

private:
	hx_os_message_queue *m_queue_;
};

template <typename T> class RingQueue {
	static_assert(std::is_trivially_copyable<T>::value,
		"Ring messages must support plain byte copying");

public:
	RingQueue() noexcept
	    : m_queue_(NULL)
	{
	}

	RingQueue(RingQueue &&other) noexcept
	    : m_queue_(other.m_queue_)
	{
		other.m_queue_ = NULL;
	}

	RingQueue &operator=(RingQueue &&other) noexcept
	{
		std::swap(m_queue_, other.m_queue_);
		return *this;
	}

	RingQueue(const RingQueue &) = delete;
	RingQueue &operator=(const RingQueue &) = delete;

	~RingQueue() noexcept
	{
		(void)hx_os_ring_queue_destroy(&m_queue_, NULL);
	}

	static Result<RingQueue> create(size_t capacity,
		int mode = HX_OS_QUEUE_MPMC, int backend = HX_OS_QUEUE_LOCKED,
		hx_os_context *context = NULL) noexcept
	{
		RingQueue queue;
		hx_os_error error = {};
		auto status = hx_os_ring_queue_create(context, sizeof(T),
			capacity, mode, backend, &queue.m_queue_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return queue;
	}

	Result<void> tryPush(const T &message) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_ring_queue_try_push(m_queue_, &message, &error);

		return detail::Status(status, error);
	}

	Result<void> tryPop(T &message) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_ring_queue_try_pop(m_queue_, &message, &error);

		return detail::Status(status, error);
	}

	Result<size_t> tryPushMany(const T *messages, size_t count) noexcept
	{
		size_t completed = 0;
		hx_os_error error = {};
		auto status = hx_os_ring_queue_try_push_many(
			m_queue_, messages, count, &completed, &error);

		return Result<size_t>::withValue(completed, status, error);
	}

	Result<size_t> tryPopMany(T *messages, size_t maximum) noexcept
	{
		size_t completed = 0;
		hx_os_error error = {};
		auto status = hx_os_ring_queue_try_pop_many(
			m_queue_, messages, maximum, &completed, &error);

		return Result<size_t>::withValue(completed, status, error);
	}

	size_t capacity() const noexcept
	{
		return hx_os_ring_queue_capacity(m_queue_);
	}

	bool isLockFree() const noexcept
	{
		return hx_os_ring_queue_is_lock_free(m_queue_) != 0;
	}

private:
	hx_os_ring_queue *m_queue_;
};

} // namespace hx
#endif
