#include "process_private.h"
#include "hx_os_pipe.h"

typedef struct hx_os_capture_stream {
	hx_os_pipe *pipe;
	hx_os_request *request;
	unsigned char *data;
	unsigned char *scratch;
	size_t bytes;
	size_t capacity;
	size_t limit;
	hx_os_error error;
	int completed;
	int ended;
	int discard;
	int read_discard;
} hx_os_capture_stream;

struct hx_os_process_capture {
	hx_os_context *context;
	hx_os_process *process;
	hx_os_loop *loop;
	uint64_t owner;
	hx_os_capture_stream streams[2];
};
#define HX_OS_I_CAPTURE_CHUNK 16384u

hx_os_status hx_os_process_capture_options_init(
	hx_os_process_capture_options *options, size_t capacity)
{
	hx_os_process_capture_options value =
		HX_OS_PROCESS_CAPTURE_OPTIONS_INIT;

	if (!options || capacity < sizeof(value))
		return HX_OS_INVALID;

	*options = value;

	return HX_OS_OK;
}
#if HX_OS_ENABLE_PROCESS && HX_OS_HAVE_NATIVE_PROCESS && HX_OS_ENABLE_PIPE &&  \
	HX_OS_ENABLE_LOOP
static void HX_OS_CALL hx_os_capture_read(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	hx_os_capture_stream *stream = user;

	(void)request;
	if (!stream->read_discard)
		stream->bytes += completion->bytes;

	stream->error = completion->error;
	stream->completed = 1;
	if (completion->error.status == HX_OS_EOF) {
		stream->ended = 1;
		hx_os_error_clear(&stream->error);
	}
}

static hx_os_status hx_os_capture_submit(hx_os_process_capture *capture,
	hx_os_capture_stream *stream, hx_os_error *error)
{
	void *buffer;
	size_t bytes;

	if (stream->completed) {
		hx_os_request_release(stream->request);
		stream->request = NULL;
		stream->completed = 0;
	}

	if (stream->request || stream->ended)
		return HX_OS_OK;

	if (stream->error.status != HX_OS_OK) {
		if (!stream->discard) {
			if (error)
				*error = stream->error;

			return stream->error.status;
		}

		/* Explicit abandonment of a failed stream, after retirement. */
		hx_os_status status = hx_os_pipe_close_until(
			&stream->pipe, HX_OS_DEADLINE_NOW, error);
		if (status != HX_OS_OK)
			return status;

		stream->ended = 1;
		hx_os_error_clear(&stream->error);

		return HX_OS_OK;
	}

	if (stream->discard) {
		if (!stream->scratch)
			stream->scratch = hx_os_i_alloc(
				capture->context, HX_OS_I_CAPTURE_CHUNK, 0);

		if (!stream->scratch)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

		buffer = stream->scratch;
		bytes = HX_OS_I_CAPTURE_CHUNK;
	}
	else {
		if (stream->bytes >= stream->limit)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		if (stream->bytes == stream->capacity) {
			size_t capacity =
				stream->capacity ? stream->capacity : 4096;
			unsigned char *replacement;

			if (capacity <= SIZE_MAX / 2)
				capacity *= 2;

			if (capacity > stream->limit)
				capacity = stream->limit;

			if (capacity <= stream->capacity)
				return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

			replacement =
				hx_os_i_alloc(capture->context, capacity, 0);
			if (!replacement)
				return hx_os_i_error(
					error, HX_OS_NO_MEMORY, 0, 0);

			if (stream->bytes)
				memcpy(replacement, stream->data,
					stream->bytes);

			hx_os_memory_free(stream->data);
			stream->data = replacement;
			stream->capacity = capacity;
		}

		buffer = stream->data + stream->bytes;
		bytes = stream->capacity - stream->bytes;
		if (bytes > HX_OS_I_CAPTURE_CHUNK)
			bytes = HX_OS_I_CAPTURE_CHUNK;
	}

	stream->read_discard = stream->discard;

	return hx_os_pipe_read_async(capture->loop, stream->pipe, buffer, bytes,
		HX_OS_DEADLINE_INFINITE, hx_os_capture_read, stream,
		&stream->request, error);
}

static hx_os_status hx_os_capture_release(hx_os_process_capture *capture,
	hx_os_deadline deadline, hx_os_error *error)
{
	int i;
	hx_os_status status;

	for (i = 0; i < 2; ++i) {
		hx_os_capture_stream *stream = &capture->streams[i];

		status = hx_os_pipe_close_until(&stream->pipe, deadline, error);
		if (status != HX_OS_OK)
			return status;

		if (stream->request) {
			hx_os_request_release(stream->request);
			stream->request = NULL;
		}
	}

	if (capture->loop) {
		status = hx_os_loop_close_until(capture->loop, deadline, error);
		if (status != HX_OS_OK)
			return status;

		status = hx_os_loop_destroy(&capture->loop, error);
		if (status != HX_OS_OK)
			return status;
	}

	if (capture->process) {
		status = hx_os_process_destroy(&capture->process, error);
		if (status != HX_OS_OK)
			return status;
	}

	for (i = 0; i < 2; ++i) {
		hx_os_memory_free(capture->streams[i].data);
		hx_os_memory_free(capture->streams[i].scratch);
	}

	hx_os_memory_free(capture);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
#endif
hx_os_status hx_os_process_capture_run_with_options(hx_os_context *context,
	const char *path, const char *const arguments[],
	const char *const environment[],
	const hx_os_process_capture_options *options, hx_os_deadline deadline,
	hx_os_process_capture **out, hx_os_error *error)
{
#if HX_OS_ENABLE_PROCESS && HX_OS_HAVE_NATIVE_PROCESS && HX_OS_ENABLE_PIPE &&  \
	HX_OS_ENABLE_LOOP
	hx_os_process_capture_options defaults =
		HX_OS_PROCESS_CAPTURE_OPTIONS_INIT;
	hx_os_process_options spawn = HX_OS_PROCESS_OPTIONS_INIT;
	hx_os_process_capture *capture;
	hx_os_loop_options loop_options;
	hx_os_file *input_writer = NULL;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (!options)
		options = &defaults;

	if (options->size < sizeof(*options) || options->version != 1 ||
		!options->stdout_limit || !options->stderr_limit)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	capture = hx_os_i_zero(context, sizeof(*capture));
	if (!capture)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	capture->context = context;
	capture->owner = hx_os_thread_id();
	capture->streams[0].limit = options->stdout_limit;
	capture->streams[1].limit = options->stderr_limit;
	hx_os_loop_options_init(&loop_options);
	loop_options.max_requests = 2;
	loop_options.max_pending_bytes = 2 * HX_OS_I_CAPTURE_CHUNK;

	status = hx_os_loop_create_local(
		context, &loop_options, &capture->loop, error);
	if (status == HX_OS_OK)
		status = hx_os_pipe_pair_anonymous(
			context, &spawn.standard_input, &input_writer, error);

	if (status == HX_OS_OK)
		status = hx_os_file_close(&input_writer, error);

	if (status == HX_OS_OK)
		status = hx_os_pipe_pair_child(context, 1,
			&capture->streams[0].pipe, &spawn.standard_output,
			error);

	if (status == HX_OS_OK)
		status = hx_os_pipe_pair_child(context, 1,
			&capture->streams[1].pipe, &spawn.standard_error,
			error);

	/* Allocate and accept both reads before accepting a child. */
	if (status == HX_OS_OK)
		status = hx_os_capture_submit(
			capture, &capture->streams[0], error);

	if (status == HX_OS_OK)
		status = hx_os_capture_submit(
			capture, &capture->streams[1], error);

	if (status == HX_OS_OK)
		status = hx_os_process_spawn(context, path, arguments,
			environment, &spawn, &capture->process, error);

	(void)hx_os_file_close(&input_writer, NULL);
	(void)hx_os_file_close(&spawn.standard_input, NULL);
	(void)hx_os_file_close(&spawn.standard_output, NULL);
	(void)hx_os_file_close(&spawn.standard_error, NULL);
	if (status != HX_OS_OK) {
		(void)hx_os_capture_release(
			capture, HX_OS_DEADLINE_INFINITE, NULL);

		return status;
	}

	*out = capture;

	return hx_os_process_capture_wait_until(capture, deadline, NULL, error);
#else
	(void)context;
	(void)path;
	(void)arguments;
	(void)environment;
	(void)options;
	(void)deadline;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_process_capture_run(hx_os_context *context, const char *path,
	const char *const arguments[], const char *const environment[],
	hx_os_deadline deadline, hx_os_process_capture **out,
	hx_os_error *error)
{
	return hx_os_process_capture_run_with_options(context, path, arguments,
		environment, NULL, deadline, out, error);
}

hx_os_status hx_os_process_capture_wait_until(hx_os_process_capture *capture,
	hx_os_deadline deadline, hx_os_process_result *result,
	hx_os_error *error)
{
#if HX_OS_ENABLE_PROCESS && HX_OS_HAVE_NATIVE_PROCESS && HX_OS_ENABLE_PIPE &&  \
	HX_OS_ENABLE_LOOP
	hx_os_error local;
	hx_os_status status, first;
	int polled = 0, i;

	if (!error)
		error = &local;

	if (!capture || capture->owner != hx_os_thread_id())
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	for (;;) {
		hx_os_process_result process_result;
		hx_os_error first_error;
		uint64_t now;
		hx_os_deadline wake;

		first = HX_OS_OK;

		for (i = 0; i < 2; ++i) {
			status = hx_os_capture_submit(
				capture, &capture->streams[i], error);
			if (status != HX_OS_OK && first == HX_OS_OK) {
				first = status;
				first_error = *error;
			}
		}

		status = hx_os_process_wait_until(capture->process,
			HX_OS_DEADLINE_NOW, &process_result, error);
		if (status != HX_OS_OK && status != HX_OS_TIMEOUT)
			return status;

		if (status == HX_OS_OK && capture->streams[0].ended &&
			capture->streams[1].ended) {
			if (result)
				*result = process_result;

			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		}

		if (first != HX_OS_OK) {
			*error = first_error;

			return first;
		}

		status = hx_os_monotonic_read(&now, error);
		if (status != HX_OS_OK)
			return status;

		if (polled && deadline != HX_OS_DEADLINE_INFINITE &&
			now >= deadline)
			return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
		wake = hx_os_deadline_after_ms(5);
		if (wake > deadline)
			wake = deadline;

		if (hx_os_loop_pending(capture->loop))
			status = hx_os_loop_poll_until(
				capture->loop, wake, error);
		else
			status = hx_os_sleep_until(wake, error);

		if (status != HX_OS_OK && status != HX_OS_TIMEOUT)
			return status;

		polled = 1;
	}
#else
	(void)capture;
	(void)deadline;
	(void)result;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_process_capture_view(hx_os_process_capture *capture,
	int stream, const void **data, size_t *bytes)
{
	if (!capture || capture->owner != hx_os_thread_id() || stream < 0 ||
		stream > 1 || !data || !bytes)
		return HX_OS_INVALID;
	*data = capture->streams[stream].data;
	*bytes = capture->streams[stream].bytes;

	return HX_OS_OK;
}

hx_os_status hx_os_process_capture_text_encoded(hx_os_process_capture *capture,
	int stream, hx_os_encoding encoding, hx_string *out, hx_os_error *error)
{
	const void *data;
	size_t bytes;
	hx_os_status status =
		hx_os_process_capture_view(capture, stream, &data, &bytes);

	if (status != HX_OS_OK || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_string_assign_encoded(out, encoding, data, bytes, error);
}

hx_os_status hx_os_process_capture_text(hx_os_process_capture *capture,
	int stream, hx_string *out, hx_os_error *error)
{
	return hx_os_process_capture_text_encoded(
		capture, stream, HX_OS_UTF8, out, error);
}

hx_os_status hx_os_process_capture_raise_limit(
	hx_os_process_capture *capture, int stream, size_t limit)
{
	if (!capture || capture->owner != hx_os_thread_id() || stream < 0 ||
		stream > 1 || limit < capture->streams[stream].limit)
		return HX_OS_INVALID;
	capture->streams[stream].limit = limit;

	return HX_OS_OK;
}

hx_os_status hx_os_process_capture_discard(
	hx_os_process_capture *capture, int stream)
{
	if (!capture || capture->owner != hx_os_thread_id() || stream < 0 ||
		stream > 1)
		return HX_OS_INVALID;
	/* An accepted read still commits its retained prefix. Subsequent
	 * reads use scratch storage. No buffer moves while native I/O runs.
	 */
	capture->streams[stream].discard = 1;

	return HX_OS_OK;
}

hx_os_status hx_os_process_capture_close_until(hx_os_process_capture **capture,
	hx_os_deadline deadline, hx_os_error *error)
{
#if HX_OS_ENABLE_PROCESS && HX_OS_HAVE_NATIVE_PROCESS && HX_OS_ENABLE_PIPE &&  \
	HX_OS_ENABLE_LOOP
	hx_os_status status;

	if (!capture)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!*capture)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	status = hx_os_process_capture_wait_until(
		*capture, deadline, NULL, error);
	if (status != HX_OS_OK)
		return status;

	status = hx_os_capture_release(*capture, deadline, error);
	if (status == HX_OS_OK)
		*capture = NULL;

	return status;
#else
	(void)capture;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
