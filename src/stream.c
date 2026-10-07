#include "internal.h"
#include "hx_os_stream.h"

enum { HX_STREAM_FILE, HX_STREAM_VIEW, HX_STREAM_MEMORY, HX_STREAM_CALLBACK };

struct hx_os_stream {
	hx_os_context *context;

	union {
		struct {
			hx_os_stream_callbacks callbacks;
			void *user;
		} custom;

		hx_os_file *file;

		struct {
			const unsigned char *view;
			unsigned char *memory;
			size_t size, capacity, position, limit;
		} bytes;
	} storage;

	int kind, busy;
};

static int hx_os_stream_size_valid(size_t bytes)
{
#if SIZE_MAX > INT64_MAX
	return bytes <= INT64_MAX;
#else
	(void)bytes;

	return 1;
#endif
}

static hx_os_status hx_os_stream_allocate(hx_os_context *context, int kind,
	hx_os_stream **out, hx_os_error *error)
{
	hx_os_stream *stream;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (!HX_OS_ENABLE_STREAM)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	stream = hx_os_i_zero(context, sizeof(*stream));
	if (!stream)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	stream->context = context;
	stream->kind = kind;
	*out = stream;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_stream_check(hx_os_stream *stream)
{
	if (!HX_OS_ENABLE_STREAM)
		return HX_OS_NOT_SUPPORTED;
	if (!stream)
		return HX_OS_INVALID;

	return stream->busy ? HX_OS_BUSY : HX_OS_OK;
}

static int hx_os_stream_overlap(
	const void *data, size_t bytes, const void *storage, size_t capacity)
{
	uintptr_t a = (uintptr_t)data, b = (uintptr_t)storage;

	return storage && bytes && capacity &&
		(a >= b ? a - b < capacity : b - a < bytes);
}

hx_os_status hx_os_stream_open(hx_os_context *context, const char *path,
	uint32_t flags, hx_os_stream **out, hx_os_error *error)
{
	hx_os_stream *stream;
	hx_os_status status =
		hx_os_stream_allocate(context, HX_STREAM_FILE, out, error);

	if (status != HX_OS_OK)
		return status;

	stream = *out;
	stream->busy = 1;
	status = hx_os_file_open(
		context, path, flags, &stream->storage.file, error);
	stream->busy = 0;
	if (status != HX_OS_OK) {
		hx_os_memory_free(stream);
		*out = NULL;
	}

	return status;
}

hx_os_status hx_os_stream_from_file(hx_os_context *context, hx_os_file **file,
	hx_os_stream **out, hx_os_error *error)
{
	hx_os_status status;

	if (!file || !*file) {
		if (out)
			*out = NULL;

		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}

	status = hx_os_stream_allocate(context, HX_STREAM_FILE, out, error);
	if (status == HX_OS_OK) {
		(*out)->storage.file = *file;
		*file = NULL;
	}

	return status;
}

hx_os_status hx_os_stream_from_memory(hx_os_context *context, const void *data,
	size_t bytes, hx_os_stream **out, hx_os_error *error)
{
	hx_os_status status;

	if ((!data && bytes) || !hx_os_stream_size_valid(bytes)) {
		if (out)
			*out = NULL;

		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}

	status = hx_os_stream_allocate(context, HX_STREAM_VIEW, out, error);
	if (status == HX_OS_OK) {
		(*out)->storage.bytes.view = data;
		(*out)->storage.bytes.size = bytes;
		(*out)->storage.bytes.limit = bytes;
	}

	return status;
}

hx_os_status hx_os_stream_memory(hx_os_context *context, size_t max_bytes,
	hx_os_stream **out, hx_os_error *error)
{
	hx_os_status status;

	if (!hx_os_stream_size_valid(max_bytes)) {
		if (out)
			*out = NULL;

		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	}

	status = hx_os_stream_allocate(context, HX_STREAM_MEMORY, out, error);
	if (status == HX_OS_OK)
		(*out)->storage.bytes.limit =
			max_bytes ? max_bytes : 16 * 1024 * 1024;

	return status;
}

hx_os_status hx_os_stream_create(hx_os_context *context,
	const hx_os_stream_callbacks *callbacks, void *user, hx_os_stream **out,
	hx_os_error *error)
{
	hx_os_status status;

	if (!callbacks || (!callbacks->read && !callbacks->write)) {
		if (out)
			*out = NULL;

		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}

	status = hx_os_stream_allocate(context, HX_STREAM_CALLBACK, out, error);
	if (status == HX_OS_OK) {
		(*out)->storage.custom.callbacks = *callbacks;
		(*out)->storage.custom.user = user;
	}

	return status;
}

hx_os_status hx_os_stream_read_some(hx_os_stream *stream, void *data,
	size_t bytes, size_t *completed, hx_os_error *error)
{
	hx_os_status status = hx_os_stream_check(stream);
	size_t count = 0;

	if (completed)
		*completed = 0;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if ((!data && bytes) || !completed)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!bytes)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	stream->busy = 1;
	if (stream->kind == HX_STREAM_FILE)
		status = hx_os_file_read_some(
			stream->storage.file, data, bytes, &count, error);
	else if (stream->kind == HX_STREAM_CALLBACK) {
		status = stream->storage.custom.callbacks.read
			? stream->storage.custom.callbacks.read(
				  stream->storage.custom.user, data, bytes,
				  &count, error)
			: hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	}
	else {
		const unsigned char *source = stream->kind == HX_STREAM_VIEW
			? stream->storage.bytes.view
			: stream->storage.bytes.memory;
		size_t capacity = stream->kind == HX_STREAM_VIEW
			? stream->storage.bytes.size
			: stream->storage.bytes.capacity;

		if (hx_os_stream_overlap(data, bytes, source, capacity)) {
			stream->busy = 0;
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		}

		count = stream->storage.bytes.position >=
				stream->storage.bytes.size
			? 0
			: stream->storage.bytes.size -
				stream->storage.bytes.position;
		if (count > bytes)
			count = bytes;
		if (count) {
			memcpy(data, source + stream->storage.bytes.position,
				count);
			stream->storage.bytes.position += count;
		}
		status = hx_os_i_error(
			error, count ? HX_OS_OK : HX_OS_EOF, 0, 0);
	}
	stream->busy = 0;
	if (count > bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*completed = count;
	return status;
}

static hx_os_status hx_os_stream_grow(
	hx_os_stream *stream, size_t needed, hx_os_error *error)
{
	size_t capacity;
	unsigned char *memory;

	if (needed <= stream->storage.bytes.capacity)
		return HX_OS_OK;

	capacity = stream->storage.bytes.capacity
		? stream->storage.bytes.capacity
		: 128;
	capacity = capacity > stream->storage.bytes.limit / 2
		? stream->storage.bytes.limit
		: capacity * 2;
	if (capacity < needed)
		capacity = needed;

	memory = hx_os_i_alloc(stream->context, capacity, 0);
	if (!memory)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	if (stream->storage.bytes.size)
		memcpy(memory, stream->storage.bytes.memory,
			stream->storage.bytes.size);

	hx_os_memory_free(stream->storage.bytes.memory);
	stream->storage.bytes.memory = memory;
	stream->storage.bytes.capacity = capacity;

	return HX_OS_OK;
}

hx_os_status hx_os_stream_write_some(hx_os_stream *stream, const void *data,
	size_t bytes, size_t *completed, hx_os_error *error)
{
	hx_os_status status = hx_os_stream_check(stream);
	size_t count = 0;

	if (completed)
		*completed = 0;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if ((!data && bytes) || !completed)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!bytes)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	stream->busy = 1;
	if (stream->kind == HX_STREAM_FILE)
		status = hx_os_file_write_some(
			stream->storage.file, data, bytes, &count, error);
	else if (stream->kind == HX_STREAM_CALLBACK) {
		status = stream->storage.custom.callbacks.write
			? stream->storage.custom.callbacks.write(
				  stream->storage.custom.user, data, bytes,
				  &count, error)
			: hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	}
	else if (stream->kind == HX_STREAM_VIEW)
		status = hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	else if (bytes >
		stream->storage.bytes.limit - stream->storage.bytes.position)
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	else {
		size_t needed = stream->storage.bytes.position + bytes;
		int overlap = hx_os_stream_overlap(data, bytes,
			stream->storage.bytes.memory,
			stream->storage.bytes.capacity);

		status = overlap ? hx_os_i_error(error, HX_OS_INVALID, 0, 0)
				 : hx_os_stream_grow(stream, needed, error);
		if (status == HX_OS_OK) {
			if (stream->storage.bytes.position >
				stream->storage.bytes.size)
				memset(stream->storage.bytes.memory +
						stream->storage.bytes.size,
					0,
					stream->storage.bytes.position -
						stream->storage.bytes.size);
			memcpy(stream->storage.bytes.memory +
					stream->storage.bytes.position,
				data, bytes);
			stream->storage.bytes.position = needed;
			if (stream->storage.bytes.size < needed)
				stream->storage.bytes.size = needed;
			count = bytes;
			status = hx_os_i_error(error, HX_OS_OK, 0, 0);
		}
	}
	stream->busy = 0;
	if (count > bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*completed = count;
	return status;
}

static hx_os_status hx_os_stream_exact(hx_os_stream *stream, void *data,
	size_t bytes, size_t *completed, int write, hx_os_error *error)
{
	size_t total = 0;
	hx_os_status status = hx_os_stream_check(stream);

	if (completed)
		*completed = 0;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if ((!data && bytes) || !completed)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	while (total < bytes) {
		size_t count = 0;
		unsigned char *buffer = (unsigned char *)data + total;

		status = write ? hx_os_stream_write_some(stream, buffer,
					 bytes - total, &count, error)
			       : hx_os_stream_read_some(stream, buffer,
					 bytes - total, &count, error);
		total += count;
		if (status != HX_OS_OK) {
			if (!write && status == HX_OS_EOF && total)
				status = hx_os_i_error(error,
					total == bytes ? HX_OS_OK
						       : HX_OS_TRUNCATED_INPUT,
					0, 0);
			break;
		}
		if (!count) {
			status = hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
			break;
		}
	}

	*completed = total;
	return status == HX_OS_OK ? hx_os_i_error(error, status, 0, 0) : status;
}

hx_os_status hx_os_stream_read_exact(hx_os_stream *stream, void *data,
	size_t bytes, size_t *completed, hx_os_error *error)
{
	return hx_os_stream_exact(stream, data, bytes, completed, 0, error);
}

hx_os_status hx_os_stream_write_all(hx_os_stream *stream, const void *data,
	size_t bytes, size_t *completed, hx_os_error *error)
{
	return hx_os_stream_exact(
		stream, (void *)data, bytes, completed, 1, error);
}

hx_os_status hx_os_stream_seek(hx_os_stream *stream, int64_t offset, int origin,
	uint64_t *position, hx_os_error *error)
{
	hx_os_status status = hx_os_stream_check(stream);
	uint64_t base, next;

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if (!position || origin < HX_OS_SEEK_BEGIN || origin > HX_OS_SEEK_END)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	stream->busy = 1;
	if (stream->kind == HX_STREAM_FILE)
		status = hx_os_file_seek(
			stream->storage.file, offset, origin, position, error);
	else if (stream->kind == HX_STREAM_CALLBACK)
		status = stream->storage.custom.callbacks.seek
			? stream->storage.custom.callbacks.seek(
				  stream->storage.custom.user, offset, origin,
				  position, error)
			: hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	else {
		base = origin == HX_OS_SEEK_BEGIN ? 0
			: origin == HX_OS_SEEK_CURRENT
			? stream->storage.bytes.position
			: stream->storage.bytes.size;
		if (offset < 0) {
			uint64_t magnitude = (uint64_t)(-(offset + 1)) + 1;

			next = magnitude > base ? UINT64_MAX : base - magnitude;
		}
		else
			next = (uint64_t)offset > UINT64_MAX - base
				? UINT64_MAX
				: base + (uint64_t)offset;

		status = hx_os_i_error(error,
			next > stream->storage.bytes.limit ? HX_OS_RANGE
							   : HX_OS_OK,
			0, 0);
		if (status == HX_OS_OK) {
			stream->storage.bytes.position = (size_t)next;
			*position = next;
		}
	}
	stream->busy = 0;

	return status;
}

hx_os_status hx_os_stream_tell(
	hx_os_stream *stream, uint64_t *position, hx_os_error *error)
{
	return hx_os_stream_seek(
		stream, 0, HX_OS_SEEK_CURRENT, position, error);
}

hx_os_status hx_os_stream_flush(
	hx_os_stream *stream, int persistent, hx_os_error *error)
{
	hx_os_status status = hx_os_stream_check(stream);

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if (persistent != 0 && persistent != 1)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	stream->busy = 1;
	if (stream->kind == HX_STREAM_CALLBACK)
		status = stream->storage.custom.callbacks.flush
			? stream->storage.custom.callbacks.flush(
				  stream->storage.custom.user, persistent,
				  error)
			: hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	else if (!persistent)
		status = hx_os_i_error(error, HX_OS_OK, 0, 0);
	else if (stream->kind == HX_STREAM_FILE)
		status = hx_os_file_sync(stream->storage.file, error);
	else
		status = hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	stream->busy = 0;

	return status;
}

hx_os_status hx_os_stream_data(hx_os_stream *stream, const void **data,
	size_t *bytes, hx_os_error *error)
{
	hx_os_status status = hx_os_stream_check(stream);

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if (!data || !bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (stream->kind != HX_STREAM_MEMORY && stream->kind != HX_STREAM_VIEW)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	*data = stream->kind == HX_STREAM_VIEW ? stream->storage.bytes.view
					       : stream->storage.bytes.memory;
	*bytes = stream->storage.bytes.size;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_stream_close(hx_os_stream **stream, hx_os_error *error)
{
	hx_os_stream *value;
	hx_os_status status = HX_OS_OK;

	if (!stream)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!*stream)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	value = *stream;
	if (value->busy)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	*stream = NULL;
	value->busy = 1;
	if (value->kind == HX_STREAM_FILE)
		status = hx_os_file_close(&value->storage.file, error);
	else if (value->kind == HX_STREAM_CALLBACK &&
		value->storage.custom.callbacks.close)
		status = value->storage.custom.callbacks.close(
			value->storage.custom.user, error);

	if (value->kind == HX_STREAM_MEMORY)
		hx_os_memory_free(value->storage.bytes.memory);
	hx_os_memory_free(value);

	return status == HX_OS_OK ? hx_os_i_error(error, status, 0, 0) : status;
}
