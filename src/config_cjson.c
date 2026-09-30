#include "config_private.h"
#include <cJSON.h>

#if defined(_WIN32)
#include <windows.h>
static SRWLOCK hx_os_cfg_json_lock = SRWLOCK_INIT;
static __declspec(thread) unsigned hx_os_cfg_json_nesting;
#else
#include <pthread.h>
static pthread_mutex_t hx_os_cfg_json_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread unsigned hx_os_cfg_json_nesting;
#endif

int hx_os_cfg_cjson(hx_os_cfg_parser *parser, hx_os_config_node **out)
{
	hx_os_cfg_parser *previous;
	cJSON *native;
	const char *end = NULL;
	int outer;
	/*
	 * The shared materializer retains exact integers and embedded NULs;
	 * cJSON's double/NUL-terminated DOM cannot preserve those values.
	 */
	if (!hx_os_cfg_json(parser, out))
		return 0;
	previous = hx_os_cfg_vendor_enter(parser);
	outer = hx_os_cfg_json_nesting++ == 0;
	if (outer) {
#if defined(_WIN32)
		AcquireSRWLockExclusive(&hx_os_cfg_json_lock);
#else
		int code = pthread_mutex_lock(&hx_os_cfg_json_lock);
		if (code) {
			--hx_os_cfg_json_nesting;
			hx_os_cfg_vendor_leave(parser, previous);
			return hx_os_cfg_reject(parser, HX_OS_IO_ERROR,
				"cJSON serialization failed");
		}
#endif
	}
	/* All calls that write cJSON's private global error state are
	 * serialized. */
	native = cJSON_ParseWithLengthOpts(
		parser->text, parser->length, &end, 0);
	--hx_os_cfg_json_nesting;
	if (outer) {
#if defined(_WIN32)
		ReleaseSRWLockExclusive(&hx_os_cfg_json_lock);
#else
		(void)pthread_mutex_unlock(&hx_os_cfg_json_lock);
#endif
	}
	if (!native && parser->status == HX_OS_OK)
		(void)hx_os_cfg_fail(parser, HX_OS_SYNTAX,
			end ? (size_t)(end - parser->text) : SIZE_MAX,
			"cJSON rejected the document");
	cJSON_Delete(native);
	hx_os_cfg_vendor_leave(parser, previous);
	return parser->status == HX_OS_OK;
}
