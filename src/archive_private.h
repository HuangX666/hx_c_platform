#ifndef HX_C_PLATFORM_ARCHIVE_PRIVATE_H
#define HX_C_PLATFORM_ARCHIVE_PRIVATE_H

#include "internal.h"
#include "file_utils_private.h"
#include "hx_os_archive.h"
#include "hx_os_util.h"

#define HX_AR_CHUNK 65536

typedef struct hx_ar_root hx_ar_root;
typedef struct hx_ar_sink hx_ar_sink;
typedef struct hx_ar_decoder hx_ar_decoder;

typedef struct hx_ar_item {
	hx_os_archive_entry entry;
	char path[];
} hx_ar_item;

typedef struct hx_ar_job {
	hx_os_context *parent, *context;
	hx_os_archive_options options;
	hx_os_archive_result result;
	hx_os_error error;
	hx_os_atomic used, budget_failed, failed, notifying;
	hx_os_mutex *mutex;
	void *locale, *previous_locale;
	uint64_t locale_thread;
	hx_string source_path, archive_file, destination_path;
	hx_os_vector *items;
	hx_ar_root *source_root, *output_root;
	hx_os_file *input, *output;
	hx_ar_decoder *decoder;
	unsigned char *buffer;
	uint64_t input_size, input_position, output_position;
	uint64_t source_bytes;
	const char *source, *archive_path, *destination, *selection;
	hx_os_archive_visit_fn visit;
	void *visit_user;
	char diagnostic[512];
	uint32_t format;
	uint32_t compression;
	int packing;
	int source_directory;
} hx_ar_job;

hx_os_status hx_ar_i_begin(hx_ar_job *job, hx_os_context *context,
	const hx_os_archive_options *options, hx_os_archive_result *result,
	hx_os_error *error);
hx_os_status hx_ar_i_end(hx_ar_job *job, hx_os_status status,
	hx_os_archive_result *result, hx_os_error *error);
hx_os_status hx_ar_i_fail(hx_ar_job *job, hx_os_status status,
	const hx_os_error *error, const char *message);
hx_os_status hx_ar_i_check(hx_ar_job *job, int notify);
hx_os_status hx_ar_i_progress(
	hx_ar_job *job, uint64_t content, uint64_t archive_position);
hx_os_status hx_ar_i_path(
	hx_ar_job *job, const char *path, hx_string *out, int directory);
uint32_t hx_ar_i_format(const char *path);
hx_os_status hx_ar_i_collect(hx_ar_job *job);
void hx_ar_i_items_free(hx_ar_job *job);
typedef hx_os_status (*hx_ar_source_fn)(
	hx_ar_job *job, const hx_os_archive_entry *entry, void *user);
hx_os_status hx_ar_i_walk(hx_ar_job *job, hx_ar_source_fn visit, void *user);
hx_os_status hx_ar_i_split(hx_ar_job *job, const char *path,
	hx_string *directory, hx_string *name);

// Native directory anchors keep extraction below the caller's root.
hx_os_status hx_ar_i_root_open(
	hx_ar_job *job, const char *path, int create, hx_ar_root **out);
void hx_ar_i_root_close(hx_ar_root *root);
const char *hx_ar_i_root_path(const hx_ar_root *root);
hx_os_status hx_ar_i_contains(
	hx_ar_root *root, const char *directory, int *inside);
hx_os_status hx_ar_i_directory(hx_ar_root *root, const char *path);
hx_os_status hx_ar_i_input(
	hx_ar_root *root, const char *path, hx_os_file **out);
hx_os_status hx_ar_i_sink(hx_ar_root *root, const char *path, int encrypted,
	hx_ar_sink **out, hx_os_file **file);
hx_os_status hx_ar_i_publish(hx_ar_sink *sink);
void hx_ar_i_sink_free(hx_ar_sink **sink);

#if HX_OS_HAVE_ARCHIVE
#include <archive.h>
#include <archive_entry.h>

hx_os_status hx_ar_i_native_error(
	hx_ar_job *job, struct archive *archive, int result);
hx_os_status hx_ar_i_pack(hx_ar_job *job);
hx_os_status hx_ar_i_read(hx_ar_job *job);
hx_os_status hx_ar_i_open_input(hx_ar_job *job);
hx_os_status hx_ar_i_decode_open(hx_ar_job *job);
la_ssize_t hx_ar_i_decode_read(hx_ar_job *job, const void **buffer);
hx_os_status hx_ar_i_decode_finish(hx_ar_job *job);
void hx_ar_i_decode_close(hx_ar_job *job);
hx_os_status hx_ar_i_reader_open(hx_ar_job *job, struct archive *native);
hx_os_status hx_ar_i_writer_open(hx_ar_job *job, struct archive *native);
hx_os_status hx_ar_i_destination(
	hx_ar_job *job, hx_string *selection, hx_string *name);
hx_os_status hx_ar_i_entry(hx_ar_job *job, const hx_os_archive_entry *entry);
hx_os_status hx_ar_i_locale_begin(hx_ar_job *job);
void hx_ar_i_locale_end(hx_ar_job *job);
void *hx_ar_i_callback_begin(hx_ar_job *job);
void hx_ar_i_callback_end(void *previous);
#endif

hx_os_status hx_ar_i_7zip(hx_ar_job *job);
#endif
