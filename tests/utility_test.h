#ifndef HX_TEST_UTILITY_H
#define HX_TEST_UTILITY_H

#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__,     \
				#value);                                       \
			exit(1);                                               \
		}                                                              \
	} while (0)
typedef struct test_memory {
	size_t calls, fail, live;
} test_memory;

static inline void *HX_OS_CALL test_allocate(size_t bytes, void *user)
{
	test_memory *state = user;
	void *memory;

	if (++state->calls == state->fail)
		return NULL;

	memory = malloc(bytes);
	if (memory)
		++state->live;

	return memory;
}

static inline void HX_OS_CALL test_deallocate(void *memory, void *user)
{
	test_memory *state = user;

	if (memory) {
		CHECK(state->live);

		--state->live;

		free(memory);
	}
}

static inline hx_os_context *test_context(test_memory *memory)
{
	hx_os_context_options options;
	hx_os_context *context = NULL;

	hx_os_context_options_init(&options);
	options.allocator.allocate = test_allocate;
	options.allocator.deallocate = test_deallocate;
	options.allocator.user = memory;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);

	return context;
}
#endif
