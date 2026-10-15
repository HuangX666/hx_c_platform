#include "coroutine_private.h"
#include "hx_os_coro_http.h"

#if HX_OS_ENABLE_COROUTINES && HX_OS_ENABLE_THREAD_POOL && HX_OS_HAVE_HTTP
typedef struct hx_coro_http_job hx_coro_http_job;

struct hx_os_coro_http {
	hx_os_coro_runtime *runtime;
	hx_os_thread_pool *pool;
	hx_coro_http_job *jobs;
	size_t count, bytes, maximum, capacity;
	size_t preparing;
	int closing;
};

struct hx_coro_http_job {
	hx_os_atomic references, cancelled;
	hx_coro_http_job *next;
	hx_os_coro_http *service;
	hx_os_coro *coroutine;
	hx_os_http_options options;
	hx_os_http_response response;
	hx_os_http_response *output;
	hx_os_error error;
	const char *method, *url, *headers[64];
	const unsigned char *body;
	size_t body_bytes, offset, header_count, budget;
	unsigned char storage[];
};

static void hx_coro_http_release(hx_coro_http_job *job)
{
	hx_os_coro_runtime *runtime;
	hx_os_coro_http *service;
	hx_coro_http_job **cursor;

	if (hx_os_atomic_fetch_sub(&job->references, 1, HX_OS_ACQ_REL) != 1)
		return;
	service = job->service;
	runtime = service->runtime;
	hx_os_http_response_destroy(&job->response);
	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	cursor = &service->jobs;
	while (*cursor && *cursor != job)
		cursor = &(*cursor)->next;
	if (*cursor)
		*cursor = job->next;
	--service->count;
	service->bytes -= job->budget;
	hx_os_condition_notify_all(runtime->condition);
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	hx_os_coro_release(job->coroutine);
	hx_os_memory_free(job);
}

void hx_os_i_coro_http_dispose(hx_os_coro *coroutine)
{
	hx_coro_http_job *job = coroutine->http_wait;

	coroutine->http_wait = NULL;
	if (job) {
		hx_os_atomic_store(&job->cancelled, 1, HX_OS_RELEASE);
		hx_coro_http_release(job);
	}
}

void hx_os_i_coro_http_cancel(hx_os_coro *coroutine)
{
	hx_coro_http_job *job = coroutine->http_wait;

	if (job)
		hx_os_atomic_store(&job->cancelled, 1, HX_OS_RELEASE);
}

static hx_os_status HX_OS_CALL hx_coro_http_progress(
	void *user, uint64_t downloaded, uint64_t uploaded, hx_os_error *error)
{
	hx_coro_http_job *job = user;

	(void)downloaded;
	(void)uploaded;
	return hx_os_i_error(error,
		hx_os_atomic_load(&job->cancelled, HX_OS_ACQUIRE)
			? HX_OS_CANCELLED
			: HX_OS_OK,
		0, 0);
}

static hx_os_status HX_OS_CALL hx_coro_http_read(void *user, void *data,
	size_t capacity, size_t *completed, hx_os_error *error)
{
	hx_coro_http_job *job = user;
	size_t remaining = job->body_bytes - job->offset;

	*completed = remaining < capacity ? remaining : capacity;
	if (*completed)
		memcpy(data, job->body + job->offset, *completed);
	job->offset += *completed;
	return hx_coro_http_progress(job, 0, 0, error);
}

static hx_os_status HX_OS_CALL hx_coro_http_run(hx_os_task *task, void *user)
{
	hx_coro_http_job *job = user;
	hx_os_http *http = NULL;
	hx_os_http_stream_options options = HX_OS_HTTP_STREAM_OPTIONS_INIT;
	hx_os_status status;

	(void)task;
	options.request = job->options;
	options.progress = hx_coro_http_progress;
	options.user = job;
	if (job->body_bytes) {
		options.upload_bytes = job->body_bytes;
		options.read = hx_coro_http_read;
	}
	status = hx_coro_http_progress(job, 0, 0, &job->error);
	if (status == HX_OS_OK)
		status = hx_os_http_create(
			job->service->runtime->context, &http, &job->error);
	if (status == HX_OS_OK)
		status = hx_os_http_request_stream(http, job->method, job->url,
			job->headers, job->header_count, &options,
			&job->response, NULL, &job->error);
	(void)hx_os_http_close(&http, NULL);
	job->error.status = status;
	return status;
}

static void HX_OS_CALL hx_coro_http_done(void *user)
{
	hx_coro_http_job *job = user;
	hx_os_coro *coroutine = job->coroutine;

	(void)hx_os_mutex_lock(coroutine->runtime->mutex, NULL);
	if (coroutine->wait_kind == HX_OS_CO_WAIT_HTTP &&
		coroutine->http_wait == job)
		hx_os_i_coro_finish_wait(coroutine, &job->error);
	(void)hx_os_mutex_unlock(coroutine->runtime->mutex, NULL);
	hx_coro_http_release(job);
}

static const char *hx_coro_http_copy(unsigned char **cursor, const char *value)
{
	char *copy = (char *)*cursor;
	size_t bytes;

	if (!value)
		return NULL;
	bytes = strlen(value) + 1;
	memcpy(copy, value, bytes);
	*cursor += bytes;
	return copy;
}
#else
void hx_os_i_coro_http_dispose(hx_os_coro *coroutine)
{
	(void)coroutine;
}

void hx_os_i_coro_http_cancel(hx_os_coro *coroutine)
{
	(void)coroutine;
}
#endif

hx_os_status hx_os_coro_http_create(hx_os_coro_runtime *runtime, size_t threads,
	size_t max_requests, size_t max_pending_bytes, hx_os_coro_http **out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_COROUTINES && HX_OS_ENABLE_THREAD_POOL && HX_OS_HAVE_HTTP
	hx_os_coro_http *service;
	hx_os_thread_pool_options options;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!runtime || threads > 64 || max_requests > 1024)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	service = hx_os_i_zero(runtime->context, sizeof(*service));
	if (!service)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	service->runtime = runtime;
	service->capacity =
		max_pending_bytes ? max_pending_bytes : 64 * 1024 * 1024;
	service->maximum = max_requests ? max_requests : 16;
	hx_os_thread_pool_options_init(&options);
	options.threads = threads ? threads : 2;
	options.queue_capacity = service->maximum;
	status = hx_os_thread_pool_create(
		runtime->context, &options, &service->pool, error);
	if (status == HX_OS_OK) {
		hx_os_i_coro_runtime_retain(runtime);
		*out = service;
	}
	else
		hx_os_memory_free(service);
	return status;
#else
	(void)runtime;
	(void)threads;
	(void)max_requests;
	(void)max_pending_bytes;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_coro_http_request(hx_os_coro_http *service,
	const char *method, const char *url, hx_os_bytes_view body,
	const char *const *headers, size_t header_count,
	const hx_os_http_options *input, hx_os_http_response *out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_COROUTINES && HX_OS_ENABLE_THREAD_POOL && HX_OS_HAVE_HTTP
	const hx_os_http_options defaults = HX_OS_HTTP_OPTIONS_INIT;
	const hx_os_http_options *options = input ? input : &defaults;
	hx_os_coro *coroutine = hx_os_coro_current();
	hx_coro_http_job *job;
	hx_os_task *task = NULL;
	hx_os_status status;
	size_t bytes, budget, i;
	unsigned char *cursor;

	if (!service || !coroutine || service->runtime != coroutine->runtime ||
		!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (hx_os_i_coro_wait_ready(coroutine, HX_OS_CO_WAIT_HTTP)) {
		job = coroutine->http_wait;
		if (job->service != service || job->output != out)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		goto completed;
	}
	if (coroutine->wait_kind != HX_OS_CO_WAIT_NONE)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	if (!method || !url || strlen(method) > 32 || strlen(url) > 16384 ||
		(!body.data && body.size) || body.size > HX_BYTES_MAX_SIZE ||
		header_count > 64 || (header_count && !headers) ||
		options->size < sizeof(*options) || options->version != 1 ||
		options->cancel ||
		options->max_body_bytes > HX_BYTES_MAX_SIZE ||
		options->max_header_bytes > 1024 * 1024 ||
		(options->ca_file && strlen(options->ca_file) > 16384) ||
		(options->proxy_url && strlen(options->proxy_url) > 16384))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	bytes = body.size + strlen(method) + strlen(url) + 2;
	if (options->ca_file)
		bytes += strlen(options->ca_file) + 1;
	if (options->proxy_url)
		bytes += strlen(options->proxy_url) + 1;
	for (i = 0; i < header_count; ++i) {
		if (!headers[i] || strlen(headers[i]) > 8192)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		bytes += strlen(headers[i]) + 1;
	}
	budget = sizeof(*job) + bytes + options->max_body_bytes +
		options->max_header_bytes + 16385;
	(void)hx_os_mutex_lock(service->runtime->mutex, NULL);
	status = service->closing ? HX_OS_CLOSED
		: service->count == service->maximum ||
			budget > service->capacity - service->bytes
		? HX_OS_LIMIT
		: HX_OS_OK;
	if (status == HX_OS_OK) {
		++service->count;
		++service->preparing;
		service->bytes += budget;
	}
	(void)hx_os_mutex_unlock(service->runtime->mutex, NULL);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	job = hx_os_i_zero(service->runtime->context, sizeof(*job) + bytes);
	if (job) {
		job->service = service;
		job->coroutine = coroutine;
		job->budget = budget;
		job->output = out;
		job->options = *options;
		job->options.deadline =
			hx_os_deadline_after_ms(options->timeout_ms);
		if (options->deadline < job->options.deadline)
			job->options.deadline = options->deadline;
		job->error.status = HX_OS_CANCELLED;
		job->body_bytes = body.size;
		job->header_count = header_count;
		cursor = job->storage;
		job->method = hx_coro_http_copy(&cursor, method);
		job->url = hx_coro_http_copy(&cursor, url);
		job->options.ca_file =
			hx_coro_http_copy(&cursor, options->ca_file);
		job->options.proxy_url =
			hx_coro_http_copy(&cursor, options->proxy_url);
		for (i = 0; i < header_count; ++i)
			job->headers[i] =
				hx_coro_http_copy(&cursor, headers[i]);
		job->body = cursor;
		if (body.size)
			memcpy(cursor, body.data, body.size);
		hx_os_atomic_store(&job->references, 2, HX_OS_RELAXED);
		hx_os_atomic_store(&job->cancelled, 0, HX_OS_RELAXED);
		hx_os_http_response_init(
			&job->response, service->runtime->context);
		hx_os_coro_retain(coroutine);
	}
	(void)hx_os_mutex_lock(service->runtime->mutex, NULL);
	status = !job ? HX_OS_NO_MEMORY
		: service->closing
		? HX_OS_CLOSED
		: hx_os_i_coro_begin_wait(coroutine, HX_OS_CO_WAIT_HTTP,
			  job->options.deadline, error);
	if (job) {
		job->next = service->jobs;
		service->jobs = job;
	}
	if (status == HX_OS_OK)
		coroutine->http_wait = job;
	if (!job) {
		--service->count;
		service->bytes -= budget;
	}
	(void)hx_os_mutex_unlock(service->runtime->mutex, NULL);
	if (status == HX_OS_OK)
		status = hx_os_thread_pool_submit(service->pool,
			hx_coro_http_run, job, hx_coro_http_done, &task, error);
	(void)hx_os_mutex_lock(service->runtime->mutex, NULL);
	--service->preparing;
	hx_os_condition_notify_all(service->runtime->condition);
	(void)hx_os_mutex_unlock(service->runtime->mutex, NULL);
	if (status != HX_OS_OK) {
		if (job) {
			if (error)
				job->error = *error;
			job->error.status = status;
			hx_coro_http_done(job);
			if (coroutine->http_wait == job)
				return hx_os_i_coro_consume(coroutine, error);
			hx_coro_http_release(job);
		}
		return hx_os_i_error(error, status, 0, 0);
	}
	hx_os_task_release(task);
	status = hx_os_i_coro_suspend(coroutine, error);
	if (status != HX_OS_OK)
		return status;
completed:
	job = coroutine->http_wait;
	if (coroutine->wait_result.status == HX_OS_OK) {
		hx_os_http_response_destroy(out);
		*out = job->response;
		memset(&job->response, 0, sizeof(job->response));
	}
	return hx_os_i_coro_consume(coroutine, error);
#else
	(void)service;
	(void)method;
	(void)url;
	(void)body;
	(void)headers;
	(void)header_count;
	(void)input;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_coro_http_get(hx_os_coro_http *http, const char *url,
	const hx_os_http_options *options, hx_os_http_response *out,
	hx_os_error *error)
{
	return hx_os_coro_http_request(http, "GET", url,
		(hx_os_bytes_view){NULL, 0}, NULL, 0, options, out, error);
}

hx_os_status hx_os_coro_http_close_until(
	hx_os_coro_http **owner, hx_os_deadline deadline, hx_os_error *error)
{
#if HX_OS_ENABLE_COROUTINES && HX_OS_ENABLE_THREAD_POOL && HX_OS_HAVE_HTTP
	hx_os_coro_http *service;
	hx_os_coro_runtime *runtime;
	hx_coro_http_job *job;
	hx_os_status status = HX_OS_OK;

	if (!owner)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	service = *owner;
	if (!service)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (hx_os_coro_current())
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	runtime = service->runtime;
	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	service->closing = 1;
	for (job = service->jobs; job; job = job->next)
		hx_os_atomic_store(&job->cancelled, 1, HX_OS_RELEASE);
	while (service->preparing && status == HX_OS_OK)
		status = hx_os_condition_wait_until(
			runtime->condition, runtime->mutex, deadline, error);
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_thread_pool_close_until(
		service->pool, HX_OS_POOL_CANCEL_PENDING, deadline, error);
	if (status != HX_OS_OK)
		return status;
	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	while (service->count && status == HX_OS_OK)
		status = hx_os_condition_wait_until(
			runtime->condition, runtime->mutex, deadline, error);
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_thread_pool_destroy(&service->pool, error);
	if (status == HX_OS_OK) {
		*owner = NULL;
		hx_os_memory_free(service);
		hx_os_i_coro_runtime_release(runtime);
	}
	return status;
#else
	(void)owner;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
