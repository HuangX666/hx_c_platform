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
typedef struct change_state {
	hx_settings *settings;
	hx_os_event *entered, *release;
	hx_os_atomic callbacks, cleanups;
	int mode;
} change_state;

static void HX_OS_CALL on_change(
	hx_settings *settings, const hx_settings_change *change, void *user)
{
	change_state *state = user;
	hx_settings *same = settings;
	int64_t value, current;

	(void)hx_os_atomic_fetch_add(&state->callbacks, 1, HX_OS_RELAXED);
	CHECK(change->kind == HX_SETTINGS_SET && change->after);
	CHECK(hx_settings_value_get_i64(change->after, &value, NULL) ==
		HX_OS_OK);
	if (state->mode == 1) {
		CHECK(hx_settings_destroy(&same, NULL) == HX_OS_DEADLOCK &&
			same == settings);
		CHECK(hx_settings_get_i64(settings, "n", &current, NULL) ==
				HX_OS_OK &&
			current == value);
		if (value == 0)
			CHECK(hx_settings_set_i64(settings, "n", 1, NULL) ==
				HX_OS_OK);
		else
			CHECK(hx_settings_set_i64(settings, "n", 2, NULL) ==
				HX_OS_LIMIT);

		/* The event still owns the original value after nested
		 * publication. */
		CHECK(hx_settings_value_get_i64(
			      change->after, &current, NULL) == HX_OS_OK &&
			current == value);
	}
	else if (state->mode == 2) {
		hx_os_event_set(state->entered);
		CHECK(hx_os_event_wait_until(state->release,
			      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	}
}

static void HX_OS_CALL on_cleanup(void *user)
{
	change_state *state = user;
	int64_t value;
	hx_settings *same = state->settings;

	CHECK(hx_settings_get_i64(state->settings, "n", &value, NULL) ==
		HX_OS_CLOSED);
	CHECK(hx_settings_destroy(&same, NULL) == HX_OS_DEADLOCK);

	(void)hx_os_atomic_fetch_add(&state->cleanups, 1, HX_OS_RELAXED);
}

static void *set_blocked(void *user)
{
	change_state *state = user;

	CHECK(hx_settings_set_i64(state->settings, "n", 42, NULL) == HX_OS_OK);

	return NULL;
}

typedef struct writer_state {
	hx_settings *settings;
	int index;
} writer_state;

static void *write_many(void *user)
{
	writer_state *state = user;
	char key[16];
	int i;

	(void)snprintf(key, sizeof(key), "thread_%d", state->index);

	for (i = 0; i < 150; ++i) {
		hx_os_status status;
		unsigned retries = 0;
		hx_settings_value *snapshot = NULL, *value = NULL;
		int64_t stored;

		do {
			status = hx_settings_set_i64(
				state->settings, key, i, NULL);
			CHECK(++retries <= 2000);
		} while (status == HX_OS_BUSY);
		CHECK(status == HX_OS_OK);
		CHECK(hx_settings_snapshot(state->settings,
			      HX_SETTINGS_EXPLICIT, &snapshot,
			      NULL) == HX_OS_OK);
		CHECK(hx_settings_value_member(snapshot,
			      hx_string_view_from_cstr(key), &value,
			      NULL) == HX_OS_OK);
		CHECK(hx_settings_value_get_i64(value, &stored, NULL) ==
				HX_OS_OK &&
			stored == i);

		hx_settings_value_release(value);
		hx_settings_value_release(snapshot);
	}

	return NULL;
}

int main(void)
{
	change_state state;
	hx_settings_options options = HX_SETTINGS_OPTIONS_INIT;
	hx_os_thread *thread = NULL, *writers[4];
	writer_state jobs[4];
	int i;
	int64_t value;

	if (!(hx_os_capabilities() & HX_OS_CAP_SETTINGS))
		return 0;

	memset(&state, 0, sizeof(state));
	options.on_change = on_change;
	options.cleanup = on_cleanup;
	options.user = &state;
	options.max_callback_depth = 2;
	state.mode = 1;
	CHECK(hx_settings_create_with_options(
		      NULL, &options, &state.settings, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_i64(state.settings, "n", 0, NULL) == HX_OS_OK);
	CHECK(hx_settings_get_i64(state.settings, "n", &value, NULL) ==
			HX_OS_OK &&
		value == 1);
	CHECK(hx_os_atomic_load(&state.callbacks, HX_OS_ACQUIRE) == 2);
	CHECK(hx_settings_destroy(&state.settings, NULL) == HX_OS_OK &&
		hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 1);

	memset(&state, 0, sizeof(state));
	state.mode = 2;
	CHECK(hx_settings_create_with_options(
		      NULL, &options, &state.settings, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.entered, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.release, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_create(NULL, set_blocked, &state, &thread, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.entered,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	CHECK(hx_settings_close_until(&state.settings, HX_OS_DEADLINE_NOW,
		      NULL) == HX_OS_TIMEOUT &&
		state.settings);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 0);
	CHECK(hx_settings_set_i64(state.settings, "new", 1, NULL) ==
		HX_OS_CLOSED);

	hx_os_event_set(state.release);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(2000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	CHECK(hx_settings_destroy(&state.settings, NULL) == HX_OS_OK &&
		hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 1);

	hx_os_event_destroy(state.entered);
	hx_os_event_destroy(state.release);
	memset(&state, 0, sizeof(state));
	options.cleanup = NULL;
	options.max_retries = 2;
	CHECK(hx_settings_create_with_options(
		      NULL, &options, &state.settings, NULL) == HX_OS_OK);

	for (i = 0; i < 4; ++i) {
		jobs[i].settings = state.settings;
		jobs[i].index = i;
		CHECK(hx_os_thread_create(NULL, write_many, &jobs[i],
			      &writers[i], NULL) == HX_OS_OK);
	}

	for (i = 0; i < 4; ++i) {
		char key[16];

		CHECK(hx_os_thread_join_until(writers[i],
			      hx_os_deadline_after_ms(5000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&writers[i], NULL) == HX_OS_OK);

		(void)snprintf(key, sizeof(key), "thread_%d", i);
		CHECK(hx_settings_get_i64(state.settings, key, &value, NULL) ==
				HX_OS_OK &&
			value == 149);
	}

	CHECK(hx_os_atomic_load(&state.callbacks, HX_OS_ACQUIRE) == 600);
	CHECK(hx_settings_destroy(&state.settings, NULL) == HX_OS_OK);

	return 0;
}
