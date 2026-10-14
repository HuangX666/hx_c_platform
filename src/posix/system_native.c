#include "system_private.h"
#include <fcntl.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#include <sys/sysctl.h>
#elif (defined(__arm__) || defined(__aarch64__)) && HX_OS_HAVE_GETAUXVAL
#include <sys/auxv.h>
#include <asm/hwcap.h>
#endif

uint64_t hx_os_i_process_id(void)
{
	return (uint64_t)getpid();
}

hx_os_status hx_os_i_host_name(hx_string *out, hx_os_error *error)
{
	char buffer[256];

	memset(buffer, 0xff, sizeof(buffer));
	if (gethostname(buffer, sizeof(buffer)))
		return hx_os_i_errno(error, errno);

	if (!memchr(buffer, '\0', sizeof(buffer)))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	return hx_string_assign_cstr(out, buffer, error);
}

hx_os_status hx_os_i_arguments(
	hx_os_context *context, hx_os_string_list **out, hx_os_error *error)
{
	char *block = NULL;
	size_t bytes = 0;
	hx_os_status status = HX_OS_OK;
#if defined(__linux__)
	size_t capacity = 4096;

	int descriptor = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
	if (descriptor < 0) {
		int code = errno;

		return code == ENOENT
			? hx_os_i_error(error, HX_OS_NOT_SUPPORTED,
				  HX_OS_ERROR_ERRNO, code)
			: hx_os_i_errno(error, code);
	}

	block = hx_os_i_alloc(context, capacity, 0);
	if (!block) {
		status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		goto done;
	}

	for (;;) {
		ssize_t got;

		if (bytes == capacity) {
			char *next;

			if (capacity == HX_OS_I_SYSTEM_BYTES) {
				char excess;

				do {
					got = read(descriptor, &excess, 1);
				} while (got < 0 && errno == EINTR);
				if (got)
					status = got < 0
						? hx_os_i_errno(error, errno)
						: hx_os_i_error(error,
							  HX_OS_LIMIT, 0, 0);

				break;
			}

			capacity *= 2;
			next = hx_os_i_alloc(context, capacity, 0);
			if (!next) {
				status = hx_os_i_error(
					error, HX_OS_NO_MEMORY, 0, 0);
				break;
			}

			memcpy(next, block, bytes);

			hx_os_memory_free(block);
			block = next;
		}

		got = read(descriptor, block + bytes, capacity - bytes);
		if (got < 0) {
			if (errno == EINTR)
				continue;

			status = hx_os_i_errno(error, errno);
			break;
		}

		if (!got)
			break;

		bytes += (size_t)got;
	}

done:
	(void)close(descriptor);
#else
	int count = *_NSGetArgc(), i;

	char **values = *_NSGetArgv();

	size_t offset = 0;

	if (count < 0 || (unsigned)count > HX_OS_I_SYSTEM_ITEMS)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	for (i = 0; i < count; ++i) {
		size_t length = strlen(values[i]) + 1;
		if (length > HX_OS_I_SYSTEM_BYTES - bytes)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		bytes += length;
	}

	block = hx_os_i_alloc(context, bytes, 0);
	if (!block)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	for (i = 0; i < count; ++i) {
		size_t length = strlen(values[i]) + 1;
		memcpy(block + offset, values[i], length);
		offset += length;
	}
#endif
	if (status == HX_OS_OK)
		status = hx_os_i_strings_from_block(
			context, block, bytes, 1, out, error);

	hx_os_memory_free(block);

	return status;
}

hx_os_status hx_os_i_emulated(int *out, hx_os_error *error)
{
#if defined(__APPLE__)
	int translated = 0;
	size_t bytes = sizeof(translated);

	if (sysctlbyname(
		    "sysctl.proc_translated", &translated, &bytes, NULL, 0)) {
		if (errno != ENOENT)
			return hx_os_i_errno(error, errno);

		// Apple's documented ENOENT case means Rosetta is unavailable.
		translated = 0;
	}

	*out = translated != 0;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

uint64_t hx_os_i_cpu_native_features(void)
{
#if defined(__linux__) && defined(__aarch64__) && HX_OS_HAVE_GETAUXVAL
	return getauxval(AT_HWCAP) & HWCAP_ASIMD ? HX_OS_CPU_NEON : 0;
#elif defined(__linux__) && defined(__arm__) && HX_OS_HAVE_GETAUXVAL
	return getauxval(AT_HWCAP) & HWCAP_NEON ? HX_OS_CPU_NEON : 0;
#elif defined(__APPLE__) && defined(__aarch64__)
	int supported = 0;
	size_t bytes = sizeof(supported);

	return !sysctlbyname("hw.optional.neon", &supported, &bytes, NULL, 0) &&
			supported
		? HX_OS_CPU_NEON
		: 0;
#else
	return 0;
#endif
}
