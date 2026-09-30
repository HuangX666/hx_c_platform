#include "paths_private.h"
#include "hx_os_paths.h"

static hx_os_status hx_os_path_string(
	int kind, hx_string *out, hx_os_error *error)
{
#if HX_OS_ENABLE_PATHS
	hx_string value;
	hx_os_status status;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	hx_string_init(&value, out->context);
	status = hx_os_i_special_path(kind, &value, error);
	if (status == HX_OS_OK) {
		size_t root = 1;
#if defined(_WIN32)
		if (value.size >= 3 && value.data[1] == ':')
			root = 3;
#endif
		while (value.size > root &&
			(value.data[value.size - 1] == '/'
#if defined(_WIN32)
				|| value.data[value.size - 1] == '\\'
#endif
				))
			value.data[--value.size] = 0;
		hx_string_move(out, &value);
	}
	hx_string_destroy(&value);
	return status;
#else
	(void)kind;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

static hx_os_status hx_os_path_buffer(int kind, char *buffer, size_t capacity,
	size_t *required, hx_os_error *error)
{
	hx_string value = HX_STRING_INIT;
	hx_os_status status;
	if (!required || (!buffer && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_path_string(kind, &value, error);
	if (status == HX_OS_OK)
		status = hx_os_i_text_copy(hx_string_c_str(&value), buffer,
			capacity, required, error);
	hx_string_destroy(&value);
	return status;
}

#define HX_OS_DEFINE_PATH(name, kind)                                          \
	hx_os_status hx_os_path_##name(char *buffer, size_t capacity,          \
		size_t *required, hx_os_error *error)                          \
	{                                                                      \
		return hx_os_path_buffer(                                      \
			kind, buffer, capacity, required, error);              \
	}                                                                      \
	hx_os_status hx_os_path_##name##_string(                               \
		hx_string *out, hx_os_error *error)                            \
	{                                                                      \
		return hx_os_path_string(kind, out, error);                    \
	}

HX_OS_DEFINE_PATH(home, HX_OS_I_PATH_HOME)
HX_OS_DEFINE_PATH(config_home, HX_OS_I_PATH_CONFIG_HOME)
HX_OS_DEFINE_PATH(data_home, HX_OS_I_PATH_DATA_HOME)
HX_OS_DEFINE_PATH(program_data, HX_OS_I_PATH_PROGRAM_DATA)
HX_OS_DEFINE_PATH(temp, HX_OS_I_PATH_TEMP)
HX_OS_DEFINE_PATH(executable, HX_OS_I_PATH_EXECUTABLE)
HX_OS_DEFINE_PATH(module, HX_OS_I_PATH_MODULE)
