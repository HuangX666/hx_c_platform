#include "coroutine_private.h"

#if HX_OS_ENABLE_COROUTINE_IO
#include "loop_private.h"

#if defined(_MSC_VER)
static __declspec(thread) int hx_os_coro_io_active;
#else
static __thread int hx_os_coro_io_active;
#endif

int hx_os_i_coro_io_inside(void)
{
	return hx_os_coro_io_active;
}

typedef struct hx_os_coro_io_job hx_os_coro_io_job;
struct hx_os_coro_stream {
	hx_os_atomic references;
	hx_os_coro_runtime *runtime;
	hx_os_coro_stream *next;
	hx_os_socket *socket;
	hx_os_pipe *pipe;
};

struct hx_os_coro_io_job {
	hx_os_coro_io_job *next;
	hx_os_coro *coroutine;
	hx_os_coro_stream *stream;
	hx_os_coro_stream *output;
	hx_os_coro_stream *second;
	hx_os_request *request;
	hx_os_address_list *addresses;
	hx_os_operation operation;
	hx_os_completion completion;
	int kind;
	int backlog;
	int reuse;
	int cancelled;
};

struct hx_os_coro_io_state {
	hx_os_coro_runtime *runtime;
	hx_os_network *network;
	hx_os_loop *loop;
	hx_os_thread *thread;
	hx_os_coro_stream *streams;
	hx_os_coro_io_job *inbox;
	hx_os_coro_io_job *last;
	hx_os_coro_io_job *active;
	hx_os_coro_io_job *done;
	hx_os_deadline close_deadline;
	uint64_t close_sequence;
	hx_os_error result;
	size_t handles;
	int initialized;
	int closing;
	int close_done;
	int stopped;
};

// The scheduler mutex also excludes native loop destruction.
static void hx_os_coro_io_wake(hx_os_coro_io_state *state)
{
	if (state->loop)
		hx_os_i_loop_wake(state->loop);

	hx_os_condition_notify_all(state->runtime->condition);
}

void hx_os_coro_stream_retain(hx_os_coro_stream *stream)
{
	if (stream)
		(void)hx_os_atomic_fetch_add(
			&stream->references, 1, HX_OS_RELAXED);
}

void hx_os_coro_stream_release(hx_os_coro_stream *stream)
{
	hx_os_coro_runtime *runtime;
	int64_t references;

	if (!stream)
		return;

	runtime = stream->runtime;
	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	references =
		hx_os_atomic_fetch_sub(&stream->references, 1, HX_OS_ACQ_REL);
	if (references == 1) {
		--runtime->io->handles;
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);

		hx_os_memory_free(stream);
		hx_os_i_coro_runtime_release(runtime);
	}
	else if (references == 2) {
		hx_os_coro_io_wake(runtime->io);
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	}
	else
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);
}

static hx_os_coro_stream *hx_os_coro_stream_new(
	hx_os_coro_io_state *state, hx_os_error *error)
{
	hx_os_coro_runtime *runtime = state->runtime;
	hx_os_coro_stream *stream;

	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	if (state->handles == runtime->options.max_io_handles) {
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);
		(void)hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		return NULL;
	}

	++state->handles;
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	stream = hx_os_i_zero(runtime->context, sizeof(*stream));
	if (!stream) {
		(void)hx_os_mutex_lock(runtime->mutex, NULL);
		--state->handles;
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);
		(void)hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

		return NULL;
	}

	stream->runtime = runtime;
	hx_os_i_coro_runtime_retain(runtime);
	// One dispatcher lease remains until native close.
	hx_os_atomic_store(&stream->references, 2, HX_OS_RELAXED);
	stream->next = state->streams;
	state->streams = stream;

	return stream;
}

static hx_os_status hx_os_coro_stream_sweep(
	hx_os_coro_io_state *state, int all, hx_os_error *error)
{
	hx_os_coro_stream **cursor = &state->streams;

	while (*cursor) {
		hx_os_coro_stream *stream = *cursor;
		hx_os_status status;

		if (!all &&
			hx_os_atomic_load(&stream->references, HX_OS_ACQUIRE) !=
				1) {
			cursor = &stream->next;
			continue;
		}

		status = stream->socket
			? hx_os_socket_close(&stream->socket, error)
			: HX_OS_OK;
		if (status == HX_OS_OK && stream->pipe)
			status = hx_os_pipe_close_until(
				&stream->pipe, HX_OS_DEADLINE_NOW, error);

		if (status != HX_OS_OK)
			return status;

		*cursor = stream->next;

		hx_os_coro_stream_release(stream);
	}

	return HX_OS_OK;
}

static void hx_os_coro_io_ready(hx_os_coro_io_job *job)
{
	hx_os_coro *coroutine = job->coroutine;
	hx_os_coro_runtime *runtime = coroutine->runtime;

	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	if (coroutine->wait_result.status != HX_OS_OK) {
		job->completion.error.status = coroutine->wait_result.status;
		if (coroutine->wait_result.domain != HX_OS_ERROR_NONE)
			job->completion.error = coroutine->wait_result;
	}

	hx_os_i_coro_finish_wait(coroutine, &job->completion.error);
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	// Publication transfers job back to its worker.
}

static void hx_os_coro_io_completed(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	hx_os_coro_io_job *job = user;
	hx_os_coro_io_state *state = job->coroutine->runtime->io;
	hx_os_coro_io_job **cursor = &state->active;

	while (*cursor != job)
		cursor = &(*cursor)->next;

	*cursor = job->next;
	job->completion = *completion;
	if (completion->error.status == HX_OS_OK) {
		if (job->kind == HX_OS_OP_ACCEPT) {
			job->output = hx_os_coro_stream_new(
				state, &job->completion.error);
			if (job->output)
				job->output->socket =
					hx_os_request_take_socket(request);
		}
		else if (job->kind == HX_OS_OP_RESOLVE)
			job->addresses = hx_os_request_take_addresses(request);
	}

	// Defer publication until the native callback has returned.
	job->next = state->done;
	state->done = job;
}

static hx_os_status hx_os_coro_io_open(
	hx_os_coro_io_state *state, hx_os_coro_io_job *job)
{
	hx_os_error *error = &job->completion.error;
	hx_os_status status;
	hx_os_socket **socket;

	job->output = hx_os_coro_stream_new(state, error);
	if (!job->output)
		return error->status;

	if (job->kind == HX_OS_CO_PIPE) {
		job->second = hx_os_coro_stream_new(state, error);

		return job->second
			? hx_os_pipe_pair_async(state->runtime->context,
				  &job->output->pipe, &job->second->pipe, error)
			: error->status;
	}

	socket = &job->output->socket;

	status = hx_os_socket_create(state->network,
		job->operation.address.family, HX_OS_TCP, socket, error);
	job->operation.socket = *socket;
	if (job->kind != HX_OS_CO_LISTEN)
		return status;

	if (status == HX_OS_OK && job->reuse)
		status = hx_os_socket_set_reuseaddr(*socket, 1, error);

	if (status == HX_OS_OK)
		status = hx_os_socket_bind(
			*socket, &job->operation.address, error);

	if (status == HX_OS_OK)
		status = hx_os_socket_listen(*socket, job->backlog, error);

	if (status == HX_OS_OK)
		status = hx_os_socket_local_address(
			*socket, &job->completion.peer, error);

	return status;
}

static void hx_os_coro_io_start(
	hx_os_coro_io_state *state, hx_os_coro_io_job *job)
{
	hx_os_error *error = &job->completion.error;
	hx_os_status status = HX_OS_OK;
	int cancelled;

	(void)hx_os_mutex_lock(state->runtime->mutex, NULL);
	cancelled = job->cancelled;
	(void)hx_os_mutex_unlock(state->runtime->mutex, NULL);
	if (cancelled) {
		(void)hx_os_i_error(error, HX_OS_CANCELLED, 0, 0);
		hx_os_coro_io_ready(job);

		return;
	}

	if (job->kind >= HX_OS_CO_DIAL) {
		status = hx_os_coro_io_open(state, job);
		if (status != HX_OS_OK || job->kind != HX_OS_CO_DIAL) {
			error->status = status;
			hx_os_coro_io_ready(job);

			return;
		}
	}

	if (job->stream) {
		job->operation.socket = job->stream->socket;
		if (!job->stream->socket && !job->stream->pipe) {
			(void)hx_os_i_error(error, HX_OS_CLOSED, 0, 0);
			hx_os_coro_io_ready(job);

			return;
		}
	}

	if (job->stream && job->stream->pipe) {
		hx_os_pipe_operation operation;

		if (job->kind != HX_OS_OP_READ && job->kind != HX_OS_OP_WRITE) {
			(void)hx_os_i_error(error, HX_OS_INVALID, 0, 0);
			hx_os_coro_io_ready(job);

			return;
		}

		(void)hx_os_pipe_operation_init(&operation, sizeof(operation),
			job->kind == HX_OS_OP_READ ? HX_OS_PIPE_READ
						   : HX_OS_PIPE_WRITE);
		operation.pipe = job->stream->pipe;
		operation.data = job->operation.data;
		operation.bytes = job->operation.bytes;
		operation.deadline = job->operation.deadline;
		operation.send_mode = HX_OS_SEND_BORROW;

		status = hx_os_loop_submit_pipe(state->loop, &operation,
			hx_os_coro_io_completed, job, &job->request, error);
	}
	else
		status = hx_os_loop_submit(state->loop, &job->operation,
			hx_os_coro_io_completed, job, &job->request, error);

	if (status != HX_OS_OK) {
		hx_os_coro_io_ready(job);

		return;
	}

	job->next = state->active;
	state->active = job;
}

static hx_os_status hx_os_coro_io_native_close(
	hx_os_coro_io_state *state, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status = HX_OS_OK;
	hx_os_loop *loop = state->loop;

	if (loop)
		status = hx_os_loop_close_until(loop, deadline, error);

	if (status == HX_OS_OK)
		status = hx_os_coro_stream_sweep(state, 1, error);

	if (status == HX_OS_OK && loop) {
		(void)hx_os_mutex_lock(state->runtime->mutex, NULL);
		state->loop = NULL;
		(void)hx_os_mutex_unlock(state->runtime->mutex, NULL);

		status = hx_os_loop_destroy(&loop, error);
		if (status != HX_OS_OK) {
			(void)hx_os_mutex_lock(state->runtime->mutex, NULL);
			state->loop = loop;
			(void)hx_os_mutex_unlock(state->runtime->mutex, NULL);
		}
	}

	if (status == HX_OS_OK && state->network)
		status = hx_os_network_destroy(&state->network, error);

	return status;
}

static void *hx_os_coro_io_main(void *user)
{
	hx_os_coro_io_state *state = user;
	hx_os_coro_runtime *runtime = state->runtime;
	hx_os_loop_options options;
	hx_os_status status;
	hx_os_error error;

	hx_os_coro_io_active = 1;
	hx_os_loop_options_init(&options);
	options.max_requests = runtime->options.max_coroutines;
	options.max_pending_bytes = runtime->options.max_io_bytes;

	status =
		hx_os_network_create(runtime->context, &state->network, &error);
	if (status == HX_OS_OK)
		status = hx_os_loop_create(
			state->network, &options, &state->loop, &error);

	if (status != HX_OS_OK)
		(void)hx_os_coro_io_native_close(
			state, HX_OS_DEADLINE_INFINITE, NULL);

	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	state->result = error;
	state->initialized = 1;
	state->stopped = status != HX_OS_OK;
	hx_os_condition_notify_all(runtime->condition);
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	if (status != HX_OS_OK) {
		hx_os_coro_io_active = 0;

		return NULL;
	}

	for (;;) {
		hx_os_coro_io_job *jobs, *job;
		hx_os_deadline close_deadline;
		uint64_t close_sequence;
		int closing;

		(void)hx_os_mutex_lock(runtime->mutex, NULL);
		jobs = state->inbox;
		state->inbox = state->last = NULL;
		closing = state->closing && !state->close_done;
		close_deadline = state->close_deadline;
		close_sequence = state->close_sequence;

		for (job = state->active; job; job = job->next)
			if (job->cancelled)
				hx_os_request_cancel(job->request);

		(void)hx_os_mutex_unlock(runtime->mutex, NULL);

		while (jobs) {
			job = jobs;
			jobs = jobs->next;
			hx_os_coro_io_start(state, job);
		}

		(void)hx_os_coro_stream_sweep(state, 0, NULL);
		if (closing) {
			status = hx_os_coro_io_native_close(
				state, close_deadline, &error);
			(void)hx_os_mutex_lock(runtime->mutex, NULL);
			state->result = error;
			state->result.status = status;
			state->close_done = status == HX_OS_OK ||
				state->close_sequence == close_sequence;
			state->stopped = status == HX_OS_OK;
			hx_os_condition_notify_all(runtime->condition);
			if (state->stopped) {
				(void)hx_os_mutex_unlock(runtime->mutex, NULL);
				break;
			}

			while (state->close_done)
				(void)hx_os_condition_wait_until(
					runtime->condition, runtime->mutex,
					HX_OS_DEADLINE_INFINITE, NULL);

			(void)hx_os_mutex_unlock(runtime->mutex, NULL);
			continue;
		}

		status = hx_os_loop_poll_until(
			state->loop, HX_OS_DEADLINE_INFINITE, &error);

		while (state->done) {
			job = state->done;
			state->done = job->next;

			hx_os_request_release(job->request);
			job->request = NULL;
			hx_os_coro_io_ready(job);
		}

		if (status != HX_OS_OK && status != HX_OS_TIMEOUT) {
			// OS failure does not permit releasing pending native
			// buffers.
			(void)hx_os_mutex_lock(runtime->mutex, NULL);

			for (job = state->active; job; job = job->next) {
				job->coroutine->wait_result = error;
				job->cancelled = 1;
			}

			(void)hx_os_mutex_unlock(runtime->mutex, NULL);
			(void)hx_os_sleep_until(
				hx_os_deadline_after_ms(1), NULL);
		}
	}

	hx_os_coro_io_active = 0;

	return NULL;
}

hx_os_status hx_os_i_coro_io_create(
	hx_os_coro_runtime *runtime, hx_os_error *error)
{
	hx_os_coro_io_state *state;
	hx_os_status status;

	if (!runtime->options.max_io_handles)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	state = hx_os_i_zero(runtime->context, sizeof(*state));
	if (!state)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	runtime->io = state;
	state->runtime = runtime;

	status = hx_os_thread_create(runtime->context, hx_os_coro_io_main,
		state, &state->thread, error);
	if (status != HX_OS_OK)
		return status;

	(void)hx_os_mutex_lock(runtime->mutex, NULL);

	while (!state->initialized)
		(void)hx_os_condition_wait_until(runtime->condition,
			runtime->mutex, HX_OS_DEADLINE_INFINITE, NULL);

	status = state->result.status;
	if (error)
		*error = state->result;

	(void)hx_os_mutex_unlock(runtime->mutex, NULL);

	return status;
}

hx_os_status hx_os_i_coro_io_close(hx_os_coro_runtime *runtime,
	hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_coro_io_state *state = runtime->io;
	hx_os_status status = HX_OS_OK;

	if (!state || !state->thread)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	if (!state->stopped) {
		state->closing = 1;
		++state->close_sequence;
		state->close_done = 0;
		state->close_deadline = deadline;
		hx_os_coro_io_wake(state);

		while (!state->close_done) {
			status = hx_os_condition_wait_until(runtime->condition,
				runtime->mutex, deadline, error);
			if (status != HX_OS_OK)
				break;
		}

		if (state->close_done) {
			status = state->result.status;
			if (error)
				*error = state->result;
		}
	}

	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	if (status == HX_OS_OK)
		status = hx_os_thread_join_until(
			state->thread, deadline, NULL, error);

	if (status == HX_OS_OK)
		status = hx_os_thread_destroy(&state->thread, error);

	return status;
}

void hx_os_i_coro_io_destroy(hx_os_coro_runtime *runtime)
{
	hx_os_memory_free(runtime->io);
}

void hx_os_i_coro_io_cancel(hx_os_coro *coroutine)
{
	hx_os_coro_io_job *job = coroutine->io;

	if (job) {
		job->cancelled = 1;
		hx_os_coro_io_wake(coroutine->runtime->io);
	}
}

void hx_os_i_coro_io_dispose_wait(hx_os_coro *coroutine)
{
	hx_os_coro_io_job *job = coroutine->io;

	coroutine->io = NULL;
	if (!job)
		return;

	hx_os_coro_stream_release(job->stream);
	hx_os_coro_stream_release(job->output);
	hx_os_coro_stream_release(job->second);
	hx_os_address_list_destroy(job->addresses);
	hx_os_memory_free(job);
}

hx_os_status hx_os_i_coro_io_call(hx_os_coro_stream *stream, int kind,
	const hx_os_operation *input, int backlog, int reuse,
	hx_os_coro_stream **output, hx_os_coro_stream **second,
	hx_os_address_list **addresses, hx_os_completion *completion,
	hx_os_error *error)
{
	hx_os_coro *coroutine = hx_os_coro_current();

	hx_os_coro_runtime *runtime;
	hx_os_coro_io_job *job;
	hx_os_status status;

	if (!coroutine)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	runtime = coroutine->runtime;
	if (!runtime->io)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	if (hx_os_i_coro_wait_ready(coroutine, HX_OS_CO_WAIT_IO)) {
		job = coroutine->io;
		if (job->kind != kind || job->stream != stream)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

		goto completed;
	}

	if (coroutine->preparing || coroutine->wait_kind != HX_OS_CO_WAIT_NONE)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	if (stream && stream->runtime != runtime)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	job = hx_os_i_zero(runtime->context, sizeof(*job));
	if (!job)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	job->operation = *input;
	job->operation.send_mode = HX_OS_SEND_BORROW;
	job->kind = kind;
	job->backlog = backlog;
	job->reuse = reuse;
	job->stream = stream;
	job->coroutine = coroutine;
	hx_os_coro_stream_retain(stream);
	(void)hx_os_mutex_lock(runtime->mutex, NULL);

	status = hx_os_i_coro_begin_wait(
		coroutine, HX_OS_CO_WAIT_IO, input->deadline, error);
	if (status == HX_OS_OK) {
		coroutine->io = job;
		if (runtime->io->last)
			runtime->io->last->next = job;
		else
			runtime->io->inbox = job;

		runtime->io->last = job;
		hx_os_coro_io_wake(runtime->io);
	}

	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	if (status != HX_OS_OK) {
		hx_os_coro_stream_release(stream);
		hx_os_memory_free(job);

		return status;
	}

	status = hx_os_i_coro_suspend(coroutine, error);
	if (status != HX_OS_OK)
		return status;

completed:
	if (completion)
		*completion = job->completion;

	if (job->completion.error.status == HX_OS_OK) {
		if (output) {
			*output = job->output;
			job->output = NULL;
		}

		if (second) {
			*second = job->second;
			job->second = NULL;
		}

		if (addresses) {
			*addresses = job->addresses;
			job->addresses = NULL;
		}
	}

	return hx_os_i_coro_consume(coroutine, error);
}
#endif

#if !HX_OS_ENABLE_COROUTINE_IO
int hx_os_i_coro_io_inside(void)
{
	return 0;
}

void hx_os_coro_stream_retain(hx_os_coro_stream *stream)
{
	(void)stream;
}

void hx_os_coro_stream_release(hx_os_coro_stream *stream)
{
	(void)stream;
}

hx_os_status hx_os_i_coro_io_call(hx_os_coro_stream *stream, int kind,
	const hx_os_operation *input, int backlog, int reuse,
	hx_os_coro_stream **output, hx_os_coro_stream **second,
	hx_os_address_list **addresses, hx_os_completion *completion,
	hx_os_error *error)
{
	(void)stream;
	(void)kind;
	(void)input;
	(void)backlog;
	(void)reuse;
	(void)output;
	(void)second;
	(void)addresses;
	(void)completion;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_i_coro_io_create(
	hx_os_coro_runtime *runtime, hx_os_error *error)
{
	(void)runtime;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_coro_io_close(hx_os_coro_runtime *runtime,
	hx_os_deadline deadline, hx_os_error *error)
{
	(void)runtime;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_i_coro_io_destroy(hx_os_coro_runtime *runtime)
{
	(void)runtime;
}

void hx_os_i_coro_io_cancel(hx_os_coro *coroutine)
{
	(void)coroutine;
}

void hx_os_i_coro_io_dispose_wait(hx_os_coro *coroutine)
{
	(void)coroutine;
}
#endif
