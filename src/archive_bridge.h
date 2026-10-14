#ifndef HX_C_PLATFORM_ARCHIVE_BRIDGE_H
#define HX_C_PLATFORM_ARCHIVE_BRIDGE_H

#include "hx_os_archive.h"
#include <wchar.h>

/**
 * \brief Private, versioned C boundary: the optional C++ adapter links no core
 * implementation and never owns filesystem paths or allocator state.
 */
typedef struct hx_ar_bridge {
	uint32_t size, version;
	void *user, *create_object, *module_property, *stream;
	const char *password;
	uint32_t count, workers, preset, encrypt_names, packing, listing;
	size_t max_entries, max_path_bytes;
	void *(*allocate)(void *user, size_t bytes);
	void (*free)(void *memory);
	hx_os_status (*check)(void *user);
	hx_os_status (*fail)(void *user, hx_os_status status, uint64_t native,
		const char *message);
	hx_os_status (*to_wide)(void *user, const char *text, wchar_t **wide);
	hx_os_status (*from_wide)(
		void *user, const wchar_t *wide, size_t units, char **text);
	hx_os_status (*read)(void *user, void *stream, void *data,
		uint32_t bytes, uint32_t *done);
	hx_os_status (*write)(void *user, void *stream, const void *data,
		uint32_t bytes, uint32_t *done);
	hx_os_status (*seek)(void *user, void *stream, int64_t offset,
		uint32_t origin, uint64_t *position);
	hx_os_status (*resize)(void *user, void *stream, uint64_t bytes);
	void (*release)(void *stream);
	hx_os_status (*source)(void *user, uint32_t index,
		hx_os_archive_entry *entry, void **stream);
	hx_os_status (*inspect)(void *user, uint32_t index,
		const hx_os_archive_entry *entry, int *selected);
	hx_os_status (*extract)(
		void *user, const hx_os_archive_entry *entry, void **stream);
	hx_os_status (*finish)(void *user);
} hx_ar_bridge;

typedef hx_os_status(HX_OS_CALL *hx_ar_bridge_run)(const hx_ar_bridge *host);

#endif
