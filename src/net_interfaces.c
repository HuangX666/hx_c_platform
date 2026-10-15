#include "net_interfaces_private.h"

#if !defined(_WIN32)
#include <net/if.h>
#endif

typedef struct hx_os_i_interface_entry {
	hx_os_interface_info info;
	size_t capacity;
} hx_os_i_interface_entry;

struct hx_os_interface_list {
	hx_os_context *context;
	hx_os_i_interface_entry *entries;
	size_t count, capacity, address_count;
};

#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
static hx_os_status hx_os_interface_name(hx_os_context *context,
	const char *source, const char **out, hx_os_error *error)
{
	size_t length = 0, invalid;
	char *copy;

	while (length <= 4096 && source[length])
		++length;

	if (length > 4096)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	if (hx_os_text_validate_utf8(source, length, &invalid) != HX_OS_OK)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);

	copy = hx_os_i_alloc(context, length + 1, 0);
	if (!copy)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	memcpy(copy, source, length + 1);
	*out = copy;

	return HX_OS_OK;
}

hx_os_status hx_os_i_interface_append(hx_os_interface_list *list,
	const hx_os_interface_info *info,
	const hx_os_interface_address *address, hx_os_error *error)
{
	hx_os_i_interface_entry *entry;
	hx_os_interface_address *addresses;
	hx_os_status status;
	size_t i, capacity;

	if (list->address_count == 8192)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	for (i = 0; i < list->count; ++i)
		if (!strcmp(list->entries[i].info.name, info->name))
			break;

	if (i == list->count) {
		if (list->count == 1024)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		if (list->count == list->capacity) {
			capacity = list->capacity ? list->capacity * 2 : 8;
			entry = hx_os_i_zero(list->context,
				capacity * sizeof(*entry));
			if (!entry)
				return hx_os_i_error(
					error, HX_OS_NO_MEMORY, 0, 0);

			if (list->count)
				memcpy(entry, list->entries,
					list->count * sizeof(*entry));
			hx_os_memory_free(list->entries);
			list->entries = entry;
			list->capacity = capacity;
		}

		entry = list->entries + list->count++;
		entry->info = *info;
		entry->info.name = NULL;
		entry->info.display_name = NULL;
		entry->info.addresses = NULL;
		entry->info.address_count = 0;
		status = hx_os_interface_name(list->context, info->name,
			&entry->info.name, error);
		if (status != HX_OS_OK)
			return status;

		status = hx_os_interface_name(list->context,
			info->display_name, &entry->info.display_name, error);
		if (status != HX_OS_OK)
			return status;
	}

	entry = list->entries + i;
	if (entry->info.address_count == entry->capacity) {
		capacity = entry->capacity ? entry->capacity * 2 : 4;
		addresses = hx_os_i_alloc(list->context,
			capacity * sizeof(*addresses), 0);
		if (!addresses)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

		if (entry->info.address_count)
			memcpy(addresses, entry->info.addresses,
				entry->info.address_count * sizeof(*addresses));
		hx_os_memory_free((void *)entry->info.addresses);
		entry->info.addresses = addresses;
		entry->capacity = capacity;
	}

	addresses = (hx_os_interface_address *)entry->info.addresses;
	addresses[entry->info.address_count++] = *address;
	++list->address_count;

	return HX_OS_OK;
}

#if !defined(_WIN32)
uint32_t hx_os_i_interface_flags(unsigned flags)
{
	uint32_t result = 0;

	if (flags & IFF_UP)
		result |= HX_OS_INTERFACE_UP;
	if (flags & IFF_LOOPBACK)
		result |= HX_OS_INTERFACE_LOOPBACK;
	if (flags & IFF_MULTICAST)
		result |= HX_OS_INTERFACE_MULTICAST;
	if (flags & IFF_POINTOPOINT)
		result |= HX_OS_INTERFACE_POINT_TO_POINT;
	if (flags & IFF_BROADCAST)
		result |= HX_OS_INTERFACE_BROADCAST;

	return result;
}
#endif
#endif

int hx_os_interfaces_available(void)
{
	return HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS;
}

hx_os_status hx_os_interfaces_query(
	hx_os_context *context, hx_os_interface_list **out, hx_os_error *error)
{
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
	{
		hx_os_interface_list *list = hx_os_i_zero(
			context, sizeof(*list));
		hx_os_status status;

		if (!list)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

		list->context = context;
		status = hx_os_i_interfaces_native(context, list, error);
		if (status != HX_OS_OK) {
			hx_os_interface_list_destroy(list);

			return status;
		}

		*out = list;

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
#else
	(void)context;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

size_t hx_os_interface_list_count(const hx_os_interface_list *list)
{
	return list ? list->count : 0;
}

const hx_os_interface_info *hx_os_interface_list_get(
	const hx_os_interface_list *list, size_t index)
{
	return list && index < list->count ? &list->entries[index].info : NULL;
}

void hx_os_interface_list_destroy(hx_os_interface_list *list)
{
	size_t i;

	if (!list)
		return;

	for (i = 0; i < list->count; ++i) {
		hx_os_interface_info *info = &list->entries[i].info;

		hx_os_memory_free((void *)info->name);
		hx_os_memory_free((void *)info->display_name);
		hx_os_memory_free((void *)info->addresses);
	}
	hx_os_memory_free(list->entries);
	hx_os_memory_free(list);
}
