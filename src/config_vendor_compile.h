#ifndef HX_C_PLATFORM_VENDOR_COMPILE_H
#define HX_C_PLATFORM_VENDOR_COMPILE_H

// Include native declarations before redirecting calls in private objects.
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <ctype.h>
#include <assert.h>

// Some C99 headers, including Bionic, omit this vendor-required macro.
#if !defined(static_assert) && !defined(__cplusplus)
#define HX_CFG_ASSERT_NAME_(line) hx_cfg_static_assert_##line
#define HX_CFG_ASSERT_NAME(line) HX_CFG_ASSERT_NAME_(line)
#define static_assert(condition, message)                                      \
	typedef char HX_CFG_ASSERT_NAME(__LINE__)[(condition) ? 1 : -1]
#endif

void *hx_os_cfg_vendor_malloc(size_t bytes);

void *hx_os_cfg_vendor_calloc(size_t count, size_t bytes);

void *hx_os_cfg_vendor_realloc(void *memory, size_t bytes);

void hx_os_cfg_vendor_free(void *memory);

char *hx_os_cfg_vendor_strdup(const char *text);

double hx_os_cfg_vendor_strtod(const char *text, char **end);

int hx_os_cfg_vendor_isspace(int character);

#define malloc hx_os_cfg_vendor_malloc
#define calloc hx_os_cfg_vendor_calloc
#define realloc hx_os_cfg_vendor_realloc
#define free hx_os_cfg_vendor_free
#if defined(_WIN32)
#define _strdup hx_os_cfg_vendor_strdup
#define strdup _strdup
#else
#define strdup hx_os_cfg_vendor_strdup
#endif
#define strtod hx_os_cfg_vendor_strtod
#undef isspace
#define isspace hx_os_cfg_vendor_isspace

#endif
