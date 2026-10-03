#include "signal_private.h"
enum {
	HX_OS_SIGNAL_FREE,
	HX_OS_SIGNAL_CONNECTED,
	HX_OS_SIGNAL_DISCONNECTED,
	HX_OS_SIGNAL_CLEANING
};
#if HX_OS_ENABLE_SIGNALS || HX_OS_ENABLE_PROCEDURES
typedef struct hx_os_signal_frame {
	struct hx_os_signal_frame *previous;
	hx_os_signal_handler *handler;
	hx_os_signal_connection connection;
} hx_os_signal_frame;
#if defined(_MSC_VER)
static __declspec(thread) hx_os_signal_frame *hx_os_signal_current;
#else
static __thread hx_os_signal_frame *hx_os_signal_current;
#endif
void *hx_os_i_signal_exchange_execution(void *value)
{
	hx_os_signal_frame *previous = hx_os_signal_current;
	hx_os_signal_current = value;
	return previous;
}
static int hx_os_signal_self(
	hx_os_signal_handler *handler, hx_os_signal_connection connection)
{
	hx_os_signal_frame *frame;
	for (frame = hx_os_signal_current; frame; frame = frame->previous)
		if (frame->handler == handler &&
			(!connection || frame->connection == connection))
			return 1;
	return 0;
}
static size_t hx_os_signal_depth(hx_os_signal_handler *handler)
{
	hx_os_signal_frame *frame;
	size_t depth = 0;
	for (frame = hx_os_signal_current; frame; frame = frame->previous)
		if (frame->handler == handler)
			++depth;
	return depth;
}
/* Caller owns mutex; temporarily releases it for user code. Slot stays
 * CLEANING until cleanup has returned. No slot reuse or early wait success.
 */
static void hx_os_signal_retire(hx_os_signal_handler *handler, size_t index)
{
	hx_os_signal_slot *slot = &handler->slots[index];
	hx_os_signal_frame frame;
	hx_os_signal_cleanup_fn cleanup;
	void *user;
	if (slot->state != HX_OS_SIGNAL_DISCONNECTED || slot->running)
		return;
	slot->state = HX_OS_SIGNAL_CLEANING;
	cleanup = slot->cleanup;
	user = slot->user;
	frame.previous = hx_os_signal_current;
	frame.handler = handler;
	frame.connection = slot->identifier;
	(void)hx_os_mutex_unlock(handler->mutex, NULL);
	hx_os_signal_current = &frame;
	if (cleanup)
		cleanup(user);
	hx_os_signal_current = frame.previous;
	(void)hx_os_mutex_lock(handler->mutex, NULL);
	slot->state = HX_OS_SIGNAL_FREE;
	slot->function = NULL;
	slot->cleanup = NULL;
	slot->user = NULL;
	--handler->live_slots;
	hx_os_condition_notify_all(handler->condition);
}
static int hx_os_signal_id(hx_os_signal_handler *handler,
	hx_os_signal_connection id, size_t *index)
{
	uint32_t position = (uint32_t)id;
	uint64_t generation = id >> 32;
	if (!position || position > handler->capacity || !generation ||
		generation >= handler->generation)
		return 0;
	*index = position - 1;
	return 1;
}
static int hx_os_signal_pending(
	hx_os_signal_handler *handler, size_t index, hx_os_signal_connection id)
{
	return handler->slots[index].identifier == id &&
		handler->slots[index].state != HX_OS_SIGNAL_FREE;
}
static void hx_os_signal_stop_locked(hx_os_signal_handler *handler)
{
	size_t i;
	handler->closing = 1;
	for (i = 0; i < handler->capacity; ++i)
		if (handler->slots[i].state == HX_OS_SIGNAL_CONNECTED)
			handler->slots[i].state = HX_OS_SIGNAL_DISCONNECTED;
}
static int hx_os_signal_compare(const void *left, const void *right)
{
	uint64_t a = *(const uint64_t *)left, b = *(const uint64_t *)right;
	return (a > b) - (a < b);
}
#endif
hx_os_status hx_os_i_dispatch_options_init(
	hx_os_signal_options *options, size_t capacity)
{
	const hx_os_signal_options defaults = HX_OS_SIGNAL_OPTIONS_INIT;
	if (!options)
		return HX_OS_INVALID;
	if (capacity < sizeof(*options))
		return HX_OS_OUTPUT_FULL;
	*options = defaults;
	return HX_OS_OK;
}
hx_os_status hx_os_i_dispatch_handler_create(
	hx_os_context *context, hx_os_signal_handler **out, hx_os_error *error)
{
	return hx_os_i_dispatch_handler_create_with_options(
		context, NULL, out, error);
}
hx_os_status hx_os_i_dispatch_handler_create_with_options(
	hx_os_context *context, const hx_os_signal_options *options,
	hx_os_signal_handler **out, hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS || HX_OS_ENABLE_PROCEDURES
	const hx_os_signal_options defaults = HX_OS_SIGNAL_OPTIONS_INIT;
	hx_os_signal_handler *handler;
	hx_os_status status;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!options)
		options = &defaults;
	if (options->size < sizeof(*options) || options->version != 1 ||
		!options->max_connections || options->max_connections > 65535 ||
		!options->max_parallel_emits ||
		options->max_parallel_emits > 65535 || !options->max_depth ||
		options->max_depth > 64)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	handler = hx_os_i_zero(context, sizeof(*handler));
	if (!handler)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	handler->context = context;
	handler->capacity = options->max_connections;
	handler->max_parallel_emits = options->max_parallel_emits;
	handler->max_depth = options->max_depth;
	handler->generation = 1;
	handler->slots = hx_os_i_zero(
		context, handler->capacity * sizeof(*handler->slots));
	status = handler->slots ? HX_OS_OK
				: hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	if (status == HX_OS_OK)
		status = hx_os_mutex_create(context, 0, &handler->mutex, error);
	if (status == HX_OS_OK)
		status = hx_os_condition_create(
			context, &handler->condition, error);
	if (status != HX_OS_OK) {
		if (handler->mutex)
			hx_os_mutex_destroy(handler->mutex);
		hx_os_memory_free(handler->slots);
		hx_os_memory_free(handler);
		return status;
	}
	*out = handler;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)context;
	(void)options;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_i_dispatch_connect(hx_os_signal_handler *handler,
	const char *signal, hx_os_signal_fn function, void *user,
	hx_os_signal_cleanup_fn cleanup, hx_os_signal_connection *out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS || HX_OS_ENABLE_PROCEDURES
	size_t bytes, i;
	hx_os_status status;
	if (out)
		*out = 0;
	if (!handler || !function)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_signal_name(signal, &bytes, error);
	if (status != HX_OS_OK)
		return status;
	(void)hx_os_mutex_lock(handler->mutex, NULL);
	if (handler->closing)
		status = HX_OS_CLOSED;
	else if (handler->generation > UINT32_MAX)
		status = HX_OS_LIMIT;
	else {
		if (handler->unique_names) {
			for (i = 0; i < handler->capacity; ++i) {
				if (handler->slots[i].state ==
						HX_OS_SIGNAL_CONNECTED &&
					!strcmp(handler->slots[i].name,
						signal)) {
					(void)hx_os_mutex_unlock(
						handler->mutex, NULL);
					return hx_os_i_error(
						error, HX_OS_EXISTS, 0, 0);
				}
			}
		}
		for (i = 0; i < handler->capacity; ++i)
			if (handler->slots[i].state == HX_OS_SIGNAL_FREE)
				break;
		if (i == handler->capacity)
			status = HX_OS_LIMIT;
		else {
			hx_os_signal_slot *slot = &handler->slots[i];
			slot->identifier =
				(handler->generation++ << 32) | (i + 1);
			slot->function = function;
			slot->cleanup = cleanup;
			slot->user = user;
			slot->state = HX_OS_SIGNAL_CONNECTED;
			memcpy(slot->name, signal, bytes + 1);
			++handler->live_slots;
			if (out)
				*out = slot->identifier;
		}
	}
	(void)hx_os_mutex_unlock(handler->mutex, NULL);
	return hx_os_i_error(error, status, 0, 0);
#else
	(void)handler;
	(void)signal;
	(void)function;
	(void)user;
	(void)cleanup;
	if (out)
		*out = 0;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_i_dispatch_emit(hx_os_signal_handler *handler,
	const char *signal, const hx_os_calldata *data, size_t *called,
	hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS || HX_OS_ENABLE_PROCEDURES
	static const hx_os_calldata empty = HX_OS_CALLDATA_INIT;
	hx_os_signal_frame frame;
	uint64_t *snapshot = NULL, cutoff, single;
	size_t bytes, needed = 0, count = 0, i, calls = 0;
	hx_os_status status, first = HX_OS_OK;
	hx_os_error first_error = {HX_OS_OK, HX_OS_ERROR_NONE, 0, 0};
	if (called)
		*called = 0;
	if (!handler)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_signal_name(signal, &bytes, error);
	if (status != HX_OS_OK)
		return status;
	(void)hx_os_mutex_lock(handler->mutex, NULL);
	if (handler->closing)
		status = HX_OS_CLOSED;
	else if (handler->emits == handler->max_parallel_emits ||
		hx_os_signal_depth(handler) >= handler->max_depth)
		status = HX_OS_LIMIT;
	if (status != HX_OS_OK) {
		(void)hx_os_mutex_unlock(handler->mutex, NULL);
		return hx_os_i_error(error, status, 0, 0);
	}
	++handler->emits;
	cutoff = handler->generation - 1;
	for (i = 0; i < handler->capacity; ++i)
		if (handler->slots[i].state == HX_OS_SIGNAL_CONNECTED &&
			!strcmp(handler->slots[i].name, signal))
			++needed;
	(void)hx_os_mutex_unlock(handler->mutex, NULL);
	frame.previous = hx_os_signal_current;
	frame.handler = handler;
	frame.connection = 0;
	hx_os_signal_current = &frame;
	if (needed) {
		snapshot = handler->unique_names
			? &single
			: hx_os_i_alloc(handler->context,
				  needed * sizeof(*snapshot), 0);
		if (!snapshot) {
			first = HX_OS_NO_MEMORY;
			(void)hx_os_i_error(&first_error, first, 0, 0);
			goto finish;
		}
		(void)hx_os_mutex_lock(handler->mutex, NULL);
		for (i = 0; i < handler->capacity; ++i) {
			hx_os_signal_slot *slot = &handler->slots[i];
			if (slot->state == HX_OS_SIGNAL_CONNECTED &&
				(slot->identifier >> 32) <= cutoff &&
				!strcmp(slot->name, signal))
				snapshot[count++] = slot->identifier;
		}
		(void)hx_os_mutex_unlock(handler->mutex, NULL);
		if (count > 1)
			qsort(snapshot, count, sizeof(*snapshot),
				hx_os_signal_compare);
	}
	for (i = 0; i < count; ++i) {
		size_t index = (uint32_t)snapshot[i] - 1;
		hx_os_signal_slot *slot = &handler->slots[index];
		hx_os_signal_fn function;
		hx_os_error callback_error = {HX_OS_OK, HX_OS_ERROR_NONE, 0, 0};
		void *user;
		(void)hx_os_mutex_lock(handler->mutex, NULL);
		if (slot->state != HX_OS_SIGNAL_CONNECTED ||
			slot->identifier != snapshot[i] || handler->closing) {
			(void)hx_os_mutex_unlock(handler->mutex, NULL);
			continue;
		}
		++slot->running;
		function = slot->function;
		user = slot->user;
		(void)hx_os_mutex_unlock(handler->mutex, NULL);
		frame.connection = snapshot[i];
		status = function(data ? data : &empty, user, &callback_error);
		frame.connection = 0;
		++calls;
		if (first == HX_OS_OK && status != HX_OS_OK) {
			first = status;
			if (callback_error.status != status)
				(void)hx_os_i_error(
					&callback_error, status, 0, 0);
			first_error = callback_error;
		}
		(void)hx_os_mutex_lock(handler->mutex, NULL);
		--slot->running;
		hx_os_signal_retire(handler, index);
		hx_os_condition_notify_all(handler->condition);
		(void)hx_os_mutex_unlock(handler->mutex, NULL);
	}
finish:
	if (snapshot != &single)
		hx_os_memory_free(snapshot);
	(void)hx_os_mutex_lock(handler->mutex, NULL);
	--handler->emits;
	hx_os_condition_notify_all(handler->condition);
	(void)hx_os_mutex_unlock(handler->mutex, NULL);
	hx_os_signal_current = frame.previous;
	if (called)
		*called = calls;
	if (error)
		*error = first_error;
	return first;
#else
	(void)handler;
	(void)signal;
	(void)data;
	if (called)
		*called = 0;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_i_dispatch_disconnect(hx_os_signal_handler *handler,
	hx_os_signal_connection connection, hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS || HX_OS_ENABLE_PROCEDURES
	size_t index;
	hx_os_status status = HX_OS_OK;
	if (!handler)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	(void)hx_os_mutex_lock(handler->mutex, NULL);
	if (!hx_os_signal_id(handler, connection, &index))
		status = HX_OS_NOT_FOUND;
	else if (hx_os_signal_pending(handler, index, connection)) {
		if (handler->slots[index].state == HX_OS_SIGNAL_CONNECTED)
			handler->slots[index].state = HX_OS_SIGNAL_DISCONNECTED;
		hx_os_signal_retire(handler, index);
	}
	(void)hx_os_mutex_unlock(handler->mutex, NULL);
	return hx_os_i_error(error, status, 0, 0);
#else
	(void)handler;
	(void)connection;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_i_dispatch_disconnect_until(hx_os_signal_handler *handler,
	hx_os_signal_connection connection, hx_os_deadline deadline,
	hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS || HX_OS_ENABLE_PROCEDURES
	size_t index;
	hx_os_status status;
	status = hx_os_i_dispatch_disconnect(handler, connection, error);
	if (status != HX_OS_OK)
		return status;
	(void)hx_os_mutex_lock(handler->mutex, NULL);
	if (!hx_os_signal_id(handler, connection, &index)) {
		(void)hx_os_mutex_unlock(handler->mutex, NULL);
		return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
	}
	if (hx_os_signal_pending(handler, index, connection) &&
		hx_os_signal_self(handler, connection))
		status = HX_OS_DEADLOCK;
	while (status == HX_OS_OK &&
		hx_os_signal_pending(handler, index, connection)) {
		status = hx_os_condition_wait_until(
			handler->condition, handler->mutex, deadline, error);
		if (!hx_os_signal_pending(handler, index, connection))
			status = HX_OS_OK;
	}
	(void)hx_os_mutex_unlock(handler->mutex, NULL);
	if (status == HX_OS_OK || status == HX_OS_DEADLOCK)
		return hx_os_i_error(error, status, 0, 0);
	return status;
#else
	(void)handler;
	(void)connection;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
void hx_os_i_dispatch_handler_stop(hx_os_signal_handler *handler)
{
#if HX_OS_ENABLE_SIGNALS || HX_OS_ENABLE_PROCEDURES
	if (!handler)
		return;
	(void)hx_os_mutex_lock(handler->mutex, NULL);
	hx_os_signal_stop_locked(handler);
	(void)hx_os_mutex_unlock(handler->mutex, NULL);
#else
	(void)handler;
#endif
}
hx_os_status hx_os_i_dispatch_handler_close_until(hx_os_signal_handler **owner,
	hx_os_deadline deadline, hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS || HX_OS_ENABLE_PROCEDURES
	hx_os_signal_handler *handler;
	hx_os_status status = HX_OS_OK;
	size_t i;
	if (!owner)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	handler = *owner;
	if (!handler)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	(void)hx_os_mutex_lock(handler->mutex, NULL);
	hx_os_signal_stop_locked(handler);
	if (hx_os_signal_self(handler, 0))
		status = HX_OS_DEADLOCK;
	else {
		for (i = 0; i < handler->capacity; ++i)
			hx_os_signal_retire(handler, i);
		while (status == HX_OS_OK &&
			(handler->emits || handler->live_slots)) {
			status = hx_os_condition_wait_until(handler->condition,
				handler->mutex, deadline, error);
			if (!handler->emits && !handler->live_slots)
				status = HX_OS_OK;
		}
	}
	(void)hx_os_mutex_unlock(handler->mutex, NULL);
	if (status != HX_OS_OK) {
		if (status == HX_OS_DEADLOCK)
			return hx_os_i_error(error, status, 0, 0);
		return status;
	}
	hx_os_condition_destroy(handler->condition);
	hx_os_mutex_destroy(handler->mutex);
	hx_os_memory_free(handler->slots);
	hx_os_memory_free(handler);
	*owner = NULL;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)owner;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_i_dispatch_handler_destroy(
	hx_os_signal_handler **handler, hx_os_error *error)
{
	return hx_os_i_dispatch_handler_close_until(
		handler, HX_OS_DEADLINE_NOW, error);
}
