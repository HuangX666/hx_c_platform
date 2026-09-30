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
typedef struct metrics {
	unsigned long long completed, aborted, inclusive, exclusive, minimum,
		maximum;
	unsigned flags;
} metrics;
static metrics read_metrics(const hx_string *text, const char *name)
{
	char marker[128];
	const char *row;
	metrics result;
	(void)snprintf(marker, sizeof(marker), "\t\"%s\"\t", name);
	row = strstr(hx_string_c_str(text), marker);
	CHECK(row);
	row += strlen(marker);
	CHECK(sscanf(row, "%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%u",
		      &result.completed, &result.aborted, &result.inclusive,
		      &result.exclusive, &result.minimum, &result.maximum,
		      &result.flags) == 7);
	return result;
}
static void record_work(hx_os_profiler *profiler)
{
	CHECK(hx_os_profiler_start_root(profiler, "work", NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_reach(profiler, "child", NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_leave(profiler, NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_leave(profiler, NULL) == HX_OS_OK);
}
static void *record_thread(void *user)
{
	int i;
	for (i = 0; i < 50; ++i)
		record_work(user);
	/* Deliberately keep the idle cache to exercise native TLS exit cleanup.
	 */
	return NULL;
}
static void *abort_thread(void *user)
{
	hx_os_profiler *profiler = user;
	CHECK(hx_os_profiler_start_root(profiler, "aborted_root", NULL) ==
		HX_OS_OK);
	CHECK(hx_os_profiler_reach(profiler, "aborted_child", NULL) ==
		HX_OS_OK);
	return NULL;
}
typedef struct idle_state {
	hx_os_profiler *profiler;
	hx_os_event *entered, *release;
} idle_state;
static void *idle_thread(void *user)
{
	idle_state *state = user;
	record_work(state->profiler);
	hx_os_event_set(state->entered);
	CHECK(hx_os_event_wait_until(state->release,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_restart_thread(state->profiler, NULL) == HX_OS_OK);
	return NULL;
}
typedef struct allocation_state {
	size_t calls, fail, live;
	hx_os_profiler *profiler;
	int reenter;
} allocation_state;
static void allocation_reentry(allocation_state *state)
{
	size_t required;
	hx_os_profiler *same = state->profiler;
	if (!state->reenter || !same)
		return;
	CHECK(hx_os_profiler_dump(same, NULL, 0, &required, NULL) ==
		HX_OS_DEADLOCK);
	CHECK(hx_os_profiler_close_until(&same, HX_OS_DEADLINE_NOW, NULL) ==
			HX_OS_DEADLOCK &&
		same == state->profiler);
}
static void *HX_OS_CALL allocate(size_t bytes, void *user)
{
	allocation_state *state = user;
	void *result;
	allocation_reentry(state);
	if (++state->calls == state->fail)
		return NULL;
	result = malloc(bytes);
	if (result)
		++state->live;
	return result;
}
static void HX_OS_CALL deallocate(void *memory, void *user)
{
	allocation_state *state = user;
	allocation_reentry(state);
	if (memory) {
		--state->live;
		free(memory);
	}
}
static void allocation_test(void)
{
	allocation_state state = {0, 0, 0, NULL, 0};
	hx_os_context *context = NULL;
	hx_os_context_options options;
	hx_string text = HX_STRING_INIT;
	size_t i;
	hx_os_context_options_init(&options);
	options.allocator.allocate = allocate;
	options.allocator.deallocate = deallocate;
	options.allocator.user = &state;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);
	for (i = 1; i < 7; ++i) {
		hx_os_status status;
		state.calls = 0;
		state.fail = i;
		status = hx_os_profiler_create(context, &state.profiler, NULL);
		state.fail = 0;
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		if (state.profiler)
			CHECK(hx_os_profiler_destroy(&state.profiler, NULL) ==
				HX_OS_OK);
		CHECK(hx_os_context_allocations(context) == 0 &&
			state.live == 1);
	}
	for (i = 1; i < 6; ++i) {
		hx_os_status status;
		CHECK(hx_os_profiler_create(context, &state.profiler, NULL) ==
			HX_OS_OK);
		state.calls = 0;
		state.fail = i;
		state.reenter = 1;
		status = hx_os_profiler_start_root(state.profiler, "new", NULL);
		state.fail = 0;
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		if (status == HX_OS_OK)
			CHECK(hx_os_profiler_leave(state.profiler, NULL) ==
				HX_OS_OK);
		CHECK(hx_os_profiler_destroy(&state.profiler, NULL) ==
			HX_OS_OK);
		state.reenter = 0;
		CHECK(hx_os_context_allocations(context) == 0 &&
			state.live == 1);
	}
	CHECK(hx_os_profiler_create(context, &state.profiler, NULL) ==
		HX_OS_OK);
	record_work(state.profiler);
	state.calls = 0;
	for (i = 0; i < 1000; ++i)
		record_work(state.profiler);
	CHECK(state.calls ==
		0); /* Warmed recording has no context allocation. */
	CHECK(hx_string_assign_cstr(&text, "unchanged", NULL) == HX_OS_OK);
	state.calls = 0;
	state.fail = 1;
	CHECK(hx_os_profiler_dump_string(state.profiler, &text, NULL) ==
			HX_OS_NO_MEMORY &&
		!strcmp(hx_string_c_str(&text), "unchanged"));
	state.fail = 0;
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);
	CHECK(hx_os_profiler_destroy(&state.profiler, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK && !state.live);
	hx_string_destroy(&text);
}
static void limit_test(void)
{
	hx_os_profiler *profiler = NULL;
	hx_os_profiler_options options = HX_OS_PROFILER_OPTIONS_INIT;
	char buffer[128] = "keep";
	size_t required;
	options.max_records = 1;
	options.max_depth = 1;
	options.max_name_bytes = 4;
	options.max_output_bytes = 16;
	CHECK(hx_os_profiler_create_with_options(
		      NULL, &options, &profiler, NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_start_root(profiler, "long_name", NULL) ==
		HX_OS_LIMIT);
	CHECK(hx_os_profiler_start_root(profiler, "\xff", NULL) ==
		HX_OS_INVALID_ENCODING);
	CHECK(hx_os_profiler_start_root(profiler, "one", NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_reach(profiler, "two", NULL) == HX_OS_LIMIT);
	CHECK(hx_os_profiler_leave(profiler, NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_start_root(profiler, "two", NULL) == HX_OS_LIMIT);
	CHECK(hx_os_profiler_dump(profiler, buffer, sizeof(buffer), &required,
		      NULL) == HX_OS_LIMIT &&
		!strcmp(buffer, "keep"));
	CHECK(hx_os_profiler_destroy(&profiler, NULL) == HX_OS_OK);
}
int main(void)
{
	hx_os_profiler *profiler = NULL;
	hx_os_profiler_options options;
	hx_os_thread *threads[4], *thread = NULL;
	hx_string text = HX_STRING_INIT, loaded = HX_STRING_INIT;
	char canary[8] = "keep", directory[96], path[128];
	size_t required, i;
	metrics parent, child;
	idle_state state;
	if (!(hx_os_capabilities() & HX_OS_CAP_PROFILER)) {
		CHECK(hx_os_profiler_create(NULL, &profiler, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!profiler);
		return 0;
	}
	CHECK(hx_os_profiler_options_init(&options, sizeof(options) - 1) ==
		HX_OS_OUTPUT_FULL);
	CHECK(hx_os_profiler_options_init(&options, sizeof(options)) ==
		HX_OS_OK);
	CHECK(hx_os_profiler_create(NULL, &profiler, NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_reach(profiler, "bad", NULL) == HX_OS_INVALID);
	CHECK(hx_os_profiler_leave(profiler, NULL) == HX_OS_INVALID);
	CHECK(hx_os_profiler_start_root(profiler, "work", NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_start_root(profiler, "another", NULL) ==
		HX_OS_BUSY);
	CHECK(hx_os_profiler_reach(profiler, "child", NULL) == HX_OS_OK);
	CHECK(hx_os_sleep_until(hx_os_deadline_after_ms(2), NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_leave(profiler, NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_leave(profiler, NULL) == HX_OS_OK);
	for (i = 0; i < 4; ++i)
		CHECK(hx_os_thread_create(NULL, record_thread, profiler,
			      &threads[i], NULL) == HX_OS_OK);
	for (i = 0; i < 4; ++i) {
		CHECK(hx_os_thread_join_until(threads[i],
			      hx_os_deadline_after_ms(3000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&threads[i], NULL) == HX_OS_OK);
	}
	CHECK(hx_os_thread_create(
		      NULL, abort_thread, profiler, &thread, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(3000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_dump_string(profiler, &text, NULL) == HX_OS_OK);
	parent = read_metrics(&text, "work");
	child = read_metrics(&text, "child");
	CHECK(parent.completed == 201 && child.completed == 201);
	CHECK(!parent.aborted && !parent.flags &&
		parent.inclusive >= child.inclusive);
	CHECK(parent.exclusive + child.inclusive == parent.inclusive);
	CHECK(child.exclusive == child.inclusive &&
		child.minimum <= child.maximum);
	CHECK(read_metrics(&text, "aborted_root").aborted == 1 &&
		read_metrics(&text, "aborted_child").completed == 0);
	CHECK(hx_os_profiler_start_root(profiler, "open", NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_destroy(&profiler, NULL) == HX_OS_DEADLOCK &&
		profiler);
	CHECK(hx_os_profiler_reach(profiler, "still_open", NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_restart_thread(profiler, NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_start_root(profiler, "中文😀\t\"\\", NULL) ==
		HX_OS_OK);
	CHECK(hx_os_profiler_leave(profiler, NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_dump(profiler, canary, sizeof(canary), &required,
		      NULL) == HX_OS_OUTPUT_FULL &&
		!strcmp(canary, "keep"));
	CHECK(hx_os_profiler_dump_string(profiler, &text, NULL) == HX_OS_OK &&
		strstr(hx_string_c_str(&text), "\\u0009\\\"\\\\"));
	if (hx_os_capabilities() & HX_OS_CAP_FILE_UTILS) {
		(void)snprintf(directory, sizeof(directory), "hx-profiler-%llx",
			(unsigned long long)hx_os_monotonic_ns());
		CHECK(hx_os_directory_create(directory, 0, NULL) == HX_OS_OK);
		(void)snprintf(path, sizeof(path), "%s/report.tsv", directory);
		CHECK(hx_os_profiler_save(profiler, path, NULL) == HX_OS_OK);
		CHECK(hx_os_file_read_text(path, &loaded, NULL) == HX_OS_OK &&
			!strcmp(hx_string_c_str(&loaded),
				hx_string_c_str(&text)));
		CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
		CHECK(hx_os_directory_remove(directory, NULL) == HX_OS_OK);
	}
	else
		CHECK(hx_os_profiler_save(profiler, "unused", NULL) ==
			HX_OS_NOT_SUPPORTED);
	state.profiler = profiler;
	CHECK(hx_os_event_create(NULL, 1, 0, &state.entered, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.release, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_create(NULL, idle_thread, &state, &thread, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.entered,
		      hx_os_deadline_after_ms(3000), NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_close_until(&profiler, HX_OS_DEADLINE_NOW, NULL) ==
			HX_OS_TIMEOUT &&
		profiler);
	CHECK(hx_os_profiler_start_root(profiler, "closed", NULL) ==
		HX_OS_CLOSED);
	CHECK(hx_os_profiler_dump_string(profiler, &text, NULL) == HX_OS_OK);
	hx_os_event_set(state.release);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(3000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_close_until(&profiler,
		      hx_os_deadline_after_ms(3000), NULL) == HX_OS_OK &&
		!profiler);
	hx_os_event_destroy(state.entered);
	hx_os_event_destroy(state.release);
	hx_string_destroy(&text);
	hx_string_destroy(&loaded);
	limit_test();
	allocation_test();
	return 0;
}
