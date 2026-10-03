#include "internal.h"
#include "hx_os_kcp.h"

hx_os_status hx_os_kcp_options_init(hx_os_kcp_options *options, size_t capacity)
{
	if (!options || capacity < sizeof(*options))
		return HX_OS_INVALID;
	memset(options, 0, sizeof(*options));
	options->size = sizeof(*options);
	options->version = 1;
	options->max_message_bytes = 65536;
	options->max_send_segments = 128;
	options->receive_window = 128;
	options->mtu = 1200;
	options->interval_ms = 10;
	return HX_OS_OK;
}

#if HX_OS_ENABLE_KCP
#include "kcp_symbols.h"
#include <ikcp.h>

typedef union hx_os_kcp_slot hx_os_kcp_slot;
union hx_os_kcp_slot {
	hx_os_i_alignment alignment;
	struct {
		hx_os_kcp *owner;
		hx_os_kcp_slot *next;
		int large;
	} fields;
};

struct hx_os_kcp {
	ikcpcb *native;
	hx_os_kcp_output_fn output;
	void *user;
	hx_os_kcp_slot *small;
	hx_os_kcp_slot *large;
	hx_os_kcp_options options;
	hx_os_error output_error;
	size_t free_small;
	size_t small_size;
	size_t reserved;
	uint64_t clock;
	int entered;
	int updated;
};

/* Only the integration's private KCP copy uses this dispatch. It is restored
 * around each call, so independent contexts and threads never replace one
 * another's allocator. The adapter has no owned TLS heap or destructor;
 * compiler TLS support may allocate its own storage on older Android.
 */
#if defined(_MSC_VER)
static __declspec(thread) hx_os_kcp *hx_os_kcp_current;
#else
static __thread hx_os_kcp *hx_os_kcp_current;
#endif

void *hx_os_i_kcp_allocate(size_t bytes)
{
	hx_os_kcp *owner = hx_os_kcp_current;
	hx_os_kcp_slot **head, *slot;
	if (!owner || bytes > 4272)
		return NULL;
	head = bytes <= owner->small_size ? &owner->small : &owner->large;
	slot = *head;
	if (!slot)
		return NULL;
	*head = slot->fields.next;
	if (!slot->fields.large)
		--owner->free_small;
	return slot + 1;
}

void hx_os_i_kcp_deallocate(void *memory)
{
	hx_os_kcp_slot *slot;
	hx_os_kcp *owner;
	hx_os_kcp_slot **head;
	if (!memory)
		return;
	slot = (hx_os_kcp_slot *)memory - 1;
	owner = slot->fields.owner;
	head = slot->fields.large ? &owner->large : &owner->small;
	slot->fields.next = *head;
	*head = slot;
	if (!slot->fields.large)
		++owner->free_small;
}

static hx_os_kcp *hx_os_kcp_enter(hx_os_kcp *value)
{
	hx_os_kcp *saved = hx_os_kcp_current;
	value->entered = 1;
	hx_os_kcp_current = value;
	return saved;
}

static void hx_os_kcp_leave(hx_os_kcp *value, hx_os_kcp *saved)
{
	hx_os_kcp_current = saved;
	value->entered = 0;
}

static int hx_os_kcp_output(
	const char *packet, int bytes, ikcpcb *native, void *user)
{
	hx_os_kcp *value = user;
	hx_os_error error = {0};
	hx_os_status status;
	(void)native;
	status = value->output(packet, (size_t)bytes, value->user, &error);
	if (status != HX_OS_OK && !value->output_error.status) {
		value->output_error = error;
		value->output_error.status = status;
	}
	return status == HX_OS_OK ? 0 : -1;
}

static size_t hx_os_kcp_stride(size_t bytes)
{
	size_t alignment = HX_OS_I_ALIGNMENT;
	return (sizeof(hx_os_kcp_slot) + bytes + alignment - 1) / alignment *
		alignment;
}

hx_os_status hx_os_kcp_create(hx_os_context *context, uint32_t conv,
	const hx_os_kcp_options *input, hx_os_kcp_output_fn output, void *user,
	hx_os_kcp **out, hx_os_error *error)
{
	hx_os_kcp_options options;
	hx_os_kcp *value, *saved;
	size_t small_count, stride, large_stride, bytes, index;
	unsigned char *memory;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	(void)hx_os_kcp_options_init(&options, sizeof(options));
	if (input) {
		if (input->size < sizeof(*input) || input->version != 1)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		options = *input;
	}
	if (!output || options.mtu < 512 || options.mtu > 1400 ||
		options.max_send_segments < 1 ||
		options.max_send_segments > 4096 ||
		options.receive_window < 128 || options.receive_window > 4096 ||
		options.interval_ms < 10 || options.interval_ms > 100 ||
		!options.max_message_bytes ||
		options.max_message_bytes > (size_t)(options.mtu - 24) * 127)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	/* Send queues + both receive queues + one input packet + control state.
	 * ACK storage is reserved once; input flushes before it could grow.
	 */
	small_count = options.max_send_segments + 2 * options.receive_window +
		options.mtu / 24 + 8;
	stride = hx_os_kcp_stride(sizeof(struct IKCPSEG) + options.mtu);
	if (stride < hx_os_kcp_stride(sizeof(ikcpcb)))
		stride = hx_os_kcp_stride(sizeof(ikcpcb));
	if (stride < hx_os_kcp_stride(64 * 2 * sizeof(IUINT32)))
		stride = hx_os_kcp_stride(64 * 2 * sizeof(IUINT32));
	large_stride = hx_os_kcp_stride(4272);
	bytes = sizeof(*value) + HX_OS_I_ALIGNMENT + small_count * stride +
		2 * large_stride;
	value = hx_os_i_zero(context, bytes);
	if (!value)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	value->options = options;
	value->output = output;
	value->user = user;
	value->small_size = stride - sizeof(hx_os_kcp_slot);
	value->reserved = hx_os_i_allocation_bytes(value);
	memory = (unsigned char *)(value + 1);
	memory += (HX_OS_I_ALIGNMENT - (uintptr_t)memory % HX_OS_I_ALIGNMENT) %
		HX_OS_I_ALIGNMENT;
	for (index = 0; index < small_count + 2; ++index) {
		hx_os_kcp_slot *slot = (hx_os_kcp_slot *)memory;
		slot->fields.owner = value;
		slot->fields.large = index >= small_count;
		hx_os_i_kcp_deallocate(slot + 1);
		memory += slot->fields.large ? large_stride : stride;
	}
	saved = hx_os_kcp_enter(value);
	value->native = ikcp_create(conv, value);
	if (value->native &&
		ikcp_setmtu(value->native, (int)options.mtu) == 0) {
		value->native->acklist = hx_os_i_kcp_allocate(64 * 8);
		value->native->ackblock = 64;
		(void)ikcp_wndsize(value->native,
			(int)options.max_send_segments,
			(int)options.receive_window);
		(void)ikcp_nodelay(
			value->native, 1, (int)options.interval_ms, 2, 0);
		ikcp_setoutput(value->native, hx_os_kcp_output);
	}
	hx_os_kcp_leave(value, saved);
	if (!value->native || !value->native->acklist) {
		hx_os_kcp_destroy(value);
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	*out = value;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_kcp_destroy(hx_os_kcp *value)
{
	hx_os_kcp *saved;
	if (!value)
		return;
	saved = hx_os_kcp_enter(value);
	ikcp_release(value->native);
	hx_os_kcp_leave(value, saved);
	hx_os_memory_free(value);
}

hx_os_status hx_os_kcp_send(
	hx_os_kcp *value, const void *data, size_t bytes, hx_os_error *error)
{
	size_t count;
	hx_os_kcp *saved;
	int result;
	if (!value || (!data && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (value->entered)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	count = bytes ? (bytes - 1) / value->native->mss + 1 : 1;
	if (bytes > value->options.max_message_bytes || count >= 128 ||
		count > value->free_small ||
		count + (size_t)ikcp_waitsnd(value->native) >
			value->options.max_send_segments)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	saved = hx_os_kcp_enter(value);
	result = ikcp_send(value->native, data, (int)bytes);
	hx_os_kcp_leave(value, saved);
	return hx_os_i_error(
		error, result < 0 ? HX_OS_IO_ERROR : HX_OS_OK, 0, result);
}

static uint32_t hx_os_kcp_read32(const unsigned char *data)
{
	return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
		(uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

hx_os_status hx_os_kcp_input(
	hx_os_kcp *value, const void *packet, size_t bytes, hx_os_error *error)
{
	const unsigned char *cursor = packet;
	size_t left = bytes, pushes = 0;
	hx_os_kcp *saved;
	int result;
	if (!value || !packet)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (value->entered)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	if (bytes < 24 || bytes > value->options.mtu)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	while (left) {
		uint32_t length;
		unsigned int command;
		if (left < 24)
			return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
		length = hx_os_kcp_read32(cursor + 20);
		command = cursor[4];
		if (hx_os_kcp_read32(cursor) != value->native->conv ||
			command < 81 || command > 84 || length > left - 24 ||
			length > value->native->mss || cursor[5] >= 128 ||
			(command != 81 && (length || cursor[5])))
			return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
		pushes += command == 81;
		cursor += 24 + length;
		left -= 24 + length;
	}
	if (pushes > value->free_small)
		return hx_os_i_error(error, HX_OS_WOULD_BLOCK, 0, 0);
	saved = hx_os_kcp_enter(value);
	if (value->native->ackcount + pushes > value->native->ackblock) {
		/* flush requires initialization; update(0) is valid before the
		 * caller's first update and does not allocate memory.
		 */
		if (!value->native->updated)
			ikcp_update(value->native, (IUINT32)value->clock);
		ikcp_flush(value->native);
	}
	result = ikcp_input(value->native, packet, (long)bytes);
	hx_os_kcp_leave(value, saved);
	return hx_os_i_error(
		error, result < 0 ? HX_OS_SYNTAX : HX_OS_OK, 0, result);
}

hx_os_status hx_os_kcp_receive(hx_os_kcp *value, void *data, size_t capacity,
	size_t *bytes, hx_os_error *error)
{
	hx_os_kcp *saved;
	int size, result;
	if (!value || !bytes || (!data && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*bytes = 0;
	if (value->entered)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	size = ikcp_peeksize(value->native);
	if (size < 0)
		return hx_os_i_error(error, HX_OS_WOULD_BLOCK, 0, 0);
	*bytes = (size_t)size;
	if ((size_t)size > value->options.max_message_bytes)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if ((size_t)size > capacity)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);
	saved = hx_os_kcp_enter(value);
	result = ikcp_recv(value->native, data, size);
	hx_os_kcp_leave(value, saved);
	return hx_os_i_error(
		error, result < 0 ? HX_OS_IO_ERROR : HX_OS_OK, 0, result);
}

hx_os_status hx_os_kcp_update(
	hx_os_kcp *value, uint64_t now_ms, hx_os_error *error)
{
	hx_os_kcp *saved;
	hx_os_status status;
	if (!value || (value->updated && now_ms < value->clock))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (value->entered)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	value->clock = now_ms;
	value->updated = 1;
	saved = hx_os_kcp_enter(value);
	ikcp_update(value->native, (IUINT32)now_ms);
	hx_os_kcp_leave(value, saved);
	status = value->native->state == (IUINT32)-1
		? HX_OS_TIMEOUT
		: value->output_error.status;
	if (error)
		*error = value->output_error;
	if (error)
		error->status = status;
	hx_os_error_clear(&value->output_error);
	return status;
}

uint32_t hx_os_kcp_next_update_ms(const hx_os_kcp *value, uint64_t now_ms)
{
	IUINT32 next;
	IINT32 difference;
	if (!value)
		return 0;
	next = ikcp_check(value->native, (IUINT32)now_ms);
	difference = (IINT32)(next - (IUINT32)now_ms);
	return difference > 0 ? (uint32_t)difference : 0;
}

hx_os_status hx_os_kcp_flush(hx_os_kcp *value, hx_os_error *error)
{
	hx_os_kcp *saved;
	hx_os_status status;
	if (!value || !value->updated)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (value->entered)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	saved = hx_os_kcp_enter(value);
	ikcp_flush(value->native);
	hx_os_kcp_leave(value, saved);
	status = value->output_error.status;
	if (error)
		*error = value->output_error;
	hx_os_error_clear(&value->output_error);
	return status;
}

size_t hx_os_kcp_pending_segments(const hx_os_kcp *value)
{
	return value ? (size_t)ikcp_waitsnd(value->native) : 0;
}

size_t hx_os_kcp_reserved_bytes(const hx_os_kcp *value)
{
	return value ? value->reserved : 0;
}
#else
hx_os_status hx_os_kcp_flush(hx_os_kcp *value, hx_os_error *error)
{
	(void)value;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_kcp_create(hx_os_context *context, uint32_t conv,
	const hx_os_kcp_options *options, hx_os_kcp_output_fn output,
	void *user, hx_os_kcp **out, hx_os_error *error)
{
	(void)context;
	(void)conv;
	(void)options;
	(void)output;
	(void)user;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

void hx_os_kcp_destroy(hx_os_kcp *value)
{
	(void)value;
}

hx_os_status hx_os_kcp_send(
	hx_os_kcp *value, const void *data, size_t bytes, hx_os_error *error)
{
	(void)value;
	(void)data;
	(void)bytes;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_kcp_input(
	hx_os_kcp *value, const void *packet, size_t bytes, hx_os_error *error)
{
	return hx_os_kcp_send(value, packet, bytes, error);
}

hx_os_status hx_os_kcp_receive(hx_os_kcp *value, void *data, size_t capacity,
	size_t *bytes, hx_os_error *error)
{
	if (bytes)
		*bytes = 0;
	return hx_os_kcp_send(value, data, capacity, error);
}

hx_os_status hx_os_kcp_update(
	hx_os_kcp *value, uint64_t now_ms, hx_os_error *error)
{
	(void)now_ms;
	return hx_os_kcp_send(value, NULL, 0, error);
}

uint32_t hx_os_kcp_next_update_ms(const hx_os_kcp *value, uint64_t now_ms)
{
	(void)value;
	(void)now_ms;
	return 0;
}

size_t hx_os_kcp_pending_segments(const hx_os_kcp *value)
{
	(void)value;
	return 0;
}

size_t hx_os_kcp_reserved_bytes(const hx_os_kcp *value)
{
	(void)value;
	return 0;
}
#endif
