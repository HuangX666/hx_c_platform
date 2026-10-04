#include "utility_test.h"

static void test_string_in_place(void)
{
	test_memory memory = {0, 0, 0};

	hx_os_context *context = test_context(&memory);

	hx_string text;
	size_t before;

	hx_string_init(&text, context);
	CHECK(hx_string_assign_cstr(&text, " \tAb中文😀\r\n", NULL) ==
		HX_OS_OK);

	before = memory.calls;
	memory.fail = before + 1;
	hx_string_trim_ascii_in_place(&text);
	hx_string_lower_ascii_in_place(&text);
	CHECK(!strcmp(text.data, "ab中文😀"));

	hx_string_upper_ascii_in_place(&text);
	CHECK(!strcmp(text.data, "AB中文😀") && memory.calls == before);

	hx_string_clear(&text);
	hx_string_trim_ascii_in_place(&text);
	CHECK(text.size == 0);

	memory.fail = 0;

	hx_string_destroy(&text);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!memory.live);
}

static void test_calldata_edit(void)
{
	hx_os_calldata data = HX_OS_CALLDATA_INIT;

	hx_os_calldata_edit edit = hx_os_calldata_edit_begin(&data);

	hx_os_error error;
	hx_string_view view;
	int64_t number = 0;

	if (!(hx_os_capabilities() &
		    (HX_OS_CAP_SIGNALS | HX_OS_CAP_PROCEDURES))) {
		hx_os_calldata_edit_set_i64(&edit, "number", 42);
		CHECK(hx_os_calldata_edit_finish(&edit, &error) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(!data.count);

		return;
	}

	hx_os_calldata_edit_set_i64(&edit, "number", 42);
	hx_os_calldata_edit_set_string(
		&edit, "label", hx_string_view_from_cstr("中文😀"));
	CHECK(hx_os_calldata_edit_finish(&edit, NULL) == HX_OS_OK);
	CHECK(hx_os_calldata_get_string(&data, "label", &view, NULL) ==
		HX_OS_OK);
	CHECK(view.size == strlen("中文😀"));

	edit = hx_os_calldata_edit_begin(&data);
	hx_os_calldata_edit_set_i64(&edit, "number", 43);
	hx_os_calldata_edit_set_string(
		&edit, "bad", hx_string_view_from_cstr("\xff"));
	hx_os_calldata_edit_set_i64(&edit, "skipped", 1);
	CHECK(hx_os_calldata_edit_finish(&edit, &error) ==
		HX_OS_INVALID_ENCODING);
	CHECK(hx_os_calldata_edit_finish(&edit, NULL) ==
		HX_OS_INVALID_ENCODING);
	CHECK(error.status == HX_OS_INVALID_ENCODING);
	CHECK(hx_os_calldata_get_i64(&data, "number", &number, NULL) ==
			HX_OS_OK &&
		number == 43);
	CHECK(hx_os_calldata_get_i64(&data, "skipped", &number, NULL) ==
		HX_OS_NOT_FOUND);
}

typedef struct update_observer {
	size_t calls;
} update_observer;

static void HX_OS_CALL update_changed(
	hx_settings *settings, const hx_settings_change *change, void *user)
{
	update_observer *observer = user;
	int64_t value;

	(void)change;
	++observer->calls;
	CHECK(hx_settings_get_i64(settings, "port", &value, NULL) == HX_OS_OK);
	CHECK(value == 8080);
}

static void *replace_strings(void *user)
{
	hx_settings *settings = user;
	unsigned i;

	for (i = 0; i < 200; ++i)
		CHECK(hx_settings_set_string(settings, "label",
			      i % 2 ? "concurrent A" : "concurrent B",
			      NULL) == HX_OS_OK);

	return NULL;
}

static void test_settings_updates(void)
{
	test_memory memory = {0, 0, 0};

	hx_os_context *context = test_context(&memory);

	hx_settings *settings = NULL;
	hx_settings_string_ref ref = HX_SETTINGS_STRING_REF_INIT;
	hx_settings_string_ref copy = HX_SETTINGS_STRING_REF_INIT;
	hx_settings_update update;
	hx_settings_options options = HX_SETTINGS_OPTIONS_INIT;
	update_observer observer = {0};
	hx_os_error error;
	int64_t port = 0;
	size_t before, applied;

	if (!(hx_os_capabilities() & HX_OS_CAP_SETTINGS)) {
		update = hx_settings_update_begin(NULL);
		hx_settings_update_set_i64(&update, "port", 8080);
		CHECK(hx_settings_update_finish(&update, &applied, &error) ==
				HX_OS_NOT_SUPPORTED &&
			!applied);
		CHECK(hx_settings_value_get_string_ref(NULL, &ref, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_settings_get_string_ref(NULL, "label", &ref, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);

		return;
	}

	options.on_change = update_changed;
	options.user = &observer;
	CHECK(hx_settings_create_with_options(
		      context, &options, &settings, NULL) == HX_OS_OK);

	update = hx_settings_update_begin(settings);
	hx_settings_update_set_i64(&update, "port", 8080);
	hx_settings_update_set_string(&update, "label", "旧值😀");
	CHECK(hx_settings_update_finish(&update, &applied, NULL) == HX_OS_OK &&
		applied == 2 && observer.calls == 2);

	before = memory.calls;
	memory.fail = before + 1;
	CHECK(hx_settings_get_string_ref(settings, "label", &ref, NULL) ==
		HX_OS_OK);

	hx_settings_string_ref_copy(&copy, &ref);
	hx_settings_string_ref_copy(&copy, &copy);
	CHECK(!strcmp(ref.text.data, "旧值😀") && memory.calls == before);
	CHECK(hx_settings_get_string_ref(settings, "port", &ref, NULL) ==
			HX_OS_TYPE_MISMATCH &&
		!strcmp(ref.text.data, "旧值😀"));
	CHECK(hx_settings_get_string_ref(settings, "missing", &ref, NULL) ==
			HX_OS_NOT_FOUND &&
		!strcmp(ref.text.data, "旧值😀"));

	update = hx_settings_update_begin(settings);
	hx_settings_update_set_string(&update, "label", "分配失败");
	hx_settings_update_set_i64(&update, "skipped", 1);
	CHECK(hx_settings_update_finish(&update, &applied, &error) ==
			HX_OS_NO_MEMORY &&
		!applied && observer.calls == 2);

	memory.fail = 0;
	CHECK(hx_settings_get_i64(settings, "skipped", &port, NULL) ==
		HX_OS_NOT_FOUND);

	update = hx_settings_update_begin(settings);
	hx_settings_update_set_string(&update, "label", "新值");
	hx_settings_update_set_string(&update, "invalid", "\xff");
	hx_settings_update_set_i64(&update, "skipped", 1);
	CHECK(hx_settings_update_finish(&update, &applied, &error) ==
			HX_OS_INVALID_ENCODING &&
		applied == 1 && observer.calls == 3);
	CHECK(!strcmp(ref.text.data, "旧值😀") &&
		!strcmp(copy.text.data, "旧值😀"));

	{
		hx_os_thread *writer = NULL;
		unsigned i;

		CHECK(hx_os_thread_create(NULL, replace_strings, settings,
			      &writer, NULL) == HX_OS_OK);

		for (i = 0; i < 10000; ++i) {
			hx_settings_string_ref temporary =
				HX_SETTINGS_STRING_REF_INIT;

			hx_settings_string_ref_copy(&temporary, &copy);
			CHECK(!strcmp(temporary.text.data, "旧值😀"));

			hx_settings_string_ref_destroy(&temporary);
		}

		CHECK(hx_os_thread_join_until(writer, HX_OS_DEADLINE_INFINITE,
			      NULL, NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&writer, NULL) == HX_OS_OK);
	}

	CHECK(hx_settings_close_until(&settings, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_OK);
	CHECK(!strcmp(copy.text.data, "旧值😀"));
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);

	hx_settings_string_ref_destroy(&ref);
	hx_settings_string_ref_destroy(&copy);
	hx_settings_string_ref_destroy(&copy);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!memory.live);
}

static void test_lookup_view(void)
{
	test_memory memory = {0, 0, 0};

	hx_os_context *context = test_context(&memory);

	hx_os_text_lookup *lookup = NULL;
	hx_string_view view = {"preserved", 9};
	size_t before;
	const char *fallback = "missing";

	if (!(hx_os_capabilities() & HX_OS_CAP_TEXT_LOOKUP)) {
		CHECK(hx_os_text_lookup_get_view(NULL, fallback, &view, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!strcmp(view.data, "preserved"));
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);

		return;
	}

	CHECK(hx_os_text_lookup_create(context, &lookup, NULL) == HX_OS_OK);
	CHECK(hx_os_text_lookup_add(lookup, "hello", "你好😀", NULL) ==
		HX_OS_OK);

	before = memory.calls;
	memory.fail = before + 1;
	CHECK(hx_os_text_lookup_get_view(lookup, "hello", &view, NULL) ==
		HX_OS_OK);
	CHECK(view.size == strlen("你好😀") && !strcmp(view.data, "你好😀"));
	CHECK(hx_os_text_lookup_get_view(lookup, "\xff", &view, NULL) ==
			HX_OS_INVALID_ENCODING &&
		!strcmp(view.data, "你好😀"));
	CHECK(hx_os_text_lookup_get_view(lookup, fallback, &view, NULL) ==
			HX_OS_OK &&
		view.data == fallback && memory.calls == before);

	memory.fail = 0;

	hx_os_text_lookup_destroy(&lookup);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!memory.live);
}

typedef struct directory_visit_state {
	size_t count;
	hx_os_status status;
} directory_visit_state;

static hx_os_status HX_OS_CALL directory_name(
	hx_string_view name, void *user, hx_os_error *error)
{
	directory_visit_state *state = user;

	CHECK(name.size && name.data[name.size] == 0 &&
		error->status == HX_OS_OK);

	++state->count;
	if (state->status == HX_OS_CANCELLED) {
		error->domain = HX_OS_ERROR_ERRNO;
		error->native_code = 123;
		error->cause_code = 456;
	}

	return state->status;
}

static void test_directory_visit(void)
{
	char root[80], first[96], second[96];
#if defined(_WIN32)
	char long_name[512];
#endif
	test_memory memory = {0, 0, 0};

	hx_os_context *context = test_context(&memory);

	directory_visit_state state = {0, HX_OS_OK};
	hx_os_file *file = NULL;
	hx_os_error error;
	size_t completed, baseline = memory.live, failure;
	int succeeded = 0;

	snprintf(root, sizeof(root), "hx-visit-%llx",
		(unsigned long long)hx_os_monotonic_ns());
	snprintf(first, sizeof(first), "%s/中文😀.txt", root);
	snprintf(second, sizeof(second), "%s/second.txt", root);
	CHECK(hx_os_directory_create(root, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_visit(root, directory_name, &state, NULL) ==
			HX_OS_OK &&
		!state.count);
	CHECK(hx_os_file_open(NULL, first,
		      HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
			      HX_OS_FILE_EXCLUSIVE,
		      &file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_visit_ex(context, root, 1, directory_name, &state,
		      &completed, NULL) == HX_OS_OK &&
		completed == 1);
	CHECK(hx_os_file_open(NULL, second,
		      HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
			      HX_OS_FILE_EXCLUSIVE,
		      &file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);

	state.count = 0;
	CHECK(hx_os_directory_visit_ex(context, root, 1, directory_name, &state,
		      &completed, NULL) == HX_OS_LIMIT &&
		completed == 1 && state.count == 1);

	state.status = HX_OS_EOF;
	CHECK(hx_os_directory_visit_ex(context, root, 2, directory_name, &state,
		      &completed, NULL) == HX_OS_OK &&
		completed == 1);

	state.status = HX_OS_CANCELLED;
	CHECK(hx_os_directory_visit_ex(context, root, 2, directory_name, &state,
		      &completed, &error) == HX_OS_CANCELLED &&
		completed == 1);
	CHECK(error.native_code == 123 && error.cause_code == 456);

	state.status = HX_OS_OK;

	for (failure = 1; failure < 12; ++failure) {
		hx_os_status status;

		memory.fail = memory.calls + failure;

		status = hx_os_directory_visit_ex(context, root, 2,
			directory_name, &state, &completed, &error);
		CHECK(memory.live == baseline);
		if (status == HX_OS_OK) {
			CHECK(completed == 2);

			succeeded = 1;
			break;
		}

		CHECK(status == HX_OS_NO_MEMORY);
	}

	CHECK(succeeded);

	memory.fail = 0;
	CHECK(hx_os_file_remove(first, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(second, NULL) == HX_OS_OK);
#if defined(_WIN32)
	{
		size_t used, i;

		used = (size_t)snprintf(
			long_name, sizeof(long_name), "%s/", root);

		for (i = 0; i < 80; ++i) {
			memcpy(long_name + used, "😀", 4);
			used += 4;
		}

		memcpy(long_name + used, ".txt", 5);
		CHECK(hx_os_file_open(NULL, long_name,
			      HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
				      HX_OS_FILE_EXCLUSIVE,
			      &file, NULL) == HX_OS_OK);
		CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
		CHECK(hx_os_directory_visit_ex(context, root, 1, directory_name,
			      &state, &completed, NULL) == HX_OS_OK &&
			completed == 1);
		CHECK(hx_os_file_remove(long_name, NULL) == HX_OS_OK);
	}
#endif
	CHECK(hx_os_directory_remove(root, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!memory.live);
}

int main(void)
{
	test_string_in_place();
	test_calldata_edit();
	test_settings_updates();
	test_lookup_view();
	test_directory_visit();

	return 0;
}
