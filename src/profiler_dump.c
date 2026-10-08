#include "profiler_private.h"

typedef struct hx_os_profile_copy {
	const char *name;
	uint64_t completed, aborted, inclusive, exclusive, minimum, maximum;
	size_t name_bytes;
	uint32_t identifier, parent, depth, flags;
} hx_os_profile_copy;

typedef struct hx_os_profile_writer {
	char *buffer;
	size_t bytes;
	size_t maximum;
	hx_os_status status;
} hx_os_profile_writer;

static hx_os_status hx_os_profiler_copy_records(hx_os_profiler *profiler,
	hx_os_profile_copy **out, size_t *count, hx_os_error *error)
{
	hx_os_profile_copy *copy = NULL;
	unsigned attempt;
	size_t capacity = 0;

	*out = NULL;
	*count = 0;

	for (attempt = 0; attempt < 8; ++attempt) {
		size_t needed, i;
		hx_os_profile_record *record;

		(void)hx_os_mutex_lock(profiler->mutex, NULL);
		needed = profiler->records;
		if (needed <= capacity) {
			for (record = profiler->first, i = 0; record;
				record = record->next, ++i) {
				copy[i].name = (const char *)(record + 1);
				copy[i].name_bytes = record->name_bytes;
				copy[i].identifier = record->identifier;
				copy[i].parent = record->parent
					? record->parent->identifier
					: 0;
				copy[i].depth = record->depth;
				copy[i].flags = record->flags;
				copy[i].completed = record->completed;
				copy[i].aborted = record->aborted;
				copy[i].inclusive = record->inclusive;
				copy[i].exclusive = record->exclusive;
				copy[i].minimum =
					record->completed ? record->minimum : 0;
				copy[i].maximum = record->maximum;
			}

			(void)hx_os_mutex_unlock(profiler->mutex, NULL);
			*out = copy;
			*count = needed;

			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		}

		(void)hx_os_mutex_unlock(profiler->mutex, NULL);

		hx_os_memory_free(copy);
		copy = hx_os_i_alloc(
			profiler->context, needed * sizeof(*copy), 0);
		if (!copy)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

		capacity = needed;
	}

	hx_os_memory_free(copy);

	return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
}

static void hx_os_profiler_write(
	hx_os_profile_writer *writer, const char *text, size_t bytes)
{
	if (writer->status != HX_OS_OK)
		return;

	if (bytes > writer->maximum - writer->bytes) {
		writer->status = HX_OS_LIMIT;

		return;
	}

	if (writer->buffer && bytes)
		memcpy(writer->buffer + writer->bytes, text, bytes);

	writer->bytes += bytes;
}

static void hx_os_profiler_name(
	hx_os_profile_writer *writer, const char *name, size_t bytes)
{
	static const char hex[] = "0123456789abcdef";
	size_t i;

	hx_os_profiler_write(writer, "\"", 1);

	for (i = 0; i < bytes; ++i) {
		unsigned char c = (unsigned char)name[i];

		if (c < 0x20) {
			char text[6] = {'\\', 'u', '0', '0', 0, 0};

			text[4] = hex[c >> 4];
			text[5] = hex[c & 15];
			hx_os_profiler_write(writer, text, sizeof(text));
		}
		else if (c == '\\' || c == '"') {
			hx_os_profiler_write(writer, "\\", 1);
			hx_os_profiler_write(writer, name + i, 1);
		}
		else {
			size_t start = i++;

			while (i < bytes) {
				c = (unsigned char)name[i];
				if (c < 0x20 || c == '\\' || c == '"')
					break;
				++i;
			}
			hx_os_profiler_write(writer, name + start, i - start);
			--i;
		}
	}

	hx_os_profiler_write(writer, "\"", 1);
}

static void hx_os_profiler_decimal(hx_os_profile_writer *writer, uint64_t value)
{
	char digits[20];
	size_t start = sizeof(digits);

	do {
		digits[--start] = (char)('0' + value % 10);
		value /= 10;
	} while (value);
	hx_os_profiler_write(writer, digits + start, sizeof(digits) - start);
}

static hx_os_status hx_os_profiler_render(const hx_os_profile_copy *records,
	size_t count, size_t maximum, char *buffer, size_t *required)
{
	static const char header[] =
		"# hx_profiler v1\n"
		"id\tparent\tdepth\tname\tcompleted\taborted\tinclusive_ns\t"
		"exclusive_ns\tmin_ns\tmax_ns\tflags\n";
	hx_os_profile_writer writer = {buffer, 0, maximum, HX_OS_OK};
	size_t i;

	hx_os_profiler_write(&writer, header, sizeof(header) - 1);

	for (i = 0; i < count && writer.status == HX_OS_OK; ++i) {
		const hx_os_profile_copy *record = &records[i];
		uint64_t values[7];
		size_t j;

		hx_os_profiler_decimal(&writer, record->identifier);
		hx_os_profiler_write(&writer, "\t", 1);
		hx_os_profiler_decimal(&writer, record->parent);
		hx_os_profiler_write(&writer, "\t", 1);
		hx_os_profiler_decimal(&writer, record->depth);
		hx_os_profiler_write(&writer, "\t", 1);
		hx_os_profiler_name(&writer, record->name, record->name_bytes);
		values[0] = record->completed;
		values[1] = record->aborted;
		values[2] = record->inclusive;
		values[3] = record->exclusive;
		values[4] = record->minimum;
		values[5] = record->maximum;
		values[6] = record->flags;

		for (j = 0; j < 7; ++j) {
			hx_os_profiler_write(&writer, "\t", 1);
			hx_os_profiler_decimal(&writer, values[j]);
		}

		hx_os_profiler_write(&writer, "\n", 1);
	}

	/* A fixed snapshot and integer-only rendering make the measured second
	 * pass deterministic. Include NUL in the configured byte budget.
	 */
	hx_os_profiler_write(&writer, "", 1);
	if (writer.status == HX_OS_OK)
		*required = writer.bytes;

	return writer.status;
}

static hx_os_status hx_os_profiler_string(
	hx_os_profiler *profiler, hx_string *out, hx_os_error *error)
{
	hx_os_profile_copy *records = NULL;
	hx_string text;
	size_t count, required, written;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_profiler_copy_records(profiler, &records, &count, error);
	if (status != HX_OS_OK)
		return status;

	status = hx_os_profiler_render(records, count,
		profiler->options.max_output_bytes, NULL, &required);
	hx_string_init(&text, out->context);
	if (status == HX_OS_OK)
		status = hx_string_reserve(&text, required - 1, error);

	if (status == HX_OS_OK) {
		status = hx_os_profiler_render(records, count,
			profiler->options.max_output_bytes, text.data,
			&written);
		if (status == HX_OS_OK && written != required)
			status = HX_OS_IO_ERROR;

		if (status == HX_OS_OK) {
			text.size = required - 1;
			hx_string_move(out, &text);
		}
	}

	hx_string_destroy(&text);
	hx_os_memory_free(records);

	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_profiler_dump(hx_os_profiler *profiler, char *buffer,
	size_t capacity, size_t *required, hx_os_error *error)
{
	hx_os_profile_guard guard;
	hx_os_profile_copy *records = NULL;
	size_t count, needed, written;
	hx_os_status status;

	if (!required || (!buffer && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_i_profiler_begin(profiler, &guard, 1, error);
	if (status != HX_OS_OK)
		return status;

	status = hx_os_profiler_copy_records(profiler, &records, &count, error);
	if (status == HX_OS_OK)
		status = hx_os_profiler_render(records, count,
			profiler->options.max_output_bytes, NULL, &needed);

	if (status == HX_OS_OK) {
		*required = needed;
		if (capacity < needed)
			status = HX_OS_OUTPUT_FULL;
		else
			status = hx_os_profiler_render(records, count,
				profiler->options.max_output_bytes, buffer,
				&written);
	}

	hx_os_memory_free(records);
	hx_os_i_profiler_end(&guard);

	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_profiler_dump_string(
	hx_os_profiler *profiler, hx_string *out, hx_os_error *error)
{
	hx_os_profile_guard guard;
	hx_os_status status =
		hx_os_i_profiler_begin(profiler, &guard, 1, error);

	if (status != HX_OS_OK)
		return status;

	status = hx_os_profiler_string(profiler, out, error);
	hx_os_i_profiler_end(&guard);

	return status;
}

hx_os_status hx_os_profiler_save(
	hx_os_profiler *profiler, const char *path, hx_os_error *error)
{
#if HX_OS_ENABLE_FILE_UTILS
	hx_os_profile_guard guard;
	hx_string text;
	hx_os_status status =
		hx_os_i_profiler_begin(profiler, &guard, 1, error);

	if (status != HX_OS_OK)
		return status;

	hx_string_init(&text, profiler->context);

	status = hx_os_profiler_string(profiler, &text, error);
	if (status == HX_OS_OK)
		status = hx_os_file_save_atomic(
			path, hx_string_c_str(&text), text.size, error);

	hx_string_destroy(&text);
	hx_os_i_profiler_end(&guard);

	return status;
#else
	(void)profiler;
	(void)path;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
