#ifndef HX_C_PLATFORM_SIGNAL_PRIVATE_H
#define HX_C_PLATFORM_SIGNAL_PRIVATE_H
#include "internal.h"
#include "hx_os_signal.h"
typedef struct hx_os_signal_slot {
	hx_os_signal_fn function;
	hx_os_signal_cleanup_fn cleanup;
	void *user;
	uint64_t identifier;
	uint32_t running;
	uint32_t state;
	char name[128];
} hx_os_signal_slot;
struct hx_os_signal_handler {
	hx_os_context *context;
	hx_os_mutex *mutex;
	hx_os_condition *condition;
	hx_os_signal_slot *slots;
	size_t capacity;
	size_t max_parallel_emits;
	size_t max_depth;
	size_t live_slots;
	size_t emits;
	uint64_t generation;
	int closing;
};
hx_os_status hx_os_i_signal_name(
	const char *name, size_t *bytes, hx_os_error *error);
#endif
