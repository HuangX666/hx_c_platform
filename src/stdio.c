#include "internal.h"
#include "hx_os_stdio.h"

static int hx_os_stdio_mode_valid(const char *mode)
{
	size_t i;
	unsigned seen = 0;

	if (!mode || (*mode != 'r' && *mode != 'w' && *mode != 'a'))
		return 0;

	for (i = 1; i < 4 && mode[i]; ++i) {
		unsigned bit = mode[i] == 'b' ? 1u : mode[i] == '+' ? 2u : 0u;

		if (!bit || (seen & bit))
			return 0;

		seen |= bit;
	}

	return i < 4;
}

FILE *hx_os_fopen_utf8(const char *path, const char *mode, hx_os_error *error)
{
	FILE *stream = NULL;
	int code;

	if (!hx_os_stdio_mode_valid(mode)) {
		(void)hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		return NULL;
	}
	if (hx_os_i_path_validate(path, error) != HX_OS_OK)
		return NULL;

#if defined(_WIN32)
	{
		wchar_t wide_mode[4];
		wchar_t *wide_path = hx_os_i_windows_path(NULL, path, error);
		size_t i;

		if (!wide_path)
			return NULL;

		for (i = 0; mode[i]; ++i)
			wide_mode[i] = (wchar_t)(unsigned char)mode[i];

		wide_mode[i] = 0;
		stream = _wfopen(wide_path, wide_mode);
		code = errno;
		hx_os_memory_free(wide_path);
	}
#else
	stream = fopen(path, mode);
	code = errno;
#endif
	if (!stream)
		(void)hx_os_i_errno(error, code ? code : EIO);
	else
		(void)hx_os_i_error(error, HX_OS_OK, 0, 0);

	return stream;
}

hx_os_status hx_os_fclose(FILE **stream, hx_os_error *error)
{
	FILE *value;

	if (!stream)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	value = *stream;
	*stream = NULL;
	if (value && fclose(value))
		return hx_os_i_errno(error, errno ? errno : EIO);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
