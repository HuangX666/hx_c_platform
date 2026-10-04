#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)
typedef struct power_state {
	hx_os_sleep_inhibit *inhibit;
	hx_os_performance_token *performance;
} power_state;

static void *release_on_thread(void *user)
{
	power_state *state = user;

	if (state->inhibit) {
		CHECK(hx_os_sleep_inhibit_set_active(state->inhibit, 0, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_sleep_inhibit_destroy(&state->inhibit, NULL) ==
			HX_OS_OK);
	}

	if (state->performance)
		CHECK(hx_os_performance_release(&state->performance, NULL) ==
			HX_OS_OK);

	return NULL;
}

typedef struct allocation_state {
	size_t calls, failure, live;
} allocation_state;

static void *HX_OS_CALL allocate(size_t bytes, void *user)
{
	allocation_state *state = user;
	void *memory;

	if (++state->calls == state->failure)
		return NULL;

	memory = malloc(bytes);
	if (memory)
		++state->live;

	return memory;
}

static void HX_OS_CALL deallocate(void *memory, void *user)
{
	allocation_state *state = user;

	if (memory) {
		--state->live;

		free(memory);
	}
}

int main(void)
{
	hx_os_sleep_inhibit *first = NULL, *second = NULL;
	hx_os_context *context = NULL;
	hx_os_context_options options;
	hx_os_thread *thread = NULL;
	power_state state = {NULL, NULL};
	allocation_state allocations = {0, 0, 0};
	hx_os_status status;
	size_t i;

	if (!(hx_os_capabilities() & HX_OS_CAP_SLEEP_INHIBIT)) {
		CHECK(hx_os_sleep_inhibit_create(NULL, "test", &first, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!first);
	}
	else {
		hx_os_context_options_init(&options);
		options.allocator.allocate = allocate;
		options.allocator.deallocate = deallocate;
		options.allocator.user = &allocations;
		CHECK(hx_os_context_create(&options, &context, NULL) ==
			HX_OS_OK);

		for (i = 1; i < 8; ++i) {
			allocations.calls = 0;
			allocations.failure = i;

			status = hx_os_sleep_inhibit_create(
				context, "有限分配测试😀", &first, NULL);
			allocations.failure = 0;
			CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
			if (first)
				CHECK(hx_os_sleep_inhibit_destroy(
					      &first, NULL) == HX_OS_OK);

			CHECK(hx_os_context_allocations(context) == 0 &&
				allocations.live == 1);
		}

		CHECK(hx_os_sleep_inhibit_create(context, "Exporting project A",
			      &first, NULL) == HX_OS_OK);
		CHECK(hx_os_sleep_inhibit_create(context, "Exporting project B",
			      &second, NULL) == HX_OS_OK);
		CHECK(hx_os_sleep_inhibit_active(first) &&
			hx_os_sleep_inhibit_active(second));
		CHECK(hx_os_sleep_inhibit_set_active(first, 1, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_sleep_inhibit_set_active(first, 0, NULL) ==
			HX_OS_OK);
		CHECK(!hx_os_sleep_inhibit_active(first) &&
			hx_os_sleep_inhibit_active(second));
		CHECK(hx_os_sleep_inhibit_destroy(&first, NULL) == HX_OS_OK);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);

		/* No concurrent allocator callbacks: ownership transfers to
		 * this thread, then joins before the context is destroyed.
		 */
		state.inhibit = second;
		CHECK(hx_os_thread_create(NULL, release_on_thread, &state,
			      &thread, NULL) == HX_OS_OK);
		CHECK(hx_os_thread_join_until(thread,
			      hx_os_deadline_after_ms(2000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);

		second = NULL;
		CHECK(!state.inhibit);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
			!allocations.live);
	}

	status = hx_os_performance_request(
		NULL, "A bounded execution activity", &state.performance, NULL);
	if (!(hx_os_capabilities() & HX_OS_CAP_PERFORMANCE_REQUEST))
		CHECK(status == HX_OS_NOT_SUPPORTED && !state.performance);
	else {
		CHECK(status == HX_OS_OK || status == HX_OS_NOT_SUPPORTED);
		if (status == HX_OS_OK) {
			CHECK(hx_os_thread_create(NULL, release_on_thread,
				      &state, &thread, NULL) == HX_OS_OK);
			CHECK(hx_os_thread_join_until(thread,
				      hx_os_deadline_after_ms(2000), NULL,
				      NULL) == HX_OS_OK);
			CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
			CHECK(!state.performance);
		}
	}

	return 0;
}
