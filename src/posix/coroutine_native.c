#include "coroutine_native.h"
#include <sys/mman.h>
#include <unistd.h>
#if (defined(__x86_64__) || defined(__aarch64__)) && UINTPTR_MAX != UINT64_MAX
#error Stackful ILP32 on a 64-bit CPU ABI is not adapted
#endif
#if defined(__arm64e__)
#error Stackful arm64e is not adapted; disable stackful coroutines
#endif
#if defined(__linux__) && defined(__x86_64__)
#include <sys/syscall.h>
#endif

#ifndef __has_feature
#define __has_feature(feature) 0
#endif
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
#define HX_OS_CORO_ASAN 1

extern void __sanitizer_start_switch_fiber(void **, const void *, size_t);

extern void __sanitizer_finish_switch_fiber(void *, const void **, size_t *);
#endif
#if defined(__SANITIZE_THREAD__) || __has_feature(thread_sanitizer)
#define HX_OS_CORO_TSAN 1

extern void *__tsan_get_current_fiber(void);

extern void *__tsan_create_fiber(unsigned flags);

extern void __tsan_destroy_fiber(void *fiber);

extern void __tsan_switch_to_fiber(void *fiber, unsigned flags);
#endif

typedef char hx_os_coro_fp_offset[offsetof(hx_os_i_coro_context, floating) ==
			sizeof(uintptr_t) * 16
		? 1
		: -1];

void hx_os_i_coro_cpu_switch(
	hx_os_i_coro_context *from, hx_os_i_coro_context *to);

void hx_os_i_coro_cpu_capture(hx_os_i_coro_context *context);

void hx_os_i_coro_cpu_boot(void);

void hx_os_i_coro_context_entry(hx_os_i_coro_context *context);

hx_os_status hx_os_i_coro_context_root(
	hx_os_i_coro_context *context, hx_os_error *error)
{
#if defined(__linux__) && defined(__x86_64__) && defined(SYS_arch_prctl)
	unsigned long features = 0;

	/* ARCH_SHSTK_STATUS is absent from older distribution headers. */
	if (syscall(SYS_arch_prctl, 0x5005, &features) == 0) {
		if (features & 1)
			return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	}
	else if (errno != EINVAL && errno != ENOSYS && errno != ENOTSUP)
		return hx_os_i_errno(error, errno);
#endif
#if defined(HX_OS_CORO_TSAN)
	context->tsan_fiber = __tsan_get_current_fiber();
#else
	(void)context;
#endif
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_i_coro_context_root_close(hx_os_i_coro_context *context)
{
	(void)context;
}

hx_os_status hx_os_i_coro_stack_cost(
	size_t bytes, size_t *cost, hx_os_error *error)
{
	long native_page = sysconf(_SC_PAGESIZE);

	size_t page, rounded;

	if (native_page <= 0)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	page = (size_t)native_page;
	if (!hx_os_i_add_size(bytes, page - 1, &rounded) ||
		!hx_os_i_add_size(rounded / page * page, page * 2, cost))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_coro_context_create(hx_os_i_coro_context *context,
	size_t bytes, void (*function)(void *), void *user, hx_os_error *error)
{
	hx_os_status status;
	size_t page;
	uintptr_t top;

	void (*boot)(void) = hx_os_i_coro_cpu_boot;

	status = hx_os_i_coro_stack_cost(bytes, &context->mapping_bytes, error);
	if (status != HX_OS_OK)
		return status;

	page = (size_t)sysconf(_SC_PAGESIZE);
	context->mapping = mmap(NULL, context->mapping_bytes, PROT_NONE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (context->mapping == MAP_FAILED) {
		context->mapping = NULL;

		return hx_os_i_errno(error, errno);
	}

	context->stack_base = (unsigned char *)context->mapping + page;
	context->stack_bytes = context->mapping_bytes - 2 * page;
	if (mprotect((void *)context->stack_base, context->stack_bytes,
		    PROT_READ | PROT_WRITE)) {
		int code = errno;

		(void)munmap(context->mapping, context->mapping_bytes);
		context->mapping = NULL;

		return hx_os_i_errno(error, code);
	}

	context->function = function;
	context->user = user;
	top = ((uintptr_t)context->stack_base + context->stack_bytes) &
		~(uintptr_t)15;
#if defined(__x86_64__)
	context->registers[0] = top - sizeof(boot);
	context->registers[3] = (uintptr_t)context;
	memcpy((void *)context->registers[0], &boot, sizeof(boot));
#elif defined(__i386__)
	context->registers[0] = top - sizeof(boot);
	context->registers[1] = (uintptr_t)context;
	memcpy((void *)context->registers[0], &boot, sizeof(boot));
#elif defined(__aarch64__)
	context->registers[0] = (uintptr_t)context;
	context->registers[12] = top;
	memcpy(&context->registers[11], &boot, sizeof(boot));
#elif defined(__arm__)
	context->registers[0] = (uintptr_t)context;
	context->registers[8] = top;
	memcpy(&context->registers[9], &boot, sizeof(boot));
#else
#error Stackful coroutines require an adapted CPU ABI
#endif
	hx_os_i_coro_cpu_capture(context);
#if defined(HX_OS_CORO_TSAN)
	context->tsan_fiber = __tsan_create_fiber(0);
#endif
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_i_coro_context_destroy(hx_os_i_coro_context *context)
{
#if defined(HX_OS_CORO_TSAN)
	if (context->tsan_fiber)
		__tsan_destroy_fiber(context->tsan_fiber);
#endif
	if (context->mapping)
		(void)munmap(context->mapping, context->mapping_bytes);

	context->mapping = NULL;
}

void hx_os_i_coro_context_switch(
	hx_os_i_coro_context *from, hx_os_i_coro_context *to)
{
#if defined(HX_OS_CORO_ASAN)
	__sanitizer_start_switch_fiber(
		from->finished ? NULL : &from->asan_stack, to->stack_base,
		to->stack_bytes);
#endif
#if defined(HX_OS_CORO_TSAN)
	/* Pinned fibers share one native thread's sequencing, including TLS. */
	__tsan_switch_to_fiber(to->tsan_fiber, 0);
#endif
	hx_os_i_coro_cpu_switch(from, to);
#if defined(HX_OS_CORO_ASAN)
	__sanitizer_finish_switch_fiber(
		from->asan_stack, &to->stack_base, &to->stack_bytes);
#endif
}

void hx_os_i_coro_context_entry(hx_os_i_coro_context *context)
{
#if defined(HX_OS_CORO_ASAN)
	__sanitizer_finish_switch_fiber(context->asan_stack,
		&context->caller->stack_base, &context->caller->stack_bytes);
#endif
	context->function(context->user);
	context->finished = 1;

	for (;;)
		hx_os_i_coro_context_switch(context, context->caller);
}
