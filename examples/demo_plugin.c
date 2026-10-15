#include <hx_os_base.h>
#include <string.h>

#if defined(_WIN32)
#define HX_EXAMPLE_EXPORT __declspec(dllexport)
#else
#define HX_EXAMPLE_EXPORT __attribute__((visibility("default")))
#endif

HX_EXAMPLE_EXPORT uint32_t HX_OS_CALL hx_plugin_abi_version(void);
HX_EXAMPLE_EXPORT hx_os_status HX_OS_CALL hx_plugin_version_string(
	char *buffer, size_t capacity, size_t *required);

uint32_t HX_OS_CALL hx_plugin_abi_version(void)
{
	return HX_OS_ABI_VERSION;
}

/**
 * \brief Copy a plugin-owned description into the caller's storage.
 * \param[out] buffer Receives a NUL-terminated string on success.
 * \param[in] capacity Writable bytes; zero permits a size-only query.
 * \param[out] required Required bytes, including the terminating NUL.
 * \returns OK, OUTPUT_FULL, or INVALID for inconsistent arguments.
 */
hx_os_status HX_OS_CALL hx_plugin_version_string(
	char *buffer, size_t capacity, size_t *required)
{
	static const char description[] = "Example plugin 1.0";

	if (!required || (!buffer && capacity))
		return HX_OS_INVALID;

	*required = sizeof(description);
	if (capacity < sizeof(description))
		return HX_OS_OUTPUT_FULL;

	memcpy(buffer, description, sizeof(description));

	return HX_OS_OK;
}
