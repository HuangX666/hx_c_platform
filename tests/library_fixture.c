#include <hx_os_base.h>
#include <string.h>

#if defined(_WIN32)
#define HX_PLUGIN_EXPORT __declspec(dllexport)
#else
#define HX_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

HX_PLUGIN_EXPORT uint32_t HX_OS_CALL hx_plugin_abi_version(void);
HX_PLUGIN_EXPORT hx_os_status HX_OS_CALL hx_plugin_version_string(
	char *buffer, size_t capacity, size_t *required);

uint32_t hx_plugin_abi_version(void)
{
	return HX_OS_ABI_VERSION;
}

hx_os_status hx_plugin_version_string(
	char *buffer, size_t capacity, size_t *required)
{
	static const char version[] = HX_PLUGIN_VERSION;
	if (!required || (!buffer && capacity))
		return HX_OS_INVALID;
	*required = sizeof(version);
	if (capacity < sizeof(version)) {
		if (capacity)
			buffer[0] = 0;
		return HX_OS_OUTPUT_FULL;
	}
	memcpy(buffer, version, sizeof(version));
	return HX_OS_OK;
}
