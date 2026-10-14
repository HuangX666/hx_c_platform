#ifndef HX_C_PLATFORM_CPP_PERSISTENT_QUEUE_HPP
#define HX_C_PLATFORM_CPP_PERSISTENT_QUEUE_HPP

#include <hx_os_persistent_queue.h>
#include <hx/Values.hpp>
#include <utility>

namespace hx {
class PersistentQueue;

class PersistentMessage {
public:
	PersistentMessage() noexcept
	    : m_message_(NULL)
	{
	}

	PersistentMessage(PersistentMessage &&other) noexcept
	    : m_message_(other.m_message_)
	{
		other.m_message_ = NULL;
	}

	PersistentMessage &operator=(PersistentMessage &&other) noexcept
	{
		std::swap(m_message_, other.m_message_);
		return *this;
	}

	PersistentMessage(const PersistentMessage &) = delete;
	PersistentMessage &operator=(const PersistentMessage &) = delete;

	~PersistentMessage() noexcept
	{
		hx_os_persistent_message_destroy(&m_message_);
	}

	const char *id() const noexcept
	{
		return hx_os_persistent_message_id(m_message_);
	}

	hx_os_bytes_view data() const noexcept
	{
		return hx_os_persistent_message_data(m_message_);
	}

	const hx_os_persistent_message *native() const noexcept
	{
		return m_message_;
	}

private:
	friend class PersistentQueue;
	hx_os_persistent_message *m_message_;
};

/* Owns a synchronous database handle. Exclude concurrent calls before close
 * or destruction; no worker is started and returned messages own their bytes.
 * Native SQLite connections created by the application are separate owners.
 */
class PersistentQueue {
public:
	PersistentQueue() noexcept
	    : m_queue_(NULL)
	{
	}

	PersistentQueue(PersistentQueue &&other) noexcept
	    : m_queue_(other.m_queue_)
	{
		other.m_queue_ = NULL;
	}

	PersistentQueue &operator=(PersistentQueue &&other) noexcept
	{
		std::swap(m_queue_, other.m_queue_);
		return *this;
	}

	PersistentQueue(const PersistentQueue &) = delete;
	PersistentQueue &operator=(const PersistentQueue &) = delete;

	~PersistentQueue() noexcept
	{
		(void)hx_os_persistent_queue_close(&m_queue_, NULL);
	}

	static Result<PersistentQueue> open(const char *path,
		const hx_os_persistent_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		PersistentQueue result;
		hx_os_error error = {};
		auto status = hx_os_persistent_queue_open(
			context, path, options, &result.m_queue_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	Result<void> push(
		const char *id, const void *data, size_t size) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_persistent_queue_push(
			m_queue_, id, hx_os_bytes_view{data, size}, &error);

		return detail::Status(status, error);
	}

	Result<void> push(const char *id, StringView data) noexcept
	{
		return push(id, data.data(), data.size());
	}

	Result<void> pushMany(
		const hx_os_persistent_item *items, size_t count) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_persistent_queue_push_many(
			m_queue_, items, count, &error);

		return detail::Status(status, error);
	}

	Result<PersistentMessage> take(
		hx_os_deadline deadline = HX_OS_DEADLINE_NOW,
		hx_os_cancel_token *cancel = NULL) noexcept
	{
		PersistentMessage message;
		hx_os_error error = {};
		auto status = hx_os_persistent_queue_take_until(m_queue_,
			deadline, cancel, &message.m_message_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return message;
	}

	Result<void> ack(const PersistentMessage &message) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_persistent_queue_ack(
			m_queue_, message.native(), &error);

		return detail::Status(status, error);
	}

	Result<void> ackMany(const hx_os_persistent_message *const *messages,
		size_t count) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_persistent_queue_ack_many(
			m_queue_, messages, count, &error);

		return detail::Status(status, error);
	}

	Result<void> release(const PersistentMessage &message) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_persistent_queue_release(
			m_queue_, message.native(), &error);

		return detail::Status(status, error);
	}

	Result<void> renew(
		const PersistentMessage &message, uint32_t leaseMs) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_persistent_queue_renew(
			m_queue_, message.native(), leaseMs, &error);

		return detail::Status(status, error);
	}

	Result<hx_os_persistent_stats> stats() noexcept
	{
		hx_os_persistent_stats result = {};
		hx_os_error error = {};
		auto status =
			hx_os_persistent_queue_stats(m_queue_, &result, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_persistent_queue_close(&m_queue_, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_persistent_queue *m_queue_;
};
} // namespace hx
#endif
