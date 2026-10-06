#include <hx_os.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	const char *path = "preferences.ini";
	hx_os_config_document *document = NULL;
	hx_os_config_error error;
	hx_os_status status;
	int restore = argc == 2 && !strcmp(argv[1], "--restore");

	if (restore)
		status = hx_os_config_restore_backup(NULL, HX_OS_CONFIG_INI,
			path, NULL, &document, NULL, &error);
	else {
		status = hx_os_ini_create(NULL, &document, &error);
		if (status == HX_OS_OK)
			status = hx_os_ini_set(&document, "network", "host",
				"localhost", &error);
		if (status == HX_OS_OK)
			status = hx_os_ini_set(
				&document, "network", "port", "8080", &error);
		if (status == HX_OS_OK)
			status = hx_os_config_save_backup(document,
				HX_OS_CONFIG_INI, path, NULL, NULL, &error);
	}

	if (status == HX_OS_OK)
		printf("%s %s\n", restore ? "Restored" : "Saved", path);
	else
		fprintf(stderr, "%s: %s (%s)\n", error.source,
			hx_os_status_name(status), error.message);

	hx_os_config_release(document);

	return status == HX_OS_OK ? 0 : 1;
}
