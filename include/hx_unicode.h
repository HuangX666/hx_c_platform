#ifndef HX_C_PLATFORM_UNICODE_H
#define HX_C_PLATFORM_UNICODE_H

#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
	HX_OS_UNICODE_NFC = 1,
	HX_OS_UNICODE_NFD,
	HX_OS_UNICODE_NFKC,
	HX_OS_UNICODE_NFKD
};

HX_OS_API int HX_OS_CALL hx_os_unicode_available(void);

/**
 * \brief Explicit, locale-independent transformations, never applied to paths,
 * passwords or IDs implicitly.
 *
 * \details Output uses its own context and is unchanged on failure; input may
 * alias output. Limit: 1 MiB input, 16 MiB scratch and result. NUL is counted
 * text. No worker or hard CPU deadline.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_unicode_normalize(hx_string_view input,
	unsigned form, hx_string *out, hx_os_error *error);
/**
 * \brief Default Unicode case folding followed by NFC.
 *
 * \details No language-specific collation, Turkish locale rules or removal of
 * default ignorables.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_unicode_casefold(
	hx_string_view input, hx_string *out, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
