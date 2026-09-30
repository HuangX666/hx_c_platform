#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "empty";
#if defined(_WIN32)
	(void)_setmode(_fileno(stdout), _O_BINARY);
	(void)_setmode(_fileno(stderr), _O_BINARY);
#endif
	if (!strcmp(mode, "arguments")) {
		hx_os_string_list *arguments = NULL;
		size_t i;
		if (hx_os_process_arguments(NULL, &arguments, NULL) != HX_OS_OK)
			return 77;
		for (i = 2; i < hx_os_string_list_count(arguments); ++i) {
			const char *argument =
				hx_os_string_list_at(arguments, i);
			printf("%lu:", (unsigned long)strlen(argument));
			fwrite(argument, 1, strlen(argument), stdout);
			fputc('\n', stdout);
		}
		hx_os_string_list_destroy(&arguments);
	}
	else if (!strcmp(mode, "bulk")) {
		char data[4096];
		int i;
		for (i = 0; i < 80; ++i) {
			memset(data, 'a' + i % 26, sizeof(data));
			if (fwrite(data, 1, sizeof(data), stdout) !=
					sizeof(data) ||
				fflush(stdout) ||
				fwrite(data, 1, sizeof(data), stderr) !=
					sizeof(data) ||
				fflush(stderr))
				return 90;
		}
	}
	else if (!strcmp(mode, "binary")) {
		const unsigned char data[] = {0xff, 0, 0x41};
		fwrite(data, 1, sizeof(data), stdout);
	}
	else if (!strcmp(mode, "environment")) {
		hx_string value = HX_STRING_INIT;
		if (hx_os_env_get_string("HX_PROCESS_TEST", &value, NULL) !=
			HX_OS_OK)
			return 91;
		fwrite(hx_string_c_str(&value), 1, value.size, stdout);
		hx_string_destroy(&value);
	}
	else if (!strcmp(mode, "delay")) {
		if (hx_os_sleep_until(hx_os_deadline_after_ms(200), NULL) !=
			HX_OS_OK)
			return 92;
		fputs("finished", stdout);
		return 7;
	}
	else if (!strcmp(mode, "stdin")) {
		if (getchar() != EOF)
			return 93;
	}
	else if (!strcmp(mode, "exit259"))
		return 259;
	else if (strcmp(mode, "empty"))
		return 94;
	return fflush(stdout) || fflush(stderr) ? 95 : 0;
}
