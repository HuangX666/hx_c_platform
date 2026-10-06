#include <archive.h>
#include <archive_entry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int success, struct archive *archive, const char *operation)
{
	if (!success) {
		const char *error =
			archive ? archive_error_string(archive) : NULL;

		fprintf(stderr, "%s: %s\n", operation,
			error ? error : "failed");
		exit(1);
	}
}

static void round_trip(int format, int filter, const char *password)
{
	static const char contents[] = "archive payload\n\0with binary bytes";
	unsigned char *buffer = malloc(1024 * 1024);
	struct archive *writer = archive_write_new();
	struct archive_entry *entry = archive_entry_new();
	struct archive *reader;
	struct archive_entry *read_entry;
	char output[sizeof(contents)];
	size_t used = 0;
	la_ssize_t count;

	check(buffer && writer && entry, NULL, "allocate");
	check(archive_write_set_format(writer, format) == ARCHIVE_OK, writer,
		"set format");
	check(archive_write_add_filter(writer, filter) == ARCHIVE_OK, writer,
		"set filter without external program fallback");
	if (password) {
		check(archive_write_set_passphrase(writer, password) ==
				ARCHIVE_OK,
			writer, "set password");
		check(archive_write_set_options(
			      writer, "zip:encryption=aes256") == ARCHIVE_OK,
			writer, "select ZIP AES-256");
	}

	check(archive_write_open_memory(writer, buffer, 1024 * 1024, &used) ==
			ARCHIVE_OK,
		writer, "open memory");
	archive_entry_set_pathname(entry, "nested/payload.bin");
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_perm(entry, 0600);
	archive_entry_set_size(entry, sizeof(contents));
	check(archive_write_header(writer, entry) == ARCHIVE_OK, writer,
		"write header");
	count = archive_write_data(writer, contents, sizeof(contents));
	check(count == sizeof(contents), writer, "write payload");
	check(archive_write_close(writer) == ARCHIVE_OK, writer,
		"close writer");
	check(archive_write_free(writer) == ARCHIVE_OK, NULL, "free writer");
	archive_entry_free(entry);

	reader = archive_read_new();
	check(reader != NULL, NULL, "allocate reader");
	check(archive_read_support_format_by_code(reader, format) == ARCHIVE_OK,
		reader, "enable reader format");
	check(archive_read_support_filter_by_code(reader, filter) == ARCHIVE_OK,
		reader, "enable native reader filter");
	if (password)
		check(archive_read_add_passphrase(reader, password) ==
				ARCHIVE_OK,
			reader, "reader password");
	check(archive_read_open_memory(reader, buffer, used) == ARCHIVE_OK,
		reader, "open reader");
	check(archive_read_next_header(reader, &read_entry) == ARCHIVE_OK,
		reader, "read header");
	check(!strcmp(archive_entry_pathname(read_entry), "nested/payload.bin"),
		reader, "entry path");
	if (password)
		check(archive_entry_is_encrypted(read_entry) == 1, reader,
			"entry is encrypted");
	count = archive_read_data(reader, output, sizeof(output));
	check(count == sizeof(contents) &&
			!memcmp(contents, output, sizeof(contents)),
		reader, "read exact payload");
	check(archive_read_next_header(reader, &read_entry) == ARCHIVE_EOF,
		reader, "end of archive");
	check(archive_read_free(reader) == ARCHIVE_OK, NULL, "free reader");

	if (password) {
		reader = archive_read_new();
		check(reader != NULL, NULL, "allocate wrong-password reader");
		check(archive_read_support_format_zip(reader) == ARCHIVE_OK,
			reader, "enable ZIP");
		check(archive_read_add_passphrase(reader, "wrong") ==
				ARCHIVE_OK,
			reader, "set wrong password");
		check(archive_read_open_memory(reader, buffer, used) ==
				ARCHIVE_OK,
			reader, "open encrypted ZIP");
		check(archive_read_next_header(reader, &read_entry) ==
				ARCHIVE_OK,
			reader, "read ZIP header");
		check(archive_read_data(reader, output, sizeof(output)) < 0,
			reader, "wrong password must fail");
		archive_read_free(reader);
	}

	printf("format=%d filter=%d encrypted=%d passed\n", format, filter,
		password != NULL);
	free(buffer);
}

int main(void)
{
	round_trip(ARCHIVE_FORMAT_ZIP, ARCHIVE_FILTER_NONE, NULL);
	round_trip(
		ARCHIVE_FORMAT_ZIP, ARCHIVE_FILTER_NONE, "test-only-password");
	round_trip(ARCHIVE_FORMAT_7ZIP, ARCHIVE_FILTER_NONE, NULL);
	round_trip(
		ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_NONE, NULL);
	round_trip(
		ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_GZIP, NULL);
	round_trip(ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_XZ, NULL);
	round_trip(
		ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_ZSTD, NULL);

	return 0;
}
