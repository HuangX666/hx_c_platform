#include "coroutine_private.h"
#include "containers_private.h"

#if defined(_MSC_VER)
static __declspec(thread) int hx_os_coro_channel_callback;
#else
static __thread int hx_os_coro_channel_callback;
#endif

struct hx_os_coro_waiter {
	hx_os_coro_channel *channel;
	hx_os_coro *coroutine;
	hx_os_coro_waiter *previous;
	hx_os_coro_waiter *next;
	void *payload;
	void *output;
	size_t index;
	int operation;
	int linked;
	int constructed;
	int native_done;
	hx_os_status native_status;
};

struct hx_os_coro_channel {
	hx_os_atomic references;
	hx_os_coro_runtime *runtime;
	hx_os_coro_channel *previous;
	hx_os_coro_channel *next;
	hx_os_container_options values;
	hx_os_coro_waiter *send_first;
	hx_os_coro_waiter *send_last;
	hx_os_coro_waiter *receive_first;
	hx_os_coro_waiter *receive_last;
	unsigned char *buffer;
	size_t capacity;
	size_t head;
	size_t count;
	int closed;
};

void *hx_os_i_coro_sync_alloc(hx_os_coro_runtime *runtime, size_t bytes,
	size_t alignment, hx_os_error *error)
{
	size_t cost = hx_os_i_allocation_cost(bytes, alignment);

	void *memory;

	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	if (!cost ||
		cost > runtime->options.max_channel_bytes -
				runtime->channel_bytes) {
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);
		(void)hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		return NULL;
	}

	runtime->channel_bytes += cost;
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	memory = hx_os_i_alloc(runtime->context, bytes, alignment);
	if (!memory) {
		(void)hx_os_mutex_lock(runtime->mutex, NULL);
		runtime->channel_bytes -= cost;
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);
		(void)hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}

	return memory;
}

void hx_os_i_coro_sync_free(hx_os_coro_runtime *runtime, void *memory)
{
	size_t bytes;

	if (!memory)
		return;

	bytes = hx_os_i_allocation_bytes(memory);

	hx_os_memory_free(memory);
	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	runtime->channel_bytes -= bytes;
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
}

typedef struct hx_os_coro_value_header {
	hx_os_coro_runtime *runtime;
	void *allocation;
} hx_os_coro_value_header;

hx_os_status hx_os_coro_value_alloc(hx_os_coro_channel *channel, size_t bytes,
	size_t alignment, void **out, hx_os_error *error)
{
	hx_os_coro_value_header *header;
	size_t padding = sizeof(*header), total;
	void *allocation;
	hx_os_error detail;

	if (out)
		*out = NULL;

	if (!channel || !out || !hx_os_i_alignment_valid(alignment))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (alignment > padding)
		padding = alignment;

	if (!hx_os_i_add_size(padding, bytes, &total))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	hx_os_error_clear(&detail);
	allocation = hx_os_i_coro_sync_alloc(
		channel->runtime, total, alignment, &detail);
	if (!allocation) {
		if (error)
			*error = detail;

		return detail.status;
	}

	*out = (unsigned char *)allocation + padding;
	header = (hx_os_coro_value_header *)*out - 1;
	header->allocation = allocation;
	header->runtime = channel->runtime;
	hx_os_i_coro_runtime_retain(channel->runtime);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_coro_value_free(void *memory)
{
	hx_os_coro_value_header *header;
	hx_os_coro_runtime *runtime;

	if (!memory)
		return;

	header = (hx_os_coro_value_header *)memory - 1;
	runtime = header->runtime;

	hx_os_i_coro_sync_free(runtime, header->allocation);
	hx_os_i_coro_runtime_release(runtime);
}

hx_os_status hx_os_coro_channel_create(hx_os_coro_runtime *runtime,
	const hx_os_container_options *values, size_t capacity,
	hx_os_coro_channel **out, hx_os_error *error)
{
	hx_os_coro_channel *channel;
	hx_os_container_base validation;
	hx_os_status status;
	size_t bytes, cost;
	hx_os_error detail;

	if (out)
		*out = NULL;

	if (!runtime || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_i_container_init(
		&validation, runtime->context, values, sizeof(*channel), 0);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	if (capacity > values->max_items ||
		capacity > SIZE_MAX / values->element_size)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	bytes = capacity * values->element_size;
	cost = bytes ? hx_os_i_allocation_cost(bytes, values->element_alignment)
		     : 0;
	if (cost > values->max_bytes - validation.memory)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	(void)hx_os_mutex_lock(runtime->mutex, NULL);

	status = runtime->closing ? HX_OS_CLOSED
		: runtime->channel_count == runtime->options.max_channels
		? HX_OS_LIMIT
		: HX_OS_OK;
	if (status == HX_OS_OK) {
		++runtime->channel_count;
		hx_os_i_coro_runtime_retain(runtime);
	}

	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	hx_os_error_clear(&detail);
	channel =
		hx_os_i_coro_sync_alloc(runtime, sizeof(*channel), 0, &detail);
	if (channel) {
		memset(channel, 0, sizeof(*channel));
		channel->runtime = runtime;
		channel->values = *values;
		channel->capacity = capacity;
		if (bytes)
			channel->buffer = hx_os_i_coro_sync_alloc(runtime,
				bytes, values->element_alignment, &detail);
	}

	if (!channel || (bytes && !channel->buffer)) {
		status = detail.status;
		goto fail;
	}

	hx_os_atomic_store(&channel->references, 1, HX_OS_RELAXED);
	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	if (runtime->closing)
		status = HX_OS_CLOSED;
	else {
		channel->next = runtime->channels;
		if (runtime->channels)
			runtime->channels->previous = channel;

		runtime->channels = channel;
	}

	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	if (status != HX_OS_OK)
		goto fail;

	*out = channel;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);

fail:
	if (channel)
		hx_os_i_coro_sync_free(runtime, channel->buffer);

	hx_os_i_coro_sync_free(runtime, channel);
	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	--runtime->channel_count;
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);

	hx_os_i_coro_runtime_release(runtime);

	return hx_os_i_error(error, status, 0, 0);
}

void hx_os_coro_channel_retain(hx_os_coro_channel *channel)
{
	if (channel)
		(void)hx_os_atomic_fetch_add(
			&channel->references, 1, HX_OS_RELAXED);
}

void hx_os_coro_channel_release(hx_os_coro_channel *channel)
{
	hx_os_coro_runtime *runtime;
	hx_os_coro *current;
	size_t i;

	if (!channel ||
		hx_os_atomic_fetch_sub(
			&channel->references, 1, HX_OS_ACQ_REL) != 1)
		return;
	runtime = channel->runtime;
	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	if (channel->previous)
		channel->previous->next = channel->next;
	else
		runtime->channels = channel->next;

	if (channel->next)
		channel->next->previous = channel->previous;

	--runtime->channel_count;
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	current = hx_os_coro_current();
	if (current)
		++current->preparing;

	++hx_os_coro_channel_callback;
	if (channel->values.drop)
		for (i = 0; i < channel->count; ++i) {
			size_t index = (channel->head + i) % channel->capacity;

			channel->values.drop(channel->buffer +
					index * channel->values.element_size,
				channel->values.user);
		}

	if (current)
		--current->preparing;

	--hx_os_coro_channel_callback;

	hx_os_i_coro_sync_free(runtime, channel->buffer);
	hx_os_i_coro_sync_free(runtime, channel);
	hx_os_i_coro_runtime_release(runtime);
}

static void hx_os_coro_channel_detach(hx_os_coro_waiter *waiter)
{
	hx_os_coro_channel *channel = waiter->channel;
	hx_os_coro_waiter **first = waiter->operation == HX_OS_CORO_SEND
		? &channel->send_first
		: &channel->receive_first;
	hx_os_coro_waiter **last = waiter->operation == HX_OS_CORO_SEND
		? &channel->send_last
		: &channel->receive_last;

	if (!waiter->linked)
		return;

	if (waiter->previous)
		waiter->previous->next = waiter->next;
	else
		*first = waiter->next;

	if (waiter->next)
		waiter->next->previous = waiter->previous;
	else
		*last = waiter->previous;

	waiter->linked = 0;
}

void hx_os_i_coro_channel_unlink(hx_os_coro *coroutine)
{
	size_t i;

	for (i = 0; i < coroutine->case_count; ++i)
		hx_os_coro_channel_detach(&coroutine->cases[i]);
}

void hx_os_i_coro_channel_dispose_wait(hx_os_coro *coroutine)
{
	hx_os_coro_waiter *cases = coroutine->cases;
	size_t i, count = coroutine->case_count;

	coroutine->cases = NULL;
	coroutine->case_count = 0;
	++coroutine->preparing;

	for (i = 0; i < count; ++i) {
		hx_os_coro_channel *channel = cases[i].channel;

		if (cases[i].constructed && channel->values.drop)
			channel->values.drop(
				cases[i].payload, channel->values.user);

		hx_os_i_coro_sync_free(coroutine->runtime, cases[i].payload);
		hx_os_coro_channel_release(channel);
	}

	hx_os_i_coro_sync_free(coroutine->runtime, cases);
	--coroutine->preparing;
}

static void hx_os_coro_channel_complete(
	hx_os_coro_waiter *waiter, hx_os_status status)
{
	hx_os_error error;

	if (!waiter->coroutine) {
		hx_os_coro_channel_detach(waiter);
		waiter->native_done = 1;
		waiter->native_status = status;
		hx_os_condition_notify_all(waiter->channel->runtime->condition);

		return;
	}

	(void)hx_os_i_error(&error, status, 0, 0);
	waiter->coroutine->selected = waiter->index;
	hx_os_i_coro_finish_wait(waiter->coroutine, &error);
}

static hx_os_coro_waiter *hx_os_coro_channel_peer(
	hx_os_coro_waiter *first, hx_os_coro *self)
{
	while (first && self && first->coroutine == self)
		first = first->next;

	return first;
}

static int hx_os_coro_channel_attempt(hx_os_coro_waiter *waiter)
{
	hx_os_coro_channel *channel = waiter->channel;
	hx_os_coro_waiter *peer;
	size_t bytes = channel->values.element_size;

	if (waiter->operation == HX_OS_CORO_SEND) {
		if (channel->closed) {
			hx_os_coro_channel_complete(waiter, HX_OS_CLOSED);

			return 1;
		}

		peer = hx_os_coro_channel_peer(
			channel->receive_first, waiter->coroutine);
		if (peer) {
			memcpy(peer->output, waiter->payload, bytes);
			waiter->constructed = 0;
			hx_os_coro_channel_complete(peer, HX_OS_OK);
			hx_os_coro_channel_complete(waiter, HX_OS_OK);

			return 1;
		}

		if (channel->count < channel->capacity) {
			size_t index = (channel->head + channel->count) %
				channel->capacity;

			memcpy(channel->buffer + index * bytes, waiter->payload,
				bytes);
			waiter->constructed = 0;
			++channel->count;
			hx_os_coro_channel_complete(waiter, HX_OS_OK);

			return 1;
		}
	}
	else {
		if (channel->count) {
			memcpy(waiter->output,
				channel->buffer + channel->head * bytes, bytes);
			channel->head = (channel->head + 1) % channel->capacity;
			--channel->count;
			hx_os_coro_channel_complete(waiter, HX_OS_OK);
			peer = channel->send_first;
			if (peer && !channel->closed) {
				size_t index =
					(channel->head + channel->count) %
					channel->capacity;

				memcpy(channel->buffer + index * bytes,
					peer->payload, bytes);
				peer->constructed = 0;
				++channel->count;
				hx_os_coro_channel_complete(peer, HX_OS_OK);
			}

			return 1;
		}

		peer = hx_os_coro_channel_peer(
			channel->send_first, waiter->coroutine);
		if (peer && !channel->closed) {
			memcpy(waiter->output, peer->payload, bytes);
			peer->constructed = 0;
			hx_os_coro_channel_complete(peer, HX_OS_OK);
			hx_os_coro_channel_complete(waiter, HX_OS_OK);

			return 1;
		}

		if (channel->closed) {
			hx_os_coro_channel_complete(waiter, HX_OS_EOF);

			return 1;
		}
	}

	return 0;
}

hx_os_status hx_os_coro_channel_close(
	hx_os_coro_channel *channel, hx_os_error *error)
{
	hx_os_coro *current = hx_os_coro_current();
	if (!channel)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if ((current && current->preparing) || hx_os_coro_channel_callback)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	(void)hx_os_mutex_lock(channel->runtime->mutex, NULL);
	channel->closed = 1;

	while (channel->send_first)
		hx_os_coro_channel_complete(channel->send_first, HX_OS_CLOSED);

	while (channel->receive_first)
		(void)hx_os_coro_channel_attempt(channel->receive_first);

	(void)hx_os_mutex_unlock(channel->runtime->mutex, NULL);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_coro_channel_prepare(hx_os_coro *coroutine,
	const hx_os_coro_case *input, size_t count, hx_os_error *error)
{
	size_t i;

	hx_os_coro_waiter *cases = hx_os_i_coro_sync_alloc(
		coroutine->runtime, count * sizeof(*cases), 0, error);
	if (!cases)
		return error->status;

	memset(cases, 0, count * sizeof(*cases));
	coroutine->cases = cases;
	coroutine->case_count = count;

	for (i = 0; i < count; ++i) {
		hx_os_coro_channel *channel = input[i].channel;

		cases[i].channel = channel;
		hx_os_coro_channel_retain(channel);
		cases[i].coroutine = coroutine;
		cases[i].index = i;
		cases[i].operation = input[i].operation;
		cases[i].output = input[i].value;
		if (input[i].operation == HX_OS_CORO_SEND) {
			hx_os_status status;

			if (channel->values.drop && !channel->values.copy)
				return hx_os_i_error(
					error, HX_OS_NOT_SUPPORTED, 0, 0);

			cases[i].payload = hx_os_i_coro_sync_alloc(
				coroutine->runtime,
				channel->values.element_size,
				channel->values.element_alignment, error);
			if (!cases[i].payload)
				return error->status;

			if (channel->values.copy)
				status = channel->values.copy(cases[i].payload,
					input[i].value, channel->values.user);
			else {
				memcpy(cases[i].payload, input[i].value,
					channel->values.element_size);

				status = HX_OS_OK;
			}

			if (status != HX_OS_OK)
				return hx_os_i_error(error, status, 0, 0);

			cases[i].constructed = 1;
		}
	}

	return HX_OS_OK;
}

hx_os_status hx_os_coro_select(const hx_os_coro_case *input, size_t count,
	hx_os_deadline deadline, size_t *selected, hx_os_error *error)
{
	hx_os_coro *coroutine = hx_os_coro_current();

	hx_os_coro_runtime *runtime;
	hx_os_error detail;
	hx_os_status status;
	size_t i, start;
	int completed;

	if (!coroutine)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	runtime = coroutine->runtime;
	(void)hx_os_mutex_lock(runtime->mutex, NULL);

	completed = coroutine->wait_kind == HX_OS_CO_WAIT_CHANNEL &&
		coroutine->wait_done;
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	if (completed) {
		if (selected && coroutine->selected != SIZE_MAX)
			*selected = coroutine->selected;

		return hx_os_i_coro_consume(coroutine, error);
	}

	if (coroutine->preparing || coroutine->wait_kind != HX_OS_CO_WAIT_NONE)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	if (!input || !count || count > 64)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (hx_os_coro_cancel_requested(coroutine))
		return hx_os_i_error(error, HX_OS_CANCELLED, 0, 0);

	for (i = 0; i < count; ++i)
		if (!input[i].channel || input[i].channel->runtime != runtime ||
			!input[i].value ||
			(input[i].operation != HX_OS_CORO_SEND &&
				input[i].operation != HX_OS_CORO_RECEIVE))
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	hx_os_error_clear(&detail);
	++coroutine->preparing;

	status = hx_os_coro_channel_prepare(coroutine, input, count, &detail);
	--coroutine->preparing;
	if (status != HX_OS_OK)
		goto rejected;

	(void)hx_os_mutex_lock(runtime->mutex, NULL);

	status = hx_os_i_coro_begin_wait(
		coroutine, HX_OS_CO_WAIT_CHANNEL, deadline, &detail);
	coroutine->selected = SIZE_MAX;
	if (status == HX_OS_OK) {
		start = coroutine->bias++ % count;

		for (i = 0; i < count; ++i)
			if (hx_os_coro_channel_attempt(
				    &coroutine->cases[(start + i) % count]))
				break;

		if (!coroutine->wait_done && hx_os_deadline_expired(deadline)) {
			(void)hx_os_i_error(&detail,
				deadline == HX_OS_DEADLINE_NOW
					? HX_OS_WOULD_BLOCK
					: HX_OS_TIMEOUT,
				0, 0);
			hx_os_i_coro_finish_wait(coroutine, &detail);
		}

		if (!coroutine->wait_done)
			for (i = 0; i < count; ++i) {
				hx_os_coro_waiter *waiter =
					&coroutine->cases[(start + i) % count];
				hx_os_coro_channel *channel = waiter->channel;
				hx_os_coro_waiter **first =
					waiter->operation == HX_OS_CORO_SEND
					? &channel->send_first
					: &channel->receive_first;
				hx_os_coro_waiter **last =
					waiter->operation == HX_OS_CORO_SEND
					? &channel->send_last
					: &channel->receive_last;

				waiter->previous = *last;
				if (*last)
					(*last)->next = waiter;
				else
					*first = waiter;

				*last = waiter;
				waiter->linked = 1;
			}
	}

	completed = coroutine->wait_done;
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	if (status != HX_OS_OK)
		goto rejected;

	if (!completed) {
		status = hx_os_i_coro_suspend(coroutine, error);
		if (status != HX_OS_OK)
			return status;
	}

	if (selected && coroutine->selected != SIZE_MAX)
		*selected = coroutine->selected;

	return hx_os_i_coro_consume(coroutine, error);

rejected:
	hx_os_i_coro_channel_dispose_wait(coroutine);
	if (error)
		*error = detail;

	return status;
}

static hx_os_status hx_os_coro_channel_native(hx_os_coro_channel *channel,
	void *value, int operation, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_coro_runtime *runtime;
	hx_os_coro_waiter *waiter;
	hx_os_error detail;
	hx_os_status status = HX_OS_OK;

	if (!channel || !value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (hx_os_coro_channel_callback)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	if (deadline != HX_OS_DEADLINE_NOW &&
		(hx_os_i_coro_worker_inside() || hx_os_i_coro_io_inside()))
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	runtime = channel->runtime;
	hx_os_coro_channel_retain(channel);
	hx_os_error_clear(&detail);
	waiter = hx_os_i_coro_sync_alloc(runtime, sizeof(*waiter), 0, &detail);
	if (!waiter) {
		status = detail.status;
		goto released;
	}

	memset(waiter, 0, sizeof(*waiter));
	waiter->channel = channel;
	waiter->operation = operation;
	waiter->output = value;
	if (operation == HX_OS_CORO_SEND) {
		if (channel->values.drop && !channel->values.copy) {
			status = HX_OS_NOT_SUPPORTED;
			goto done;
		}

		waiter->payload = hx_os_i_coro_sync_alloc(runtime,
			channel->values.element_size,
			channel->values.element_alignment, &detail);
		if (!waiter->payload) {
			status = detail.status;
			goto done;
		}

		++hx_os_coro_channel_callback;
		if (channel->values.copy)
			status = channel->values.copy(
				waiter->payload, value, channel->values.user);
		else
			memcpy(waiter->payload, value,
				channel->values.element_size);

		--hx_os_coro_channel_callback;
		if (status != HX_OS_OK)
			goto done;

		waiter->constructed = 1;
	}

	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	if (runtime->closing == 2 || runtime->closed)
		status = HX_OS_CLOSED;
	else if (!hx_os_coro_channel_attempt(waiter)) {
		hx_os_coro_waiter **first = operation == HX_OS_CORO_SEND
			? &channel->send_first
			: &channel->receive_first;
		hx_os_coro_waiter **last = operation == HX_OS_CORO_SEND
			? &channel->send_last
			: &channel->receive_last;

		waiter->previous = *last;
		if (*last)
			(*last)->next = waiter;
		else
			*first = waiter;

		*last = waiter;
		waiter->linked = 1;

		while (!waiter->native_done && status == HX_OS_OK &&
			runtime->closing != 2 && !runtime->closed)
			status = hx_os_condition_wait_until(runtime->condition,
				runtime->mutex, deadline, &detail);
		if (waiter->native_done)
			status = waiter->native_status;
		else if (runtime->closing == 2 || runtime->closed)
			status = HX_OS_CLOSED;
		else if (status == HX_OS_TIMEOUT &&
			deadline == HX_OS_DEADLINE_NOW)
			status = HX_OS_WOULD_BLOCK;
	}
	else
		status = waiter->native_status;

	hx_os_coro_channel_detach(waiter);
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);

done:
	++hx_os_coro_channel_callback;
	if (waiter->constructed && channel->values.drop)
		channel->values.drop(waiter->payload, channel->values.user);

	--hx_os_coro_channel_callback;

	hx_os_i_coro_sync_free(runtime, waiter->payload);
	hx_os_i_coro_sync_free(runtime, waiter);

released:
	hx_os_coro_channel_release(channel);
	if (error) {
		*error = detail;
		error->status = status;
	}

	return status;
}

hx_os_status hx_os_coro_channel_send_until(hx_os_coro_channel *channel,
	const void *value, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_coro_case selected = {channel, (void *)value, HX_OS_CORO_SEND};

	if (!hx_os_coro_current())
		return hx_os_coro_channel_native(channel, (void *)value,
			HX_OS_CORO_SEND, deadline, error);

	return hx_os_coro_select(&selected, 1, deadline, NULL, error);
}

hx_os_status hx_os_coro_channel_receive_until(hx_os_coro_channel *channel,
	void *out, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_coro_case selected = {channel, out, HX_OS_CORO_RECEIVE};

	if (!hx_os_coro_current())
		return hx_os_coro_channel_native(
			channel, out, HX_OS_CORO_RECEIVE, deadline, error);

	return hx_os_coro_select(&selected, 1, deadline, NULL, error);
}
