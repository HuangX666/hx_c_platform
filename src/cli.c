#include "internal.h"
#include "hx_os_cli.h"
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

typedef struct hx_os_cli_entry {
	size_t option;
	hx_string_view value;
} hx_os_cli_entry;

struct hx_os_cli {
	char **names;
	hx_os_cli_entry *entries;
	char *storage;
	size_t options, count, capacity, used;
};

int hx_os_cli_available(void)
{
	return HX_OS_ENABLE_CLI;
}

static int hx_os_cli_name(const char *name)
{
	size_t i;

	if (!name || !*name)
		return 0;
	for (i = 0; name[i]; ++i) {
		unsigned char ch = (unsigned char)name[i];

		if (i >= 64 ||
			!((ch >= 'a' && ch <= 'z') ||
				(ch >= 'A' && ch <= 'Z') ||
				(ch >= '0' && ch <= '9') || ch == '_' ||
				ch == '-'))
			return 0;
	}
	return 1;
}

static hx_os_status hx_os_cli_specs(
	const hx_os_cli_option *options, size_t count, hx_os_error *error)
{
	size_t i, j;

	if (count > 256 || (!options && count))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	for (i = 0; i < count; ++i) {
		unsigned char ch = options[i].short_name;

		if (!hx_os_cli_name(options[i].name) ||
			options[i].kind > HX_OS_CLI_VALUE ||
			options[i].required > 1 ||
			(ch &&
				!((ch >= 'a' && ch <= 'z') ||
					(ch >= 'A' && ch <= 'Z') ||
					(ch >= '0' && ch <= '9'))))
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		for (j = 0; j < i; ++j)
			if (!strcmp(options[j].name, options[i].name) ||
				(ch && options[j].short_name == ch))
				return hx_os_i_error(error, HX_OS_EXISTS, 0, 0);
	}
	return HX_OS_OK;
}

static size_t hx_os_cli_lookup(const hx_os_cli_option *options, size_t count,
	const char *name, size_t bytes, unsigned char short_name)
{
	size_t i;

	for (i = 0; i < count; ++i)
		if (short_name ? options[i].short_name == short_name
			       : strlen(options[i].name) == bytes &&
					!memcmp(options[i].name, name, bytes))
			return i;
	return SIZE_MAX;
}

static char *hx_os_cli_copy(hx_os_cli *cli, const char *text)
{
	size_t bytes = strlen(text) + 1;
	char *copy = cli->storage + cli->used;

	memcpy(copy, text, bytes);
	cli->used += bytes;
	return copy;
}

static hx_os_status hx_os_cli_push(
	hx_os_cli *cli, size_t option, const char *value, hx_os_error *error)
{
	if (cli->count == cli->capacity)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	cli->entries[cli->count].option = option;
	cli->entries[cli->count].value = hx_string_view_from_cstr(value);
	++cli->count;
	return HX_OS_OK;
}

static hx_os_status hx_os_cli_arguments(hx_os_cli *cli, int argc,
	const char *const *argv, const hx_os_cli_option *options,
	hx_os_error *error)
{
	int i, positional = 0;
	hx_os_status status = HX_OS_OK;

	for (i = 1; i < argc && status == HX_OS_OK; ++i) {
		char *argument = hx_os_cli_copy(cli, argv[i]);

		if (!positional && !strcmp(argument, "--"))
			positional = 1;
		else if (positional || argument[0] != '-' || !argument[1])
			status = hx_os_cli_push(cli, SIZE_MAX, argument, error);
		else if (argument[1] == '-') {
			char *equal = strchr(argument + 2, '=');
			size_t length = equal ? (size_t)(equal - argument - 2)
					      : strlen(argument + 2);
			size_t index = hx_os_cli_lookup(
				options, cli->options, argument + 2, length, 0);
			const char *value = "true";

			if (index == SIZE_MAX ||
				(equal && !options[index].kind))
				status = HX_OS_SYNTAX;
			else if (options[index].kind) {
				if (equal)
					value = equal + 1;
				else if (i + 1 < argc)
					value = hx_os_cli_copy(cli, argv[++i]);
				else
					status = HX_OS_SYNTAX;
			}
			if (status == HX_OS_OK)
				status = hx_os_cli_push(
					cli, index, value, error);
		}
		else {
			char *cursor = argument + 1;

			while (*cursor && status == HX_OS_OK) {
				size_t index = hx_os_cli_lookup(options,
					cli->options, NULL, 0,
					(unsigned char)*cursor++);
				const char *value = "true";

				if (index == SIZE_MAX)
					status = HX_OS_SYNTAX;
				else if (options[index].kind) {
					if (*cursor)
						value = cursor;
					else if (i + 1 < argc)
						value = hx_os_cli_copy(
							cli, argv[++i]);
					else
						status = HX_OS_SYNTAX;
					cursor += strlen(cursor);
				}
				if (status == HX_OS_OK)
					status = hx_os_cli_push(
						cli, index, value, error);
			}
		}
	}
	if (status != HX_OS_OK) {
		hx_os_i_error(error, status, 0, 0);
		if (error)
			error->cause_code = i > 1 ? i - 1 : 0;
	}
	return status;
}

hx_os_status hx_os_cli_parse(hx_os_context *context, int argc,
	const char *const *argv, const hx_os_cli_option *options, size_t count,
	hx_os_cli **out, hx_os_error *error)
{
	hx_os_cli *cli;
	hx_os_status status;
	size_t bytes = 0, offset, i;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!HX_OS_ENABLE_CLI)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (argc < 0 || argc > 4096 || (argc && !argv))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_cli_specs(options, count, error);
	if (status != HX_OS_OK)
		return status;
	for (i = 0; i < count; ++i)
		bytes += strlen(options[i].name) + 1;
	for (i = 0; i < (size_t)argc; ++i) {
		size_t length;

		if (!argv[i])
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		length = strlen(argv[i]);
		if (length > 1024 * 1024 - bytes)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		if (hx_os_text_validate_utf8(argv[i], length, &offset) !=
			HX_OS_OK)
			return hx_os_i_error(
				error, HX_OS_INVALID_ENCODING, 0, 0);
		bytes += length + 1;
		if (bytes > 1024 * 1024)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}
	cli = hx_os_i_zero(context, sizeof(*cli));
	if (!cli)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	cli->options = count;
	cli->capacity = 0;
	for (i = 1; i < (size_t)argc; ++i) {
		size_t entries =
			argv[i][0] == '-' && argv[i][1] && argv[i][1] != '-'
			? strlen(argv[i]) - 1
			: 1;

		if (entries > 4096 - cli->capacity) {
			cli->capacity = 4096;
			break;
		}
		cli->capacity += entries;
	}
	cli->names =
		hx_os_i_alloc(context, (count ? count : 1) * sizeof(char *), 0);
	cli->entries = hx_os_i_alloc(context,
		(cli->capacity ? cli->capacity : 1) * sizeof(hx_os_cli_entry),
		0);
	cli->storage = hx_os_i_alloc(context, bytes ? bytes : 1, 0);
	if (!cli->names || !cli->entries || !cli->storage) {
		hx_os_cli_destroy(&cli);
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	for (i = 0; i < count; ++i)
		cli->names[i] = hx_os_cli_copy(cli, options[i].name);
	status = hx_os_cli_arguments(cli, argc, argv, options, error);
	for (i = 0; status == HX_OS_OK && i < count; ++i)
		if (options[i].required &&
			!hx_os_cli_count(cli, options[i].name)) {
			status = hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
			if (error)
				error->cause_code = argc;
		}
	if (status != HX_OS_OK) {
		hx_os_cli_destroy(&cli);
		return status;
	}
	*out = cli;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static size_t hx_os_cli_index(const hx_os_cli *cli, const char *name)
{
	size_t i;

	if (name)
		for (i = 0; i < cli->options; ++i)
			if (!strcmp(name, cli->names[i]))
				return i;
	return SIZE_MAX;
}

size_t hx_os_cli_count(const hx_os_cli *cli, const char *name)
{
	size_t index, count = 0, i;

	if (!cli || (name && hx_os_cli_index(cli, name) == SIZE_MAX))
		return 0;
	index = hx_os_cli_index(cli, name);
	for (i = 0; i < cli->count; ++i)
		if (cli->entries[i].option == index)
			++count;
	return count;
}

hx_os_status hx_os_cli_value_at(const hx_os_cli *cli, const char *name,
	size_t index, hx_string_view *out, hx_os_error *error)
{
	size_t option, i;

	if (!HX_OS_ENABLE_CLI)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!cli || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	option = hx_os_cli_index(cli, name);
	if (name && option == SIZE_MAX)
		return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
	for (i = 0; i < cli->count; ++i)
		if (cli->entries[i].option == option && !index--) {
			*out = cli->entries[i].value;
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		}
	return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
}

hx_os_status hx_os_cli_value(const hx_os_cli *cli, const char *name,
	hx_string *out, hx_os_error *error)
{
	hx_string_view view;
	size_t count = hx_os_cli_count(cli, name);
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_cli_value_at(
		cli, name, count ? count - 1 : 0, &view, error);
	return status == HX_OS_OK ? hx_string_assign(out, view, error) : status;
}

hx_os_status hx_os_cli_i64(const hx_os_cli *cli, const char *name, int64_t *out,
	hx_os_error *error)
{
	hx_string_view view;
	size_t count = hx_os_cli_count(cli, name), i = 0;
	uint64_t value = 0, maximum;
	int negative;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_cli_value_at(
		cli, name, count ? count - 1 : 0, &view, error);
	if (status != HX_OS_OK)
		return status;
	negative = view.size && view.data[0] == '-';
	if (negative || (view.size && view.data[0] == '+'))
		++i;
	if (i == view.size)
		return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
	maximum = (uint64_t)INT64_MAX + (unsigned)negative;
	for (; i < view.size; ++i) {
		unsigned digit = (unsigned char)view.data[i] - '0';

		if (digit > 9)
			return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
		if (value > (maximum - digit) / 10)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
		value = value * 10 + digit;
	}
	*out = negative ? -(int64_t)(value - !!value) - !!value
			: (int64_t)value;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_cli_help(const char *program,
	const hx_os_cli_option *options, size_t count, hx_string *out,
	hx_os_error *error)
{
	hx_string result;
	hx_os_status status;
	size_t i;

	if (!HX_OS_ENABLE_CLI)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!program || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_cli_specs(options, count, error);
	if (status != HX_OS_OK)
		return status;
	hx_string_init(&result, out->context);
	status = hx_string_format(
		&result, error, "Usage: %s [options]\n", program);
	for (i = 0; status == HX_OS_OK && i < count; ++i) {
		char short_name[5] = "    ";

		if (options[i].short_name) {
			short_name[0] = '-';
			short_name[1] = (char)options[i].short_name;
			short_name[2] = ',';
		}
		status = hx_string_append_format(&result, error,
			"  %s --%s%s%s  %s\n", short_name, options[i].name,
			options[i].kind ? " VALUE" : "",
			options[i].required ? " (required)" : "",
			options[i].description ? options[i].description : "");
	}
	if (status == HX_OS_OK)
		hx_string_move(out, &result);
	hx_string_destroy(&result);
	return status;
}

void hx_os_cli_destroy(hx_os_cli **cli)
{
	hx_os_cli *value;

	if (!cli || !*cli)
		return;
	value = *cli;
	*cli = NULL;
	hx_os_memory_free(value->names);
	hx_os_memory_free(value->entries);
	hx_os_memory_free(value->storage);
	hx_os_memory_free(value);
}

hx_os_status hx_os_cli_parse_process(hx_os_context *context, int argc,
	const char *const *argv, const hx_os_cli_option *options, size_t count,
	hx_os_cli **out, hx_os_error *error)
{
#ifdef _WIN32
	wchar_t **wide;
	char **utf8;
	hx_os_status status = HX_OS_OK;
	int i, native_count;

	(void)argc;
	(void)argv;
	if (!HX_OS_ENABLE_CLI)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	wide = CommandLineToArgvW(GetCommandLineW(), &native_count);
	if (!wide)
		return hx_os_i_win32(error, GetLastError());
	if (native_count > 4096) {
		LocalFree(wide);
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}
	utf8 = hx_os_i_zero(context, (size_t)native_count * sizeof(*utf8));
	if (!utf8) {
		LocalFree(wide);
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	for (i = 0; i < native_count; ++i) {
		int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			wide[i], -1, NULL, 0, NULL, NULL);

		if (!bytes || bytes > 1024 * 1024) {
			status = hx_os_i_error(error,
				bytes ? HX_OS_LIMIT : HX_OS_INVALID_ENCODING, 0,
				0);
			break;
		}
		utf8[i] = hx_os_i_alloc(context, (size_t)bytes, 0);
		if (!utf8[i]) {
			status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
			break;
		}
		if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i],
			    -1, utf8[i], bytes, NULL, NULL)) {
			status = hx_os_i_win32(error, GetLastError());
			break;
		}
	}
	if (status == HX_OS_OK)
		status = hx_os_cli_parse(context, native_count,
			(const char *const *)utf8, options, count, out, error);
	for (i = 0; i < native_count; ++i)
		hx_os_memory_free(utf8[i]);
	hx_os_memory_free(utf8);
	LocalFree(wide);
	return status;
#else
	return hx_os_cli_parse(context, argc, argv, options, count, out, error);
#endif
}
