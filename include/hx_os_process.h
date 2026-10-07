#ifndef HX_C_PLATFORM_PROCESS_H
#define HX_C_PLATFORM_PROCESS_H

#include "hx_os_file.h"
#include "hx_string.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct hx_os_process hx_os_process;
typedef struct hx_os_process_capture hx_os_process_capture;

typedef struct hx_os_process_options {
	uint32_t size;
	uint32_t version;
	hx_os_file *standard_input;
	hx_os_file *standard_output;
	hx_os_file *standard_error;
} hx_os_process_options;

#define HX_OS_PROCESS_OPTIONS_INIT                                             \
	{                                                                      \
		sizeof(hx_os_process_options), 1, NULL, NULL, NULL             \
	}

typedef struct hx_os_process_result {
	uint32_t exit_code;
	int signal_number;
} hx_os_process_result;

HX_OS_API hx_os_status HX_OS_CALL hx_os_process_options_init(
	hx_os_process_options *options, size_t capacity);

/*
 * Literal executable path, no shell or PATH search. arguments excludes
 * argv[0], which is supplied from path; NULL is an empty argument list.
 * UTF-8 arguments/environment are copied for spawn. environment=NULL takes
 * a native snapshot; {NULL} means empty. Entries are name=value, names
 * nonempty and unique (case-insensitive on Windows). 4 MiB/65536 entries
 * per list; Windows also enforces its native command-line length.
 * NULL streams inherit the parent's standard stream (Windows missing
 * streams use NUL). Explicit streams are borrowed only during spawn.
 * No host cwd/environment change. POSIX host descriptors without CLOEXEC
 * remain inherited; the host must coordinate unrelated descriptor/spawn
 * operations. Windows uses a handle allowlist; external broad-inheritance
 * spawns must be excluded while private inheritable duplicates exist.
 * No background worker. All operations on one object require exclusion.
 * Success owns a child; it does not prove application readiness. Some
 * POSIX implementations report pre-exec failure by child exit code 127
 * after spawn succeeds. This cannot distinguish an application's own 127.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_process_spawn(hx_os_context *context,
	const char *path, const char *const arguments[],
	const char *const environment[], const hx_os_process_options *options,
	hx_os_process **out, hx_os_error *error);

/* Nonzero child exit is still a successful wait. POSIX signal exits use
 * signal_number>0 and exit_code=0. No synthetic 128+signal conversion.
 * NOW polls once; timeout preserves process/result for retry. Success is
 * cached. Do not externally reap the same child or set SIGCHLD to ignore.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_process_wait_until(
	hx_os_process *process, hx_os_deadline deadline,
	hx_os_process_result *result, hx_os_error *error);

HX_OS_API uint64_t HX_OS_CALL hx_os_process_identifier(
	const hx_os_process *process);

/* destroy polls and returns BUSY while running. close waits then destroys;
 * timeout retains *process. No kill, detach, or implicit infinite wait.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_process_destroy(
	hx_os_process **process, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_process_close_until(
	hx_os_process **process, hx_os_deadline deadline, hx_os_error *error);

enum { HX_OS_PROCESS_STDOUT, HX_OS_PROCESS_STDERR };

typedef struct hx_os_process_capture_options {
	uint32_t size;
	uint32_t version;
	size_t stdout_limit;
	size_t stderr_limit;
} hx_os_process_capture_options;

#define HX_OS_PROCESS_CAPTURE_OPTIONS_INIT                                     \
	{                                                                      \
		sizeof(hx_os_process_capture_options), 1, 1024u * 1024u,       \
			1024u * 1024u                                          \
	}
HX_OS_API hx_os_status HX_OS_CALL hx_os_process_capture_options_init(
	hx_os_process_capture_options *options, size_t capacity);

/*
 * One call starts and collects both streams using a private local loop.
 * Child stdin is EOF; no input streaming or background thread. The creator
 * thread owns the session, including wait/view/text/close. Requires PROCESS,
 * PIPE and LOOP. Accepted spawn always returns an owned session, including
 * TIMEOUT/LIMIT/I/O failure; creation failure returns NULL.
 * Default limit is 1 MiB per stream. Reaching the limit pauses that stream
 * and reports LIMIT, even when EOF may immediately follow. No bytes are
 * silently dropped. raise_limit resumes; discard explicitly drains future
 * bytes without retaining them. Already retained bytes remain available.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_process_capture_run(
	hx_os_context *context, const char *path, const char *const arguments[],
	const char *const environment[], hx_os_deadline deadline,
	hx_os_process_capture **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_process_capture_run_with_options(
	hx_os_context *context, const char *path, const char *const arguments[],
	const char *const environment[],
	const hx_os_process_capture_options *options, hx_os_deadline deadline,
	hx_os_process_capture **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_process_capture_wait_until(
	hx_os_process_capture *capture, hx_os_deadline deadline,
	hx_os_process_result *result, hx_os_error *error);

/* View borrows raw bytes until the next wait or close; data may be NULL
 * for empty output. Text conversion is strict, transactional, uses out's
 * context and preserves BOM. Partial UTF-8 at a timeout may fail conversion.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_process_capture_view(
	hx_os_process_capture *capture, int stream, const void **data,
	size_t *bytes);

HX_OS_API hx_os_status HX_OS_CALL hx_os_process_capture_text(
	hx_os_process_capture *capture, int stream, hx_string *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_process_capture_text_encoded(
	hx_os_process_capture *capture, int stream, hx_os_encoding encoding,
	hx_string *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_process_capture_raise_limit(
	hx_os_process_capture *capture, int stream, size_t limit);

HX_OS_API hx_os_status HX_OS_CALL hx_os_process_capture_discard(
	hx_os_process_capture *capture, int stream);

/* Wait for process + both EOFs, then release. Same LIMIT/error/timeout
 * retention as wait. Descendants inheriting streams can delay EOF.
 * After an I/O error, explicit discard also closes that failed stream;
 * the child can observe a broken pipe. No forced termination.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_process_capture_close_until(
	hx_os_process_capture **capture, hx_os_deadline deadline,
	hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
