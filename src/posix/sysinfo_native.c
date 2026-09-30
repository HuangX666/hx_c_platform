#include "sysinfo_private.h"

#include <stdio.h>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#else
#include <sys/sysinfo.h>
#endif

#if defined(__linux__)
static hx_os_status hx_os_sys_read(
	const char *path, char *text, size_t capacity, hx_os_error *error)
{
	hx_os_file *file = NULL;
	size_t used = 0;
	hx_os_status status =
		hx_os_file_open(NULL, path, HX_OS_FILE_READ, &file, error);
	if (status != HX_OS_OK)
		return status;
	while (used < capacity - 1) {
		size_t bytes = 0;
		status = hx_os_file_read_some(
			file, text + used, capacity - 1 - used, &bytes, error);
		used += bytes;
		if (status != HX_OS_OK || !bytes)
			break;
	}
	text[used] = 0;
	if (status == HX_OS_OK && used == capacity - 1)
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if (status == HX_OS_EOF)
		status = hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (status == HX_OS_OK)
		status = hx_os_file_close(&file, error);
	else
		(void)hx_os_file_close(&file, NULL);
	return status;
}

static hx_os_status hx_os_sys_physical(uint64_t *out, hx_os_error *error)
{
	struct hx_os_topology {
		long package, core;
	} *cores;
	hx_os_directory *directory = NULL;
	char name[256], path[384], value[128];
	size_t required, count = 0, i;
	long capacity = sysconf(_SC_NPROCESSORS_CONF);
	hx_os_status status;
	if (capacity <= 0 || capacity > 65536)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	cores = hx_os_i_alloc(NULL, (size_t)capacity * sizeof(*cores), 0);
	if (!cores)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	status = hx_os_directory_open(
		NULL, "/sys/devices/system/cpu", &directory, error);
	while (status == HX_OS_OK) {
		char *end;
		long package, core;
		status = hx_os_directory_next(
			directory, name, sizeof(name), &required, error);
		if (status != HX_OS_OK)
			break;
		if (strncmp(name, "cpu", 3) || name[3] < '0' || name[3] > '9')
			continue;
		(void)strtoul(name + 3, &end, 10);
		if (*end)
			continue;
		(void)snprintf(path, sizeof(path),
			"/sys/devices/system/cpu/%s/online", name);
		status = hx_os_sys_read(path, value, sizeof(value), error);
		if (status == HX_OS_OK && value[0] == '0')
			continue;
		if (status != HX_OS_OK && status != HX_OS_NOT_FOUND)
			break;
		(void)snprintf(path, sizeof(path),
			"/sys/devices/system/cpu/%s/topology/"
			"physical_package_id",
			name);
		status = hx_os_sys_read(path, value, sizeof(value), error);
		if (status != HX_OS_OK)
			break;
		package = strtol(value, &end, 10);
		if (end == value || package < 0) {
			status = HX_OS_NOT_SUPPORTED;
			break;
		}
		(void)snprintf(path, sizeof(path),
			"/sys/devices/system/cpu/%s/topology/core_id", name);
		status = hx_os_sys_read(path, value, sizeof(value), error);
		if (status != HX_OS_OK)
			break;
		core = strtol(value, &end, 10);
		if (end == value || core < 0) {
			status = HX_OS_NOT_SUPPORTED;
			break;
		}
		for (i = 0; i < count; ++i)
			if (cores[i].package == package &&
				cores[i].core == core)
				break;
		if (i == count) {
			if (count == (size_t)capacity) {
				status = HX_OS_WOULD_BLOCK;
				break;
			}
			cores[count].package = package;
			cores[count++].core = core;
		}
	}
	hx_os_directory_close(directory);
	hx_os_memory_free(cores);
	if (status == HX_OS_EOF && count) {
		*out = count;
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
	if (status == HX_OS_NOT_FOUND || status == HX_OS_EOF ||
		status == HX_OS_NOT_SUPPORTED)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	return hx_os_i_error(error, status, error ? error->domain : 0,
		error ? error->native_code : 0);
}

static hx_os_status hx_os_sys_available(uint64_t *out, hx_os_error *error)
{
	char text[8192], *line, *end;
	unsigned long long value;
	hx_os_status status =
		hx_os_sys_read("/proc/meminfo", text, sizeof(text), error);
	if (status != HX_OS_OK)
		return status;
	line = text;
	while (line && strncmp(line, "MemAvailable:", 13)) {
		line = strchr(line, '\n');
		if (line)
			++line;
	}
	if (!line)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	errno = 0;
	value = strtoull(line + 13, &end, 10);
	if (errno || end == line + 13 || value > UINT64_MAX / 1024)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	*out = (uint64_t)value * 1024;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
#endif

hx_os_status hx_os_i_sys_value(int field, uint64_t *out, hx_os_error *error)
{
	uint64_t value;
	if (field == HX_OS_SYS_PAGE || field == HX_OS_SYS_LOGICAL) {
		long native =
			sysconf(field == HX_OS_SYS_PAGE ? _SC_PAGESIZE
							: _SC_NPROCESSORS_ONLN);
		if (native <= 0)
			return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
		value = (uint64_t)native;
	}
#if defined(__APPLE__)
	else if (field == HX_OS_SYS_AVAILABLE) {
		vm_statistics64_data_t stats;
		mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
		mach_port_t host = mach_host_self();
		kern_return_t result = host_statistics64(
			host, HOST_VM_INFO64, (host_info64_t)&stats, &count);
		(void)mach_port_deallocate(mach_task_self(), host);
		if (result != KERN_SUCCESS)
			return hx_os_i_error(error, HX_OS_IO_ERROR,
				HX_OS_ERROR_MACH, result);
		value = ((uint64_t)stats.free_count + stats.inactive_count) *
			(uint64_t)sysconf(_SC_PAGESIZE);
	}
	else {
		size_t bytes;
		if (field == HX_OS_SYS_PHYSICAL) {
			int physical;
			bytes = sizeof(physical);
			if (sysctlbyname("hw.physicalcpu", &physical, &bytes,
				    NULL, 0))
				return hx_os_i_errno(error, errno);
			value = (uint64_t)physical;
		}
		else {
			bytes = sizeof(value);
			if (sysctlbyname("hw.memsize", &value, &bytes, NULL, 0))
				return hx_os_i_errno(error, errno);
		}
	}
#else
	else if (field == HX_OS_SYS_AVAILABLE)
		return hx_os_sys_available(out, error);
	else if (field == HX_OS_SYS_PHYSICAL)
		return hx_os_sys_physical(out, error);
	else {
		struct sysinfo info;
		if (sysinfo(&info))
			return hx_os_i_errno(error, errno);
		if (info.mem_unit && info.totalram > UINT64_MAX / info.mem_unit)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
		value = (uint64_t)info.totalram * info.mem_unit;
	}
#endif
	*out = value;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_sys_disk(
	const char *path, hx_os_sys_disk_space_info *out, hx_os_error *error)
{
	struct statvfs value;
	uint64_t unit;
	int result;
	do {
		result = statvfs(path, &value);
	} while (result && errno == EINTR);
	if (result)
		return hx_os_i_errno(error, errno);
	unit = value.f_frsize;
	if (!unit || value.f_blocks > UINT64_MAX / unit ||
		value.f_bavail > UINT64_MAX / unit ||
		value.f_bfree > UINT64_MAX / unit)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	out->total_bytes = (uint64_t)value.f_blocks * unit;
	out->available_bytes = (uint64_t)value.f_bavail * unit;
	out->free_bytes = (uint64_t)value.f_bfree * unit;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_sys_memory(
	hx_os_sys_proc_memory_info *out, hx_os_error *error)
{
#if defined(__APPLE__)
	mach_task_basic_info_data_t info;
	mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
	kern_return_t result = task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
		(task_info_t)&info, &count);
	if (result != KERN_SUCCESS)
		return hx_os_i_error(
			error, HX_OS_IO_ERROR, HX_OS_ERROR_MACH, result);
	out->resident_bytes = info.resident_size;
	out->virtual_bytes = info.virtual_size;
#else
	char text[256], *end, *next;
	unsigned long long pages, resident;
	long page_size = sysconf(_SC_PAGESIZE);
	hx_os_status status =
		hx_os_sys_read("/proc/self/statm", text, sizeof(text), error);
	if (status != HX_OS_OK)
		return status;
	errno = 0;
	pages = strtoull(text, &end, 10);
	resident = strtoull(end, &next, 10);
	if (errno || end == text || end == next || page_size <= 0 ||
		pages > UINT64_MAX / (uint64_t)page_size ||
		resident > UINT64_MAX / (uint64_t)page_size)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	out->resident_bytes = (uint64_t)resident * (uint64_t)page_size;
	out->virtual_bytes = (uint64_t)pages * (uint64_t)page_size;
#endif
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_sys_version(
	int numbers[3], char text[256], hx_os_error *error)
{
	const char *release;
#if defined(__APPLE__)
	char product[128];
	size_t size = sizeof(product);
	if (sysctlbyname("kern.osproductversion", product, &size, NULL, 0))
		return hx_os_i_errno(error, errno);
	product[sizeof(product) - 1] = 0;
	release = product;
#else
	struct utsname info;
	if (uname(&info))
		return hx_os_i_errno(error, errno);
	release = info.release;
#endif
	numbers[0] = numbers[1] = numbers[2] = 0;
	if (sscanf(release, "%d.%d.%d", numbers, numbers + 1, numbers + 2) < 1)
		return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
#if defined(__APPLE__)
	(void)snprintf(text, 256, "macOS %.128s", release);
#else
	(void)snprintf(text, 256, "Linux %.128s", release);
#endif
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_sys_cpu(uint64_t *nanoseconds, hx_os_error *error)
{
	struct rusage value;
	uint64_t seconds, micros;
	if (getrusage(RUSAGE_SELF, &value))
		return hx_os_i_errno(error, errno);
	seconds = (uint64_t)value.ru_utime.tv_sec +
		(uint64_t)value.ru_stime.tv_sec;
	micros = (uint64_t)value.ru_utime.tv_usec +
		(uint64_t)value.ru_stime.tv_usec;
	if (seconds > (UINT64_MAX - micros * 1000) / UINT64_C(1000000000))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	*nanoseconds = seconds * UINT64_C(1000000000) + micros * 1000;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
