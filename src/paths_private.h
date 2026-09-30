#ifndef HX_C_PLATFORM_PATHS_PRIVATE_H
#define HX_C_PLATFORM_PATHS_PRIVATE_H
#include "internal.h"

enum hx_os_i_path_kind {
	HX_OS_I_PATH_HOME,
	HX_OS_I_PATH_CONFIG_HOME,
	HX_OS_I_PATH_DATA_HOME,
	HX_OS_I_PATH_PROGRAM_DATA,
	HX_OS_I_PATH_TEMP,
	HX_OS_I_PATH_EXECUTABLE,
	HX_OS_I_PATH_MODULE
};

hx_os_status hx_os_i_special_path(int kind, hx_string *out, hx_os_error *error);
#endif
