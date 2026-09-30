#ifndef HX_C_PLATFORM_WATCH_PRIVATE_H
#define HX_C_PLATFORM_WATCH_PRIVATE_H
#include "internal.h"
#include "hx_os_watch.h"

struct hx_os_watch {
	hx_os_context *context;
	hx_os_thread *thread;
	hx_os_event *started;
	hx_os_event *gate;
	hx_os_watch_fn function;
	hx_os_watch_cleanup_fn cleanup;
	void *user;
	void *native;
	char *root;
	char *scratch;
	hx_os_watch_options options;
	hx_os_error startup;
	hx_os_error result;
	hx_os_atomic stopping;
	hx_os_atomic close_claim;
	size_t allocated;
	size_t scratch_capacity;
	int recursive;
	int accepted;
};
void *hx_os_i_watch_alloc(hx_os_watch *watch, size_t bytes, hx_os_error *error);
void hx_os_i_watch_free(hx_os_watch *watch, void *memory);
hx_os_status hx_os_i_watch_root(
	hx_os_watch *watch, const char *path, hx_os_error *error);
char *hx_os_i_watch_scratch(
	hx_os_watch *watch, size_t bytes, hx_os_error *error);
void hx_os_i_watch_emit(hx_os_watch *watch, uint32_t kind, const char *path,
	const char *old_path, uint32_t flags, const hx_os_error *error);
int hx_os_i_watch_stopping(hx_os_watch *watch);
hx_os_status hx_os_i_watch_native_init(hx_os_watch *watch, hx_os_error *error);
hx_os_status hx_os_i_watch_native_start(hx_os_watch *watch, hx_os_error *error);
hx_os_status hx_os_i_watch_native_run(hx_os_watch *watch, hx_os_error *error);
void hx_os_i_watch_native_stop(hx_os_watch *watch);
void hx_os_i_watch_native_destroy(hx_os_watch *watch);
#endif
