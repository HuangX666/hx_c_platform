#include <hx_os.h>
#include <stdio.h>
#include <string.h>

static void usage(void)
{
	puts("Usage: hx_os_local_dumps query|enable|remove APP.exe "
	     "[native|32|64] [FOLDER]");
	puts("APP.exe is a basename; '-' explicitly selects this example's "
	     "EXE.");
	puts("enable/remove change persistent HKLM settings and normally need "
	     "an administrator.");
	puts("Defaults: full dumps, 3 files, %LOCALAPPDATA%\\CrashDumps. "
	     "No crash is triggered.");
	puts("remove requires the same view/folder as enable. "
	     "Foreign or edited keys are preserved.");
}

static int report(hx_os_status status, const hx_os_error *error)
{
	char message[2048];
	size_t required;

	if (hx_os_error_message(error, message, sizeof(message), &required) ==
		HX_OS_OK)
		fprintf(stderr, "%s\n", message);
	else
		fprintf(stderr, "%s\n", hx_os_status_name(status));
	if (error->cause_code)
		fprintf(stderr, "Cleanup Win32 code: %lld; inspect the key.\n",
			(long long)error->cause_code);
	if (status == HX_OS_ACCESS_DENIED)
		fputs("Use an authorized elevated installer/terminal. "
		      "The library never elevates itself.\n",
			stderr);

	return 1;
}

static int run(hx_os_string_list *arguments)
{
	const char *action = hx_os_string_list_at(arguments, 1);
	const char *application = hx_os_string_list_at(arguments, 2);
	const char *view = hx_os_string_list_at(arguments, 3);
	const char *directory = hx_os_string_list_at(arguments, 4);
	hx_os_local_dumps_options options;
	hx_os_local_dumps_info info;
	hx_os_error error;
	hx_string folder = HX_STRING_INIT;
	hx_os_status status;

	if (!action) {
		usage();

		return 0;
	}

	if (!application || hx_os_string_list_count(arguments) > 5 ||
		(strcmp(action, "query") && strcmp(action, "enable") &&
			strcmp(action, "remove")) ||
		(!strcmp(action, "query") && directory)) {
		usage();

		return 1;
	}

	(void)hx_os_local_dumps_options_init(&options, sizeof(options));
	if (view) {
		if (!strcmp(view, "32"))
			options.registry_view = HX_OS_LOCAL_DUMPS_VIEW32;
		else if (!strcmp(view, "64"))
			options.registry_view = HX_OS_LOCAL_DUMPS_VIEW64;
		else if (strcmp(view, "native")) {
			usage();

			return 1;
		}
	}

	options.dump_folder = directory;
	if (!strcmp(application, "-"))
		application = NULL;

	if (!strcmp(action, "query")) {
		status = hx_os_local_dumps_query(application,
			options.registry_view, &info, sizeof(info), &folder,
			&error);
		if (status == HX_OS_OK) {
			printf("Present fields: %u; type: %u; count: %u; "
			       "library-managed: %u\n",
				(unsigned)info.fields, (unsigned)info.dump_type,
				(unsigned)info.dump_count,
				(unsigned)info.managed);
			printf("Folder (not expanded): %s\n",
				hx_string_c_str(&folder));
		}
		else if (status == HX_OS_NOT_FOUND)
			puts("No application-specific key. "
			     "Global Windows settings may still apply.");

		hx_string_destroy(&folder);
		if (status == HX_OS_OK || status == HX_OS_NOT_FOUND)
			return 0;
	}
	else {
		status = !strcmp(action, "enable")
			? hx_os_local_dumps_enable(
				  application, &options, &error)
			: hx_os_local_dumps_remove(
				  application, &options, &error);
		if (status == HX_OS_OK) {
			puts(!strcmp(action, "enable")
					? "Configuration installed/already "
					  "identical. "
					  "Verify actual crash collection "
					  "separately."
					: "Matching configuration "
					  "removed/already absent. "
					  "Global Windows settings may still "
					  "apply.");

			return 0;
		}
	}

	return report(status, &error);
}

int main(void)
{
	hx_os_string_list *arguments = NULL;
	hx_os_error error;
	hx_os_status status;
	int result;

	if (!hx_os_local_dumps_available()) {
		puts("WER LocalDumps configuration is unavailable in this "
		     "build.");

		return 0;
	}

	status = hx_os_process_arguments(NULL, &arguments, &error);
	if (status != HX_OS_OK)
		return report(status, &error);

	result = run(arguments);
	hx_os_string_list_destroy(&arguments);

	return result;
}
