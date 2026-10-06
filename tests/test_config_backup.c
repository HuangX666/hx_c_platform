#include "utility_test.h"

static void check_contents(const char *path, const char *expected)
{
	hx_string contents = HX_STRING_INIT;

	CHECK(hx_os_file_read_text(path, &contents, NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&contents), expected));
	hx_string_destroy(&contents);
}

int main(void)
{
	const uint64_t required = HX_OS_CAP_CONFIG |
		HX_OS_CAP_CONFIG_SERIALIZE | HX_OS_CAP_FILE_UTILS;
	const char *path = "hx-config-backup.json";
	const char *backup = "hx-config-backup.json.bak";
	const char *original = "{ \"value\" : 1 }\n";
	hx_os_config_document *document = NULL, *recovered = NULL;
	hx_os_config_backup_result result;
	hx_os_config_error error;
	hx_os_file_save_result publication;
	hx_os_config_options options;
	test_memory memory = {0, 0, 0};
	hx_os_context *context;
	size_t i;
	int exists;

	if ((hx_os_capabilities() & required) != required) {
		if (!(hx_os_capabilities() & HX_OS_CAP_CONFIG) ||
			!(hx_os_capabilities() & HX_OS_CAP_CONFIG_SERIALIZE) ||
			!(hx_os_capabilities() & HX_OS_CAP_FILE_UTILS))
			CHECK(hx_os_config_save_backup(NULL, HX_OS_CONFIG_JSON,
				      path, NULL, &result,
				      &error) == HX_OS_NOT_SUPPORTED);
		return 0;
	}

	context = test_context(&memory);
	CHECK(hx_os_config_parse(context, HX_OS_CONFIG_JSON, "{\"value\":2}",
		      11, NULL, &document, &error) == HX_OS_OK);
	CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK ||
		hx_os_file_exists(path, &exists, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(backup, NULL) == HX_OS_OK ||
		hx_os_file_exists(backup, &exists, NULL) == HX_OS_OK);

	CHECK(hx_os_config_save_backup(document, HX_OS_CONFIG_JSON, path, NULL,
		      &result, &error) == HX_OS_OK);
	CHECK(result.primary.published && !result.backup.published);
	CHECK(hx_os_file_exists(backup, &exists, NULL) == HX_OS_OK && !exists);
	CHECK(hx_os_file_save(path, original, strlen(original), 1, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_config_save_backup(document, HX_OS_CONFIG_JSON, path, NULL,
		      &result, &error) == HX_OS_OK);
	CHECK(result.backup.published && result.primary.published);
	check_contents(backup, original);
	check_contents(path, "{\"value\":2}");

	CHECK(hx_os_file_save(path, "broken", 6, 1, NULL) == HX_OS_OK);
	CHECK(hx_os_config_load(context, HX_OS_CONFIG_JSON, path, NULL,
		      &recovered, &error) == HX_OS_SYNTAX &&
		!recovered);
	check_contents(path, "broken");
	CHECK(hx_os_config_save_backup(document, HX_OS_CONFIG_JSON, path, NULL,
		      &result, &error) == HX_OS_SYNTAX);
	CHECK(!result.backup.published && !result.primary.published);
	check_contents(backup, original);
	CHECK(hx_os_config_restore_backup(context, HX_OS_CONFIG_JSON, path,
		      NULL, &recovered, &publication, &error) == HX_OS_OK);
	CHECK(recovered && publication.published);
	check_contents(path, original);
	check_contents(backup, original);
	hx_os_config_release(recovered);
	recovered = NULL;

	CHECK(hx_os_file_save(backup, "bad backup", 10, 1, NULL) == HX_OS_OK);
	CHECK(hx_os_config_restore_backup(context, HX_OS_CONFIG_JSON, path,
		      NULL, &recovered, &publication, &error) == HX_OS_SYNTAX);
	CHECK(!recovered && !publication.published);
	check_contents(path, original);
	CHECK(hx_os_file_save(backup, original, strlen(original), 1, NULL) ==
		HX_OS_OK);
	hx_os_config_options_init(&options);
	options.max_input_bytes = 4;
	CHECK(hx_os_config_restore_backup(context, HX_OS_CONFIG_JSON, path,
		      &options, &recovered, &publication,
		      &error) == HX_OS_LIMIT);
	CHECK(!recovered && !publication.published);
	check_contents(path, original);

	/* Every injected failure precedes publication or leaves a valid backup.
	 */
	for (i = 1; i < 15; ++i) {
		hx_os_status status;
		size_t live = memory.live;

		memory.fail = memory.calls + i;
		status = hx_os_config_restore_backup(context, HX_OS_CONFIG_JSON,
			path, NULL, &recovered, &publication, &error);
		memory.fail = 0;
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		CHECK(publication.published == (status == HX_OS_OK));
		hx_os_config_release(recovered);
		recovered = NULL;
		CHECK(memory.live == live);
		check_contents(path, original);
	}

	CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(backup, NULL) == HX_OS_OK);
	hx_os_config_release(document);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!memory.live);
	puts("configuration backup and explicit recovery passed");

	return 0;
}
