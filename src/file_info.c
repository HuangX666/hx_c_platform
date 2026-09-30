#include "internal.h"

hx_os_status hx_os_file_info_extended_init(
	hx_os_file_info_extended *info, size_t capacity)
{
	hx_os_file_info_extended value = HX_OS_FILE_INFO_EXTENDED_INIT;
	if (!info || capacity < sizeof(value))
		return HX_OS_INVALID;
	memcpy(info, &value, sizeof(value));
	return HX_OS_OK;
}

hx_os_status hx_os_path_stat_extended(const char *path, int follow_links,
	hx_os_file_info_extended *info, hx_os_error *error)
{
#if HX_OS_ENABLE_FILE_INFO
	hx_os_file_info_extended value = HX_OS_FILE_INFO_EXTENDED_INIT;
	hx_os_status status;
	if (!info || info->size < sizeof(*info) || info->version != 1 ||
		(follow_links != 0 && follow_links != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_path_stat_extended(path, follow_links, &value, error);
	if (status == HX_OS_OK)
		memcpy(info, &value, sizeof(value));
	return status;
#else
	(void)path;
	(void)follow_links;
	(void)info;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

#if !HX_OS_ENABLE_FILE_INFO
hx_os_status hx_os_directory_next_info(hx_os_directory *directory, char *name,
	size_t capacity, size_t *required, unsigned *kind, hx_os_error *error)
{
	(void)directory;
	(void)name;
	(void)capacity;
	(void)required;
	(void)kind;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}
#endif
