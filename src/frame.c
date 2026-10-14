#include "internal.h"
#include "hx_os_frame.h"
#if HX_OS_ENABLE_NETWORK
#include "net_private.h"
#endif

struct hx_os_frame {
	hx_os_frame_callbacks callbacks;
	void *user, *backend, *write_backend;
	hx_os_mutex *read_mutex, *write_mutex;
	hx_os_atomic read_owner, write_owner;
	hx_bytes received, sending;
	size_t maximum, header_read, content_read, sent;
	uint64_t peer;
	unsigned char read_header[4], write_header[4];
	int read_closed, write_closed, pending;
};

int hx_os_frame_available(void)
{
	return HX_OS_ENABLE_FRAME;
}

static hx_os_status hx_os_frame_enter(
	hx_os_frame *frame, int writing, hx_os_error *error)
{
	uint64_t thread = hx_os_thread_id();
	hx_os_status status;

	if (!HX_OS_ENABLE_FRAME)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!frame || !thread)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if ((uint64_t)hx_os_atomic_load(&frame->read_owner, HX_OS_ACQUIRE) ==
			thread ||
		(uint64_t)hx_os_atomic_load(
			&frame->write_owner, HX_OS_ACQUIRE) == thread)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	status = hx_os_mutex_lock(
		writing ? frame->write_mutex : frame->read_mutex, error);
	if (status == HX_OS_OK)
		hx_os_atomic_store(
			writing ? &frame->write_owner : &frame->read_owner,
			(int64_t)thread, HX_OS_RELEASE);
	return status;
}

static void hx_os_frame_leave(hx_os_frame *frame, int writing)
{
	hx_os_atomic_store(writing ? &frame->write_owner : &frame->read_owner,
		0, HX_OS_RELEASE);
	(void)hx_os_mutex_unlock(
		writing ? frame->write_mutex : frame->read_mutex, NULL);
}

hx_os_status hx_os_frame_create(hx_os_context *context,
	const hx_os_frame_callbacks *callbacks, void *user, size_t maximum,
	hx_os_frame **out, hx_os_error *error)
{
	hx_os_frame *frame;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!HX_OS_ENABLE_FRAME)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!callbacks || (!callbacks->read && !callbacks->write))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!maximum)
		maximum = 1024 * 1024;
	if (maximum > HX_BYTES_MAX_SIZE)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	frame = hx_os_i_alloc(context, sizeof(*frame), 8);
	if (!frame)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	memset(frame, 0, sizeof(*frame));
	frame->callbacks = *callbacks;
	frame->user = user;
	frame->maximum = maximum;
	hx_bytes_init(&frame->received, context);
	hx_bytes_init(&frame->sending, context);
	hx_os_atomic_store(&frame->read_owner, 0, HX_OS_RELAXED);
	hx_os_atomic_store(&frame->write_owner, 0, HX_OS_RELAXED);
	status = hx_os_mutex_create(context, 0, &frame->read_mutex, error);
	if (status == HX_OS_OK)
		status = hx_os_mutex_create(
			context, 0, &frame->write_mutex, error);
	if (status != HX_OS_OK) {
		hx_os_mutex_destroy(frame->read_mutex);
		hx_bytes_destroy(&frame->received);
		hx_bytes_destroy(&frame->sending);
		hx_os_memory_free(frame);
		return status;
	}
	*out = frame;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_frame_stream_read(void *user, void *data,
	size_t bytes, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error)
{
	hx_os_frame *frame = user;

	(void)deadline;
	return hx_os_stream_read_some(
		frame->backend, data, bytes, completed, error);
}

static hx_os_status hx_os_frame_stream_write(void *user, const void *data,
	size_t bytes, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error)
{
	hx_os_frame *frame = user;

	(void)deadline;
	return hx_os_stream_write_some(
		frame->write_backend, data, bytes, completed, error);
}

static hx_os_status hx_os_frame_socket_read(void *user, void *data,
	size_t bytes, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error)
{
	hx_os_frame *frame = user;

	return hx_os_socket_receive_until(
		frame->backend, data, bytes, deadline, completed, error);
}

static hx_os_status hx_os_frame_socket_write(void *user, const void *data,
	size_t bytes, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error)
{
	hx_os_frame *frame = user;

	return hx_os_socket_send_until(
		frame->backend, data, bytes, deadline, completed, error);
}

static hx_os_status hx_os_frame_tls_read(void *user, void *data, size_t bytes,
	hx_os_deadline deadline, size_t *completed, hx_os_error *error)
{
	hx_os_frame *frame = user;

	return hx_os_tls_receive_until(frame->backend, frame->peer, data, bytes,
		deadline, completed, error);
}

static hx_os_status hx_os_frame_tls_write(void *user, const void *data,
	size_t bytes, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error)
{
	hx_os_frame *frame = user;

	return hx_os_tls_send_until(frame->backend, frame->peer, data, bytes,
		deadline, completed, error);
}

static hx_os_status hx_os_frame_tls_flush(
	void *user, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_frame *frame = user;

	return hx_os_tls_flush_until(
		frame->backend, frame->peer, deadline, error);
}

static hx_os_status hx_os_frame_adapter(hx_os_context *context, void *backend,
	uint64_t peer, size_t maximum, const hx_os_frame_callbacks *callbacks,
	hx_os_frame **out, hx_os_error *error)
{
	hx_os_status status;

	if (!backend || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_frame_create(
		context, callbacks, NULL, maximum, out, error);
	if (status == HX_OS_OK) {
		(*out)->backend = backend;
		(*out)->write_backend = backend;
		(*out)->peer = peer;
		(*out)->user = *out;
	}
	return status;
}

hx_os_status hx_os_frame_from_streams(hx_os_context *context,
	hx_os_stream *reader, hx_os_stream *writer, size_t maximum,
	hx_os_frame **out, hx_os_error *error)
{
	const hx_os_frame_callbacks callbacks = {
		reader ? hx_os_frame_stream_read : NULL,
		writer ? hx_os_frame_stream_write : NULL, NULL};
	hx_os_status status;

	if (out)
		*out = NULL;
	if (!HX_OS_ENABLE_STREAM)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	status = hx_os_frame_create(
		context, &callbacks, NULL, maximum, out, error);
	if (status == HX_OS_OK) {
		(*out)->backend = reader;
		(*out)->write_backend = writer;
		(*out)->user = *out;
	}
	return status;
}

hx_os_status hx_os_frame_from_stream(hx_os_context *context,
	hx_os_stream *stream, size_t maximum, hx_os_frame **out,
	hx_os_error *error)
{
	return hx_os_frame_from_streams(
		context, stream, stream, maximum, out, error);
}

hx_os_status hx_os_frame_from_socket(hx_os_context *context,
	hx_os_socket *socket, size_t maximum, hx_os_frame **out,
	hx_os_error *error)
{
	const hx_os_frame_callbacks callbacks = {
		hx_os_frame_socket_read, hx_os_frame_socket_write, NULL};

#if HX_OS_ENABLE_NETWORK
	if (!socket || !hx_os_i_socket_stream(socket) || socket->loop) {
		if (out)
			*out = NULL;
		return hx_os_i_error(error,
			socket && socket->loop ? HX_OS_BUSY : HX_OS_INVALID, 0,
			0);
	}
#else
	(void)context;
	(void)socket;
	(void)maximum;
	(void)callbacks;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
	return hx_os_frame_adapter(
		context, socket, 0, maximum, &callbacks, out, error);
}

hx_os_status hx_os_frame_from_tls(hx_os_context *context, hx_os_tls *tls,
	uint64_t peer, size_t maximum, hx_os_frame **out, hx_os_error *error)
{
	const hx_os_frame_callbacks callbacks = {hx_os_frame_tls_read,
		hx_os_frame_tls_write, hx_os_frame_tls_flush};

	if (!HX_OS_HAVE_TLS) {
		if (out)
			*out = NULL;
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	}
	return hx_os_frame_adapter(
		context, tls, peer, maximum, &callbacks, out, error);
}

static hx_os_status hx_os_frame_flush(
	hx_os_frame *frame, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status;

	if (frame->write_closed)
		return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);
	if (!frame->callbacks.write)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!frame->pending)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	while (frame->sent < frame->sending.size + 4) {
		const unsigned char *data;
		size_t bytes, completed = 0;

		if (frame->sent < 4) {
			data = frame->write_header + frame->sent;
			bytes = 4 - frame->sent;
		}
		else {
			data = frame->sending.data + frame->sent - 4;
			bytes = frame->sending.size + 4 - frame->sent;
		}
		status = frame->callbacks.write(
			frame->user, data, bytes, deadline, &completed, error);
		if (completed > bytes)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		frame->sent += completed;
		if (status != HX_OS_OK)
			return status;
		if (!completed)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
	}
	if (frame->callbacks.flush) {
		status = frame->callbacks.flush(frame->user, deadline, error);
		if (status != HX_OS_OK)
			return status;
	}
	frame->pending = 0;
	frame->sent = 0;
	frame->sending.size = 0;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_frame_flush_until(
	hx_os_frame *frame, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status = hx_os_frame_enter(frame, 1, error);

	if (status != HX_OS_OK)
		return status;
	status = hx_os_frame_flush(frame, deadline, error);
	hx_os_frame_leave(frame, 1);
	if (error)
		error->status = status;
	return status;
}

hx_os_status hx_os_frame_send_until(hx_os_frame *frame,
	hx_os_bytes_view message, hx_os_deadline deadline, int *accepted,
	hx_os_error *error)
{
	hx_os_status status;
	unsigned i;

	if (accepted)
		*accepted = 0;
	if (!frame || (!message.data && message.size) || !accepted)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!frame->callbacks.write)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (message.size > frame->maximum)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	status = hx_os_frame_enter(frame, 1, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_frame_flush(frame, deadline, error);
	if (status == HX_OS_OK)
		status = hx_bytes_assign(&frame->sending, message, error);
	if (status == HX_OS_OK) {
		for (i = 0; i < 4; ++i)
			frame->write_header[i] =
				(unsigned char)((uint32_t)message.size >>
					((3 - i) * 8));
		frame->pending = 1;
		frame->sent = 0;
		*accepted = 1;
		status = hx_os_frame_flush(frame, deadline, error);
	}
	hx_os_frame_leave(frame, 1);
	if (error)
		error->status = status;
	return status;
}

hx_os_status hx_os_frame_send_text_until(hx_os_frame *frame,
	hx_string_view message, hx_os_deadline deadline, int *accepted,
	hx_os_error *error)
{
	size_t bad;
	hx_os_status status =
		hx_os_text_validate_utf8(message.data, message.size, &bad);

	if (accepted)
		*accepted = 0;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	return hx_os_frame_send_until(frame,
		(hx_os_bytes_view){message.data, message.size}, deadline,
		accepted, error);
}

static hx_os_status hx_os_frame_pull(hx_os_frame *frame, unsigned char *data,
	size_t bytes, size_t *done, hx_os_deadline deadline, hx_os_error *error)
{
	while (*done < bytes) {
		size_t completed = 0, wanted = bytes - *done;
		hx_os_status status;

		if (frame->read_closed)
			return hx_os_i_error(
				error, HX_OS_TRUNCATED_INPUT, 0, 0);
		status = frame->callbacks.read(frame->user, data + *done,
			wanted, deadline, &completed, error);
		if (completed > wanted)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		*done += completed;
		if (status == HX_OS_EOF) {
			frame->read_closed = 1;
			if (*done == bytes)
				return HX_OS_OK;
			return hx_os_i_error(
				error, HX_OS_TRUNCATED_INPUT, 0, 0);
		}
		if (status != HX_OS_OK)
			return status;
		if (!completed)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
	}
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_frame_ready(
	hx_os_frame *frame, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status;
	uint32_t length = 0;
	unsigned i;

	if (!frame->callbacks.read)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (frame->read_closed == 2)
		return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);
	if (frame->read_closed && !frame->header_read)
		return hx_os_i_error(error, HX_OS_EOF, 0, 0);
	status = hx_os_frame_pull(frame, frame->read_header, 4,
		&frame->header_read, deadline, error);
	if (status != HX_OS_OK) {
		if (frame->read_closed && !frame->header_read)
			return hx_os_i_error(error, HX_OS_EOF, 0, 0);
		return status;
	}
	for (i = 0; i < 4; ++i)
		length = (length << 8) | frame->read_header[i];
	if (length > frame->maximum) {
		frame->read_closed = 2;
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}
	status = hx_bytes_resize(&frame->received, length, error);
	if (status != HX_OS_OK)
		return status;
	return hx_os_frame_pull(frame, frame->received.data, length,
		&frame->content_read, deadline, error);
}

static hx_os_status hx_os_frame_receive(hx_os_frame *frame,
	hx_os_deadline deadline, void *out, int text, hx_os_error *error)
{
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_frame_enter(frame, 0, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_frame_ready(frame, deadline, error);
	if (status == HX_OS_OK) {
		if (text)
			status = hx_string_assign(out,
				(hx_string_view){
					(const char *)frame->received.data,
					frame->received.size},
				error);
		else
			status = hx_bytes_assign(
				out, hx_bytes_view(&frame->received), error);
		if (status == HX_OS_OK) {
			frame->header_read = 0;
			frame->content_read = 0;
			frame->received.size = 0;
		}
	}
	hx_os_frame_leave(frame, 0);
	if (error)
		error->status = status;
	return status;
}

hx_os_status hx_os_frame_receive_until(hx_os_frame *frame,
	hx_os_deadline deadline, hx_bytes *out, hx_os_error *error)
{
	return hx_os_frame_receive(frame, deadline, out, 0, error);
}

hx_os_status hx_os_frame_receive_text_until(hx_os_frame *frame,
	hx_os_deadline deadline, hx_string *out, hx_os_error *error)
{
	return hx_os_frame_receive(frame, deadline, out, 1, error);
}

hx_os_status hx_os_frame_abort(hx_os_frame *frame, hx_os_error *error)
{
	if (!frame)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (hx_os_atomic_load(&frame->read_owner, HX_OS_ACQUIRE) ||
		hx_os_atomic_load(&frame->write_owner, HX_OS_ACQUIRE))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	frame->read_closed = 2;
	frame->write_closed = 1;
	frame->pending = 0;
	frame->header_read = 0;
	frame->content_read = 0;
	hx_bytes_destroy(&frame->received);
	hx_bytes_destroy(&frame->sending);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_frame_destroy(hx_os_frame **frame, hx_os_error *error)
{
	hx_os_frame *value;

	if (!frame)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	value = *frame;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (hx_os_atomic_load(&value->read_owner, HX_OS_ACQUIRE) ||
		hx_os_atomic_load(&value->write_owner, HX_OS_ACQUIRE) ||
		value->pending || value->header_read)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	*frame = NULL;
	hx_os_mutex_destroy(value->read_mutex);
	hx_os_mutex_destroy(value->write_mutex);
	hx_bytes_destroy(&value->received);
	hx_bytes_destroy(&value->sending);
	hx_os_memory_free(value);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
