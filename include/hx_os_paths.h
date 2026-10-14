#ifndef HX_C_PLATFORM_PATHS_H
#define HX_C_PLATFORM_PATHS_H

#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Absolute UTF-8 paths.
 *
 * \details No creation, no trailing separator except roots. required includes
 * NUL; OUTPUT_FULL empties a nonempty output buffer. String outputs use their
 * own context and retain their value on failure. Paths need not exist or be
 * writable. The host coordinates environment and cwd mutations. Module returns
 * this library's directory (executable for a static build); an
 * unavailable/unresolvable module name is NOT_SUPPORTED.
 */
#define HX_OS_DECLARE_PATH(name)                                               \
	HX_OS_API hx_os_status HX_OS_CALL hx_os_path_##name(char *buffer,      \
		size_t capacity, size_t *required, hx_os_error *error);        \
	HX_OS_API hx_os_status HX_OS_CALL hx_os_path_##name##_string(          \
		hx_string *out, hx_os_error *error)

HX_OS_DECLARE_PATH(home);
HX_OS_DECLARE_PATH(config_home);
HX_OS_DECLARE_PATH(data_home);
HX_OS_DECLARE_PATH(program_data);
HX_OS_DECLARE_PATH(temp);
HX_OS_DECLARE_PATH(executable);
HX_OS_DECLARE_PATH(module);

#undef HX_OS_DECLARE_PATH
/**
 * \brief Config root/application/name, without creating directories.
 *
 * \details Components must be nonempty UTF-8 without slash, backslash, colon,
 * "." or "..". Lexical composition is not a sandbox or symlink containment
 * guarantee.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_path_config_file(
	const char *application, const char *name, hx_string *out,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
