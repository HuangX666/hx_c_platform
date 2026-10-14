#include "internal.h"
#include "hx_line_reader.h"

struct hx_os_line_reader {
	hx_os_context *context;
	hx_os_stream *stream;
	unsigned char *line;
	size_t length, capacity, limit, position, buffered;
	hx_os_error pending_error;
	hx_os_status pending, blocked;
	int owned, busy, first, ready, eof, discarding, discarded;
	unsigned char buffer[4096];
};

int hx_os_line_reader_available(void)
{
	return HX_OS_ENABLE_LINE_READER && HX_OS_ENABLE_STREAM;
}

hx_os_status hx_os_line_reader_from_stream(hx_os_context *context,
	hx_os_stream *stream, size_t max_line_bytes, hx_os_line_reader **out,
	hx_os_error *error)
{
	hx_os_line_reader *reader;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (!hx_os_line_reader_available())
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!stream)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (max_line_bytes > SIZE_MAX - 4)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	reader = hx_os_i_zero(context, sizeof(*reader));
	if (!reader)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	reader->context = context;
	reader->stream = stream;
	reader->limit = max_line_bytes ? max_line_bytes : 1024 * 1024;
	reader->first = 1;
	*out = reader;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_line_reader_open(hx_os_context *context, const char *path,
	size_t max_line_bytes, hx_os_line_reader **out, hx_os_error *error)
{
	hx_os_stream *stream = NULL;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (!hx_os_line_reader_available())
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	status = hx_os_stream_open(
		context, path, HX_OS_FILE_READ, &stream, error);
	if (status == HX_OS_OK)
		status = hx_os_line_reader_from_stream(
			context, stream, max_line_bytes, out, error);
	if (status != HX_OS_OK)
		(void)hx_os_stream_close(&stream, NULL);
	else
		(*out)->owned = 1;

	return status;
}

static hx_os_status hx_os_line_fill(
	hx_os_line_reader *reader, hx_os_error *error)
{
	hx_os_status status;

	if (reader->position < reader->buffered)
		return HX_OS_OK;
	if (reader->pending != HX_OS_OK) {
		status = reader->pending;
		reader->pending = HX_OS_OK;
		if (status == HX_OS_EOF)
			reader->eof = 1;
		else if (error)
			*error = reader->pending_error;

		return status;
	}
	if (reader->eof)
		return HX_OS_EOF;

	reader->position = 0;
	reader->buffered = 0;
	hx_os_error_clear(&reader->pending_error);
	status = hx_os_stream_read_some(reader->stream, reader->buffer,
		sizeof(reader->buffer), &reader->buffered,
		&reader->pending_error);
	if (status == HX_OS_OK && !reader->buffered)
		status = hx_os_i_error(
			&reader->pending_error, HX_OS_IO_ERROR, 0, 0);
	reader->pending_error.status = status;
	if (reader->buffered) {
		reader->pending = status;

		return HX_OS_OK;
	}
	if (status == HX_OS_EOF)
		reader->eof = 1;
	if (error)
		*error = reader->pending_error;

	return status;
}

static hx_os_status hx_os_line_append(hx_os_line_reader *reader,
	const unsigned char *data, size_t bytes, hx_os_error *error)
{
	size_t maximum = reader->limit + 4;
	size_t needed, capacity;
	unsigned char *line;

	if (bytes > maximum - reader->length) {
		reader->blocked = HX_OS_LIMIT;

		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}

	needed = reader->length + bytes;
	if (needed > reader->capacity) {
		capacity = reader->capacity ? reader->capacity : 128;
		if (capacity > maximum)
			capacity = maximum;
		while (capacity < needed)
			capacity =
				capacity > maximum / 2 ? maximum : capacity * 2;

		line = hx_os_i_alloc(reader->context, capacity, 0);
		if (!line)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		if (reader->length)
			memcpy(line, reader->line, reader->length);

		hx_os_memory_free(reader->line);
		reader->line = line;
		reader->capacity = capacity;
	}
	if (bytes)
		memcpy(reader->line + reader->length, data, bytes);
	reader->length = needed;

	return HX_OS_OK;
}

static void hx_os_line_reset(hx_os_line_reader *reader)
{
	reader->length = 0;
	reader->ready = 0;
	reader->blocked = HX_OS_OK;
	reader->first = 0;
	reader->discarding = 0;
	reader->discarded = 0;
}

static hx_os_status hx_os_line_collect(
	hx_os_line_reader *reader, hx_os_error *error)
{
	while (!reader->ready) {
		const unsigned char *start, *newline;
		size_t bytes;
		hx_os_status status = hx_os_line_fill(reader, error);

		if (status == HX_OS_EOF) {
			if (!reader->length)
				return hx_os_i_error(error, HX_OS_EOF, 0, 0);

			reader->ready = 1;
			return HX_OS_OK;
		}
		if (status != HX_OS_OK)
			return status;

		start = reader->buffer + reader->position;
		bytes = reader->buffered - reader->position;
		newline = memchr(start, '\n', bytes);
		if (newline)
			bytes = (size_t)(newline - start);

		status = hx_os_line_append(reader, start, bytes, error);
		if (status != HX_OS_OK)
			return status;

		reader->position += bytes;
		if (newline) {
			++reader->position;
			reader->ready = 1;
		}
	}

	return HX_OS_OK;
}

hx_os_status hx_os_line_reader_next(
	hx_os_line_reader *reader, hx_string *out, hx_os_error *error)
{
	hx_string_view view;
	hx_os_status status;
	size_t offset = 0, invalid;

	if (!reader || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (reader->busy || reader->discarding)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	if (reader->blocked != HX_OS_OK)
		return hx_os_i_error(error, reader->blocked, 0, 0);

	reader->busy = 1;
	status = hx_os_line_collect(reader, error);
	if (status == HX_OS_OK) {
		if (reader->first && reader->length >= 3 &&
			!memcmp(reader->line, "\xef\xbb\xbf", 3))
			offset = 3;

		view.data =
			reader->line ? (const char *)reader->line + offset : "";
		view.size = reader->length - offset;
		if (view.size && view.data[view.size - 1] == '\r' &&
			!reader->eof)
			--view.size;
		if (reader->first && offset == 3 && !view.size && reader->eof) {
			hx_os_line_reset(reader);
			reader->busy = 0;

			return hx_os_i_error(error, HX_OS_EOF, 0, 0);
		}
		if (view.size > reader->limit)
			status = HX_OS_LIMIT;
		else
			status = hx_os_text_validate_utf8(
				view.data, view.size, &invalid);
		if (status != HX_OS_OK) {
			reader->blocked = status;
			(void)hx_os_i_error(error, status, 0, 0);
		}
		else {
			status = hx_string_assign(out, view, error);
			if (status == HX_OS_OK)
				hx_os_line_reset(reader);
		}
	}
	reader->busy = 0;

	return status;
}

hx_os_status hx_os_line_reader_skip_line(
	hx_os_line_reader *reader, hx_os_error *error)
{
	hx_os_status status = HX_OS_OK;

	if (!reader)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (reader->busy)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	reader->busy = 1;
	reader->discarding = 1;
	if (reader->length)
		reader->discarded = 1;
	if (!reader->ready) {
		for (;;) {
			const unsigned char *start, *newline;
			size_t bytes;

			status = hx_os_line_fill(reader, error);
			if (status != HX_OS_OK)
				break;

			start = reader->buffer + reader->position;
			bytes = reader->buffered - reader->position;
			newline = memchr(start, '\n', bytes);
			reader->discarded = 1;
			reader->position = newline
				? (size_t)(newline - reader->buffer) + 1
				: reader->buffered;
			if (newline)
				break;
		}
	}
	if (status == HX_OS_EOF && reader->discarded)
		status = HX_OS_OK;
	if (status == HX_OS_OK || status == HX_OS_EOF) {
		hx_os_line_reset(reader);
		(void)hx_os_i_error(error, status, 0, 0);
	}
	reader->busy = 0;

	return status;
}

hx_os_status hx_os_line_reader_close(
	hx_os_line_reader **reader, hx_os_error *error)
{
	hx_os_line_reader *value;
	hx_os_status status = HX_OS_OK;

	if (!reader)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	value = *reader;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (value->busy)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	value->busy = 1;
	if (value->owned)
		status = hx_os_stream_close(&value->stream, error);
	else
		(void)hx_os_i_error(error, HX_OS_OK, 0, 0);

	hx_os_memory_free(value->line);
	hx_os_memory_free(value);
	*reader = NULL;

	return status;
}
