#ifndef HX_C_PLATFORM_FORMAT_SYNTAX_H
#define HX_C_PLATFORM_FORMAT_SYNTAX_H
#include "hx_format.h"

/* Enable these source markers with hx_enable_format_syntax(target SOURCES ...).
 * The generator preserves source locations and writes only in the build tree.
 * Plain C99 users can always call hx_format/hx_print_args without this tool.
 */
#define HX_FORMAT_SYNTAX_REQUIRED                                              \
	((hx_os_status)sizeof(struct hx_enable_format_syntax_required))
#define HX_PRINT(...) HX_FORMAT_SYNTAX_REQUIRED
#define HX_PRINTLN(...) HX_FORMAT_SYNTAX_REQUIRED
#define HX_EPRINTLN(...) HX_FORMAT_SYNTAX_REQUIRED
#define HX_FORMAT(...) HX_FORMAT_SYNTAX_REQUIRED
#define HX_FPRINT(...) HX_FORMAT_SYNTAX_REQUIRED
#define HX_FPRINTLN(...) HX_FORMAT_SYNTAX_REQUIRED
#define HX_FFORMAT(...) HX_FORMAT_SYNTAX_REQUIRED
/* Static custom type registration for one translation unit; the callback has
 * hx_format_custom_fn's signature. No process-global state is registered.
 */
#define HX_FORMATTER(type, function)
#define HX_NAMED(name, value) HX_FORMAT_SYNTAX_REQUIRED
#endif
