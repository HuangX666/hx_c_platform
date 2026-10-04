#ifndef HX_C_PLATFORM_FILE_UTILS_PRIVATE_H
#define HX_C_PLATFORM_FILE_UTILS_PRIVATE_H

#include "internal.h"
#include "hx_string.h"

int hx_os_i_path_separator(char value);

size_t hx_os_i_path_root(const char *path);

int hx_os_i_glob_match(const char *pattern, size_t bytes, const char *name);

hx_os_status hx_os_i_walk_options(const hx_os_walk_options *input,
	hx_os_walk_options *out, hx_os_error *error);

hx_os_status hx_os_i_file_copy(
	const char *from, const char *to, int replace, hx_os_error *error);

hx_os_status hx_os_i_file_replace(const char *target, const char *source,
	const char *backup, hx_os_error *error);

hx_os_status hx_os_i_file_sync_parent(const char *path, hx_os_error *error);

hx_os_status hx_os_i_path_current(hx_string *out, hx_os_error *error);

hx_os_status hx_os_i_path_absolute(
	const char *path, hx_string *out, hx_os_error *error);

hx_os_status hx_os_i_path_change_current(const char *path, hx_os_error *error);

hx_os_status hx_os_i_remove_tree(const char *path,
	const hx_os_walk_options *options, hx_os_error *error);

#endif
