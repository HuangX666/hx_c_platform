#include "sysinfo_private.h"

struct hx_os_cpu_usage {
	uint64_t wall;
	uint64_t cpu;
	int cores;
};

static hx_os_status hx_os_sys_value(
	int field, uint64_t *out, hx_os_error *error)
{
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
#if HX_OS_ENABLE_SYSINFO
	return hx_os_i_sys_value(field, out, error);
#else
	(void)field;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_sys_total_memory(uint64_t *bytes, hx_os_error *error)
{
	return hx_os_sys_value(HX_OS_SYS_TOTAL, bytes, error);
}

hx_os_status hx_os_sys_available_memory(uint64_t *bytes, hx_os_error *error)
{
	return hx_os_sys_value(HX_OS_SYS_AVAILABLE, bytes, error);
}

static hx_os_status hx_os_sys_count(int field, int *out, hx_os_error *error)
{
	uint64_t value;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_sys_value(field, &value, error);
	if (status != HX_OS_OK)
		return status;

	if (!value || value > INT_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	*out = (int)value;

	return HX_OS_OK;
}

hx_os_status hx_os_sys_physical_cores(int *count, hx_os_error *error)
{
	return hx_os_sys_count(HX_OS_SYS_PHYSICAL, count, error);
}

hx_os_status hx_os_sys_logical_cores(int *count, hx_os_error *error)
{
	return hx_os_sys_count(HX_OS_SYS_LOGICAL, count, error);
}

hx_os_status hx_os_sys_page_size(size_t *bytes, hx_os_error *error)
{
	uint64_t value;
	hx_os_status status;

	if (!bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_sys_value(HX_OS_SYS_PAGE, &value, error);
	if (status != HX_OS_OK)
		return status;

	if (!value || value > SIZE_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	*bytes = (size_t)value;

	return HX_OS_OK;
}

hx_os_status hx_os_sys_disk_space(
	const char *path, hx_os_sys_disk_space_info *info, hx_os_error *error)
{
	hx_os_status checked;

	if (!info || info->size < sizeof(*info) || info->version != 1)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	checked = hx_os_i_path_validate(path, error);
	if (checked != HX_OS_OK)
		return checked;
#if HX_OS_ENABLE_SYSINFO
	{
		hx_os_sys_disk_space_info value =
			HX_OS_SYS_DISK_SPACE_INFO_INIT;

		hx_os_status status = hx_os_i_sys_disk(path, &value, error);
		if (status == HX_OS_OK) {
			info->total_bytes = value.total_bytes;
			info->available_bytes = value.available_bytes;
			info->free_bytes = value.free_bytes;
		}

		return status;
	}
#else
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_sys_proc_memory(
	hx_os_sys_proc_memory_info *info, hx_os_error *error)
{
	if (!info || info->size < sizeof(*info) || info->version != 1)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
#if HX_OS_ENABLE_SYSINFO
	{
		hx_os_sys_proc_memory_info value =
			HX_OS_SYS_PROC_MEMORY_INFO_INIT;

		hx_os_status status = hx_os_i_sys_memory(&value, error);
		if (status == HX_OS_OK) {
			info->resident_bytes = value.resident_bytes;
			info->virtual_bytes = value.virtual_bytes;
		}

		return status;
	}
#else
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_sys_version_numbers(
	int *major, int *minor, int *patch, hx_os_error *error)
{
	if (!major || !minor || !patch)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
#if HX_OS_ENABLE_SYSINFO
	{
		int numbers[3];
		char text[256];

		hx_os_status status = hx_os_i_sys_version(numbers, text, error);
		if (status == HX_OS_OK) {
			*major = numbers[0];
			*minor = numbers[1];
			*patch = numbers[2];
		}

		return status;
	}
#else
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_sys_version(
	char *buffer, size_t capacity, size_t *required, hx_os_error *error)
{
	if (!required || (!buffer && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
#if HX_OS_ENABLE_SYSINFO
	{
		int numbers[3];
		char text[256];

		hx_os_status status = hx_os_i_sys_version(numbers, text, error);

		return status == HX_OS_OK ? hx_os_i_text_copy(text, buffer,
						    capacity, required, error)
					  : status;
	}
#else
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_sys_version_string(hx_string *out, hx_os_error *error)
{
	char text[256];
	size_t required;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_sys_version(text, sizeof(text), &required, error);

	return status == HX_OS_OK ? hx_string_assign_cstr(out, text, error)
				  : status;
}

hx_os_status hx_os_cpu_usage_create(
	hx_os_context *context, hx_os_cpu_usage **out, hx_os_error *error)
{
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
#if HX_OS_ENABLE_SYSINFO
	{
		hx_os_cpu_usage value;
		hx_os_status status =
			hx_os_sys_logical_cores(&value.cores, error);

		if (status == HX_OS_OK)
			status = hx_os_i_sys_cpu(&value.cpu, error);

		if (status == HX_OS_OK)
			status = hx_os_monotonic_read(&value.wall, error);

		if (status != HX_OS_OK)
			return status;

		*out = hx_os_i_alloc(context, sizeof(value), 0);
		if (!*out)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

		**out = value;

		return HX_OS_OK;
	}
#else
	(void)context;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_cpu_usage_query(
	hx_os_cpu_usage *usage, double *percent, hx_os_error *error)
{
	if (!usage || !percent)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
#if HX_OS_ENABLE_SYSINFO
	{
		uint64_t cpu, wall;

		hx_os_status status = hx_os_i_sys_cpu(&cpu, error);
		if (status == HX_OS_OK)
			status = hx_os_monotonic_read(&wall, error);

		if (status != HX_OS_OK)
			return status;

		if (wall <= usage->wall || cpu < usage->cpu)
			return hx_os_i_error(error, HX_OS_WOULD_BLOCK, 0, 0);

		*percent = 100.0 * (double)(cpu - usage->cpu) /
			(double)(wall - usage->wall) / usage->cores;
		usage->cpu = cpu;
		usage->wall = wall;

		return HX_OS_OK;
	}
#else
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

void hx_os_cpu_usage_destroy(hx_os_cpu_usage **usage)
{
	if (usage) {
		hx_os_memory_free(*usage);
		*usage = NULL;
	}
}
