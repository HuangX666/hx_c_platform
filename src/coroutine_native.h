#ifndef HX_C_PLATFORM_COROUTINE_NATIVE_H
#define HX_C_PLATFORM_COROUTINE_NATIVE_H

#include "internal.h"

typedef struct hx_os_i_coro_context {
#if !defined(_WIN32)
	uintptr_t registers[16];
	uint64_t floating[10];
#endif
	struct hx_os_i_coro_context *caller;

	void (*function)(void *user);

	void *user;
#if defined(_WIN32)
	void *fiber;
	size_t mapping_bytes;
	int converted;
#else
	void *mapping;
	const void *stack_base;
	size_t stack_bytes;
	size_t mapping_bytes;
	void *asan_stack;
	void *tsan_fiber;
#endif
	int finished;
} hx_os_i_coro_context;

hx_os_status hx_os_i_coro_context_root(
	hx_os_i_coro_context *context, hx_os_error *error);

void hx_os_i_coro_context_root_close(hx_os_i_coro_context *context);

hx_os_status hx_os_i_coro_stack_cost(
	size_t bytes, size_t *cost, hx_os_error *error);

hx_os_status hx_os_i_coro_context_create(hx_os_i_coro_context *context,
	size_t bytes, void (*function)(void *), void *user, hx_os_error *error);

void hx_os_i_coro_context_destroy(hx_os_i_coro_context *context);

void hx_os_i_coro_context_switch(
	hx_os_i_coro_context *from, hx_os_i_coro_context *to);

#endif
