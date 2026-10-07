#include <hx_os.h>
#include <stdio.h>
#include <string.h>
#include "example_arguments.h"

static hx_os_status HX_OS_CALL print_entry(
	const hx_os_archive_entry *entry, void *user, hx_os_error *error)
{
	(void)user;
	(void)error;
	const char *kind = entry->kind == HX_OS_ARCHIVE_DIRECTORY ? "directory"
		: entry->kind == HX_OS_ARCHIVE_FILE		  ? "file"
		: entry->kind == HX_OS_ARCHIVE_LINK		  ? "link"
								  : "other";

	printf("%s %llu %s\n", kind, (unsigned long long)entry->bytes,
		entry->path.data);
	return HX_OS_OK;
}

static int hx_example_main(int argc, const char *const *argv)
{
	hx_os_archive_options options = HX_OS_ARCHIVE_OPTIONS_INIT;
	hx_os_archive_result result;
	hx_os_error error = {0};
	hx_string diagnostic = HX_STRING_INIT, password = HX_STRING_INIT;
	hx_string vendor = HX_STRING_INIT, adapter = HX_STRING_INIT;
	hx_os_status status;

	if (argc < 3 || argc > 5) {
		fputs("Usage:\n"
		      "  archive_files pack SOURCE ARCHIVE\n"
		      "  archive_files unpack ARCHIVE DIRECTORY\n"
		      "  archive_files list ARCHIVE\n"
		      "  archive_files extract ARCHIVE ENTRY DESTINATION\n"
		      "Optional environment: HX_ARCHIVE_PASSWORD, "
		      "HX_7ZIP_LIBRARY,\n"
		      "HX_7ZIP_ADAPTER (absolute trusted runtime paths).\n",
			stderr);
		return 2;
	}
	/* Passwords do not appear in the command line or diagnostic output.
	 * A production UI can supply its own in-memory options.password. */
	status = hx_os_env_get_string("HX_ARCHIVE_PASSWORD", &password, &error);
	if (status == HX_OS_NOT_FOUND)
		status = HX_OS_OK;
	if (status == HX_OS_OK) {
		status = hx_os_env_get_string(
			"HX_7ZIP_LIBRARY", &vendor, &error);
		if (status == HX_OS_NOT_FOUND)
			status = HX_OS_OK;
	}
	if (status == HX_OS_OK) {
		status = hx_os_env_get_string(
			"HX_7ZIP_ADAPTER", &adapter, &error);
		if (status == HX_OS_NOT_FOUND)
			status = HX_OS_OK;
	}
	options.password = password.size ? hx_string_c_str(&password) : NULL;
	options.seven_zip_library =
		vendor.size ? hx_string_c_str(&vendor) : NULL;
	options.seven_zip_adapter =
		adapter.size ? hx_string_c_str(&adapter) : NULL;
	if (options.password && options.seven_zip_library)
		options.flags = HX_OS_ARCHIVE_ENCRYPT_NAMES;
	options.diagnostic = &diagnostic;
	(void)hx_os_archive_result_init(&result, sizeof(result));

	if (status == HX_OS_OK) {
		if (!strcmp(argv[1], "pack") && argc == 4)
			status = hx_os_archive_pack_ex(NULL, argv[2], argv[3],
				&options, &result, &error);
		else if (!strcmp(argv[1], "unpack") && argc == 4)
			status = hx_os_archive_unpack_ex(NULL, argv[2], argv[3],
				&options, &result, &error);
		else if (!strcmp(argv[1], "extract") && argc == 5)
			status = hx_os_archive_extract_file_ex(NULL, argv[2],
				argv[3], argv[4], &options, &result, &error);
		else if (!strcmp(argv[1], "list") && argc == 3)
			status = hx_os_archive_visit_ex(NULL, argv[2], &options,
				print_entry, NULL, &result, &error);
		else
			status = HX_OS_INVALID;
	}
	if (status == HX_OS_OK)
		printf("Completed: %llu entries, %llu content bytes, %u "
		       "published\n",
			(unsigned long long)result.entries,
			(unsigned long long)result.content_bytes,
			result.published);
	else
		fprintf(stderr, "%s: %s (published: %u)\n",
			hx_os_status_name(status), hx_string_c_str(&diagnostic),
			result.published);

	hx_os_secure_zero(password.data, password.size);
	hx_string_destroy(&password);
	hx_string_destroy(&diagnostic);
	hx_string_destroy(&vendor);
	hx_string_destroy(&adapter);
	return status == HX_OS_OK ? 0 : 1;
}
