#ifndef HX_C_PLATFORM_NET_INTERFACES_H
#define HX_C_PLATFORM_NET_INTERFACES_H

#include "hx_os_net.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_interface_list hx_os_interface_list;

enum {
	HX_OS_INTERFACE_UP = 1,
	HX_OS_INTERFACE_LOOPBACK = 2,
	HX_OS_INTERFACE_MULTICAST = 4,
	HX_OS_INTERFACE_POINT_TO_POINT = 8,
	HX_OS_INTERFACE_BROADCAST = 16
};

typedef struct hx_os_interface_address {
	hx_os_address address;
	uint8_t prefix_length;
} hx_os_interface_address;

/**
 * \brief Borrowed interface information owned by its immutable list.
 *
 * \details Names are NUL-terminated UTF-8. Windows name is AdapterName;
 * display_name is FriendlyName. POSIX uses the interface name for both.
 * Indices are family-specific, may change, and are not persistent identity.
 * Zero index/MTU means unavailable. Addresses have port zero; prefix_length
 * is 0..32/128, or 255 if unavailable. IPv6 link-local addresses carry scope.
 * UP reflects native state (Windows operational, POSIX administrative), not
 * Internet reachability. Entries and addresses have no guaranteed ordering.
 */
typedef struct hx_os_interface_info {
	const char *name;
	const char *display_name;
	const hx_os_interface_address *addresses;
	size_t address_count;
	uint32_t ipv4_index, ipv6_index, flags, mtu;
} hx_os_interface_info;

HX_OS_API int HX_OS_CALL hx_os_interfaces_available(void);

/**
 * \brief Capture interfaces with IP addresses visible to this process.
 *
 * \details No network object, worker or monitoring is required. NULL context
 * uses the default allocator. The context must outlive the result. Success
 * owns *out; failure sets it NULL. An empty result is valid. Concurrent reads
 * are allowed; destruction must exclude readers. Native enumeration is not
 * atomic with network changes; query again after a change. Access restrictions
 * remain errors. Limits: 1024 interfaces, 8192 addresses, 4096-byte names,
 * and 16 MiB of native buffer/input data. Snapshot storage is bounded
 * separately by the item/name limits; libc's internal allocations are not
 * capped. Android's bounded native query may return TIMEOUT or
 * BUSY if enumeration is interrupted. No host settings change.
 * available reports build support, not permission or current connectivity.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_interfaces_query(
	hx_os_context *context, hx_os_interface_list **out, hx_os_error *error);

HX_OS_API size_t HX_OS_CALL hx_os_interface_list_count(
	const hx_os_interface_list *list);

/**
 * \brief NULL list or an out-of-range index returns NULL; count(NULL) is zero.
 */
HX_OS_API const hx_os_interface_info *HX_OS_CALL hx_os_interface_list_get(
	const hx_os_interface_list *list, size_t index);

HX_OS_API void HX_OS_CALL hx_os_interface_list_destroy(
	hx_os_interface_list *list);

#ifdef __cplusplus
}
#endif
#endif
