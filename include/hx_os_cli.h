#ifndef HX_C_PLATFORM_CLI_H
#define HX_C_PLATFORM_CLI_H

#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_cli hx_os_cli;

enum { HX_OS_CLI_FLAG, HX_OS_CLI_VALUE };

typedef struct hx_os_cli_option {
	const char *name;
	const char *description;
	unsigned char short_name, kind, required;
} hx_os_cli_option;

/* argv[0] is the program name. Supports --name=value, --name value,
 * -abc flags, -pvalue, -p value and -- positional separator. Repeated
 * options retain every occurrence; value() returns the last. Positional
 * values use name=NULL. Unknown/missing options return SYNTAX with argv
 * index in cause_code. No environment mutation or process exit.
 * At most 256 specs, 4096 arguments and 1 MiB copied UTF-8 text. Inputs are
 * borrowed through parse; the result owns copies and supports concurrent
 * readers. Views are borrowed until destroy; string outputs are copied.
 */
HX_OS_API int HX_OS_CALL hx_os_cli_available(void);
HX_OS_API hx_os_status HX_OS_CALL hx_os_cli_parse(hx_os_context *context,
	int argc, const char *const *argv, const hx_os_cli_option *options,
	size_t count, hx_os_cli **out, hx_os_error *error);
/* Windows reads the actual UTF-16 command line and converts to UTF-8;
 * other systems use argc/argv. Preferred entry for a normal main(). */
HX_OS_API hx_os_status HX_OS_CALL hx_os_cli_parse_process(
	hx_os_context *context, int argc, const char *const *argv,
	const hx_os_cli_option *options, size_t count, hx_os_cli **out,
	hx_os_error *error);
HX_OS_API size_t HX_OS_CALL hx_os_cli_count(
	const hx_os_cli *cli, const char *name);
HX_OS_API hx_os_status HX_OS_CALL hx_os_cli_value_at(const hx_os_cli *cli,
	const char *name, size_t index, hx_string_view *out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_cli_value(const hx_os_cli *cli,
	const char *name, hx_string *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_cli_i64(const hx_os_cli *cli,
	const char *name, int64_t *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_cli_help(const char *program,
	const hx_os_cli_option *options, size_t count, hx_string *out,
	hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_cli_destroy(hx_os_cli **cli);

#ifdef __cplusplus
}
#endif
#endif
