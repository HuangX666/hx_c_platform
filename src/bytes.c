#include "internal.h"
#include "hx_bytes.h"

void hx_bytes_init(hx_bytes *bytes, hx_os_context *context)
{
	memset(bytes, 0, sizeof(*bytes));
	bytes->context = context;
	hx_os_i_context_retain(context);
}

void hx_bytes_destroy(hx_bytes *bytes)
{
	hx_bytes old = *bytes;

	memset(bytes, 0, sizeof(*bytes));
	hx_os_memory_free(old.data);
	hx_os_i_context_release(old.context);
}

hx_os_bytes_view hx_bytes_view(const hx_bytes *bytes)
{
	hx_os_bytes_view view = {bytes->data, bytes->size};

	return view;
}

hx_os_status hx_bytes_reserve(
	hx_bytes *bytes, size_t capacity, hx_os_error *error)
{
	unsigned char *memory;
	size_t grown;

	if (!bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (capacity > HX_BYTES_MAX_SIZE)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if (capacity <= bytes->capacity)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	grown = bytes->capacity ? bytes->capacity * 2 : 256;
	if (grown > HX_BYTES_MAX_SIZE)
		grown = HX_BYTES_MAX_SIZE;
	if (grown < capacity)
		grown = capacity;
	memory = hx_os_i_alloc(bytes->context, grown, 0);
	if (!memory)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	if (bytes->size)
		memcpy(memory, bytes->data, bytes->size);
	hx_os_memory_free(bytes->data);
	bytes->data = memory;
	bytes->capacity = grown;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_bytes_resize(hx_bytes *bytes, size_t size, hx_os_error *error)
{
	hx_os_status status = hx_bytes_reserve(bytes, size, error);

	if (status != HX_OS_OK)
		return status;
	if (size > bytes->size)
		memset(bytes->data + bytes->size, 0, size - bytes->size);
	bytes->size = size;

	return HX_OS_OK;
}

static hx_os_status hx_bytes_set(
	hx_bytes *bytes, hx_os_bytes_view input, int append, hx_os_error *error)
{
	size_t start, size, offset = 0;
	int alias;
	hx_os_status status;

	if (!bytes || (!input.data && input.size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	start = append ? bytes->size : 0;
	if (input.size > HX_BYTES_MAX_SIZE - start)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	size = start + input.size;
	alias = input.size && bytes->data &&
		(uintptr_t)input.data >= (uintptr_t)bytes->data &&
		(uintptr_t)input.data - (uintptr_t)bytes->data <
			bytes->capacity;
	if (alias) {
		offset = (size_t)((uintptr_t)input.data -
			(uintptr_t)bytes->data);
		if (offset > bytes->size || input.size > bytes->size - offset)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}
	status = hx_bytes_reserve(bytes, size, error);
	if (status != HX_OS_OK)
		return status;
	if (input.size)
		memmove(bytes->data + start,
			alias ? bytes->data + offset : input.data, input.size);
	bytes->size = size;

	return HX_OS_OK;
}

hx_os_status hx_bytes_assign(
	hx_bytes *bytes, hx_os_bytes_view input, hx_os_error *error)
{
	return hx_bytes_set(bytes, input, 0, error);
}

hx_os_status hx_bytes_append(
	hx_bytes *bytes, hx_os_bytes_view input, hx_os_error *error)
{
	return hx_bytes_set(bytes, input, 1, error);
}

void hx_bytes_move(hx_bytes *destination, hx_bytes *source)
{
	if (destination == source)
		return;
	hx_bytes_destroy(destination);
	*destination = *source;
	memset(source, 0, sizeof(*source));
}
