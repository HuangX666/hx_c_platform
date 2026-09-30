#include "internal.h"

#include <inttypes.h>
#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <netdb.h>
#if defined(__APPLE__)
#include <mach/mach_error.h>
#endif
#endif

typedef union hx_os_i_memory_header {
	struct {
		hx_os_context *context;
		void *original;
		size_t reserved;
	} fields;
	hx_os_i_alignment alignment;
} hx_os_i_memory_header;

hx_os_status hx_os_i_error(
	hx_os_error *error, hx_os_status status, uint32_t domain, int64_t code)
{
	if (error) {
		error->status = status;
		error->domain = domain;
		error->native_code = code;
		error->cause_code = 0;
	}
	return status;
}

void hx_os_error_clear(hx_os_error *error)
{
	(void)hx_os_i_error(error, HX_OS_OK, HX_OS_ERROR_NONE, 0);
}

hx_os_status hx_os_i_errno(hx_os_error *error, int code)
{
	hx_os_status status = HX_OS_IO_ERROR;
	switch (code) {
	case 0:
		status = HX_OS_OK;
		break;
	case ENOMEM:
		status = HX_OS_NO_MEMORY;
		break;
	case EINVAL:
		status = HX_OS_INVALID;
		break;
	case ENOENT:
		status = HX_OS_NOT_FOUND;
		break;
	case EEXIST:
		status = HX_OS_EXISTS;
		break;
	case EACCES:
	case EPERM:
		status = HX_OS_ACCESS_DENIED;
		break;
	case EBUSY:
		status = HX_OS_BUSY;
		break;
	case EAGAIN:
		status = HX_OS_WOULD_BLOCK;
		break;
#if EWOULDBLOCK != EAGAIN
	case EWOULDBLOCK:
		status = HX_OS_WOULD_BLOCK;
		break;
#endif
	case ETIMEDOUT:
		status = HX_OS_TIMEOUT;
		break;
	case EDEADLK:
		status = HX_OS_DEADLOCK;
		break;
	case EXDEV:
		status = HX_OS_CROSS_DEVICE;
		break;
	case ENOSYS:
		status = HX_OS_NOT_SUPPORTED;
		break;
	case EOVERFLOW:
		status = HX_OS_RANGE;
		break;
	default:
		break;
	}
	return hx_os_i_error(error, status, HX_OS_ERROR_ERRNO, code);
}

hx_os_status hx_os_i_win32(hx_os_error *error, uint32_t code)
{
	hx_os_status status = HX_OS_IO_ERROR;
#if defined(_WIN32)
	switch (code) {
	case ERROR_SUCCESS:
		status = HX_OS_OK;
		break;
	case ERROR_NOT_ENOUGH_MEMORY:
	case ERROR_OUTOFMEMORY:
		status = HX_OS_NO_MEMORY;
		break;
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:
		status = HX_OS_NOT_FOUND;
		break;
	case ERROR_ALREADY_EXISTS:
	case ERROR_FILE_EXISTS:
		status = HX_OS_EXISTS;
		break;
	case ERROR_ACCESS_DENIED:
		status = HX_OS_ACCESS_DENIED;
		break;
	case ERROR_INVALID_PARAMETER:
		status = HX_OS_INVALID;
		break;
	case ERROR_BUSY:
	case ERROR_SHARING_VIOLATION:
		status = HX_OS_BUSY;
		break;
	case ERROR_NOT_SUPPORTED:
	case ERROR_CALL_NOT_IMPLEMENTED:
		status = HX_OS_NOT_SUPPORTED;
		break;
	case ERROR_NOT_SAME_DEVICE:
		status = HX_OS_CROSS_DEVICE;
		break;
	case ERROR_OPERATION_ABORTED:
		status = HX_OS_CANCELLED;
		break;
	case ERROR_TIMEOUT:
	case WAIT_TIMEOUT:
		status = HX_OS_TIMEOUT;
		break;
	default:
		break;
	}
#endif
	return hx_os_i_error(error, status, HX_OS_ERROR_WIN32, code);
}

const char *hx_os_status_name(hx_os_status status)
{
	switch (status) {
	case HX_OS_OK:
		return "success";
	case HX_OS_EOF:
		return "end of file";
	case HX_OS_NEED_INPUT:
		return "more input required";
	case HX_OS_OUTPUT_FULL:
		return "output buffer full";
	case HX_OS_INVALID:
		return "invalid argument";
	case HX_OS_NO_MEMORY:
		return "out of memory";
	case HX_OS_IO_ERROR:
		return "I/O error";
	case HX_OS_NOT_FOUND:
		return "not found";
	case HX_OS_EXISTS:
		return "already exists";
	case HX_OS_ACCESS_DENIED:
		return "access denied";
	case HX_OS_TIMEOUT:
		return "timeout";
	case HX_OS_CANCELLED:
		return "cancelled";
	case HX_OS_BUSY:
		return "resource busy";
	case HX_OS_CLOSED:
		return "closed";
	case HX_OS_NOT_SUPPORTED:
		return "not supported";
	case HX_OS_LIMIT:
		return "capacity limit";
	case HX_OS_WOULD_BLOCK:
		return "would block";
	case HX_OS_DEADLOCK:
		return "deadlock";
	case HX_OS_INVALID_ENCODING:
		return "invalid encoding";
	case HX_OS_UNREPRESENTABLE:
		return "unrepresentable character";
	case HX_OS_TRUNCATED_INPUT:
		return "truncated input";
	case HX_OS_CROSS_DEVICE:
		return "cross-device operation";
	case HX_OS_RANGE:
		return "value out of range";
	case HX_OS_SYNTAX:
		return "syntax error";
	case HX_OS_TYPE_MISMATCH:
		return "type mismatch";
	default:
		return "unknown status";
	}
}

/* Only the newly supported domains use this strict, bounded snapshot. */
static void hx_os_error_extra_text(
	const hx_os_error *error, char *buffer, size_t capacity)
{
	size_t invalid;
	if (error->native_code < INT_MIN || error->native_code > INT_MAX)
		return;
#if defined(_WIN32)
	if (error->domain == HX_OS_ERROR_GAI) {
		wchar_t wide[1024];
		DWORD count = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, (DWORD)error->native_code, 0, wide, 1024, NULL);
		/* gai_strerrorW shares static storage on Windows. */
		if (count &&
			!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
				wide, -1, buffer, (int)capacity, NULL, NULL))
			buffer[0] = '\0';
	}
#else
	{
		const char *message = NULL;
		if (error->domain == HX_OS_ERROR_GAI)
			message = gai_strerror((int)error->native_code);
#if defined(__APPLE__)
		else if (error->domain == HX_OS_ERROR_MACH)
			message = mach_error_string((int)error->native_code);
#endif
		if (message) {
			size_t bytes = strlen(message);
			if (bytes < capacity &&
				hx_os_text_validate_utf8(
					message, bytes, &invalid) == HX_OS_OK)
				memcpy(buffer, message, bytes + 1);
		}
	}
#endif
	if (hx_os_text_validate_utf8(buffer, strlen(buffer), &invalid) !=
		HX_OS_OK)
		buffer[0] = '\0';
}

hx_os_status hx_os_error_message(const hx_os_error *error, char *buffer,
	size_t capacity, size_t *required)
{
	char native[2048] = {0};
	int length;
	if (!error || !required || (!buffer && capacity))
		return HX_OS_INVALID;
#if defined(_WIN32)
	if (error->domain == HX_OS_ERROR_WIN32 ||
		error->domain == HX_OS_ERROR_WINSOCK ||
		error->domain == HX_OS_ERROR_HRESULT) {
		wchar_t wide[1024];
		DWORD count = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, (DWORD)error->native_code, 0, wide, 1024, NULL);
		if (count)
			(void)WideCharToMultiByte(CP_UTF8, 0, wide, -1, native,
				(int)sizeof(native), NULL, NULL);
	}
#endif
	if (error->domain == HX_OS_ERROR_ERRNO) {
		const char *message;
		size_t offset;
		char storage[1024];
#if defined(_WIN32)
		message = strerror_s(storage, sizeof(storage),
				  (int)error->native_code) == 0
			? storage
			: NULL;
#elif defined(__GLIBC__) && defined(_GNU_SOURCE)
		message = strerror_r(
			(int)error->native_code, storage, sizeof(storage));
#else
		message = strerror_r((int)error->native_code, storage,
				  sizeof(storage)) == 0
			? storage
			: NULL;
#endif
		if (message &&
			hx_os_text_validate_utf8(
				message, strlen(message), &offset) == HX_OS_OK)
			(void)snprintf(native, sizeof(native), "%s", message);
	}
	if (error->domain == HX_OS_ERROR_GAI ||
		error->domain == HX_OS_ERROR_MACH)
		hx_os_error_extra_text(error, native, sizeof(native));
	length = snprintf(buffer, capacity,
		"%s [domain=%u code=%" PRId64 "]%s%s",
		hx_os_status_name(error->status), error->domain,
		error->native_code, native[0] ? ": " : "", native);
	if (length < 0)
		return HX_OS_IO_ERROR;
	*required = (size_t)length + 1;
	return *required <= capacity ? HX_OS_OK : HX_OS_OUTPUT_FULL;
}

int hx_os_i_add_size(size_t a, size_t b, size_t *out)
{
	if (b > SIZE_MAX - a)
		return 0;
	*out = a + b;
	return 1;
}

int hx_os_i_alignment_valid(size_t alignment)
{
	return !alignment || !(alignment & (alignment - 1));
}

static void *hx_os_default_allocate(size_t bytes, void *user)
{
	(void)user;
	return malloc(bytes);
}

static void hx_os_default_deallocate(void *memory, void *user)
{
	(void)user;
	free(memory);
}

void hx_os_context_options_init(hx_os_context_options *options)
{
	memset(options, 0, sizeof(*options));
	options->size = sizeof(*options);
	options->version = HX_OS_ABI_VERSION;
}

hx_os_status hx_os_context_create(const hx_os_context_options *options,
	hx_os_context **out, hx_os_error *error)
{
	hx_os_allocator allocator;
	hx_os_context *context;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	memset(&allocator, 0, sizeof(allocator));
	if (options) {
		if (options->size < sizeof(*options) ||
			options->version != HX_OS_ABI_VERSION ||
			!!options->allocator.allocate !=
				!!options->allocator.deallocate)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		allocator = options->allocator;
	}
	if (!allocator.allocate) {
		allocator.allocate = hx_os_default_allocate;
		allocator.deallocate = hx_os_default_deallocate;
	}
	context = allocator.allocate(sizeof(*context), allocator.user);
	if (!context)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	memset(context, 0, sizeof(*context));
	context->allocator = allocator;
	*out = context;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_i_context_retain(hx_os_context *context)
{
	if (context)
		(void)hx_os_atomic_fetch_add(
			&context->leases, 1, HX_OS_RELAXED);
}

void hx_os_i_context_release(hx_os_context *context)
{
	if (context)
		(void)hx_os_atomic_fetch_add(
			&context->leases, -1, HX_OS_RELEASE);
}

hx_os_status hx_os_context_destroy(hx_os_context **context, hx_os_error *error)
{
	hx_os_context *value;
	if (!context)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	value = *context;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (hx_os_atomic_load(&value->allocations, HX_OS_ACQUIRE) ||
		hx_os_atomic_load(&value->leases, HX_OS_ACQUIRE))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	*context = NULL;
	value->allocator.deallocate(value, value->allocator.user);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

size_t hx_os_context_allocations(const hx_os_context *context)
{
	return context ? (size_t)hx_os_atomic_load(
				 &context->allocations, HX_OS_RELAXED)
		       : 0;
}

size_t hx_os_i_allocation_cost(size_t bytes, size_t alignment)
{
	size_t total;
	if (!hx_os_i_alignment_valid(alignment))
		return 0;
	if (alignment < HX_OS_I_ALIGNMENT)
		alignment = HX_OS_I_ALIGNMENT;
	if (!hx_os_i_add_size(
		    bytes ? bytes : 1, sizeof(hx_os_i_memory_header), &total) ||
		!hx_os_i_add_size(total, alignment - 1, &total))
		return 0;
	return total;
}

size_t hx_os_i_allocation_bytes(const void *memory)
{
	const hx_os_i_memory_header *header =
		(const hx_os_i_memory_header *)memory - 1;
	return header->fields.reserved;
}

void *hx_os_i_alloc(hx_os_context *context, size_t bytes, size_t alignment)
{
	hx_os_i_memory_header *header;
	void *original;
	uintptr_t address;
	size_t total;
	if (!hx_os_i_alignment_valid(alignment))
		return NULL;
	if (alignment < HX_OS_I_ALIGNMENT)
		alignment = HX_OS_I_ALIGNMENT;
	total = hx_os_i_allocation_cost(bytes, alignment);
	if (!total)
		return NULL;
	original = context
		? context->allocator.allocate(total, context->allocator.user)
		: malloc(total);
	if (!original)
		return NULL;
	address = (uintptr_t)original + sizeof(*header);
	address = (address + alignment - 1) & ~(uintptr_t)(alignment - 1);
	header = (hx_os_i_memory_header *)address - 1;
	header->fields.original = original;
	header->fields.context = context;
	header->fields.reserved = total;
	if (context)
		(void)hx_os_atomic_fetch_add(
			&context->allocations, 1, HX_OS_RELAXED);
	return (void *)address;
}

void *hx_os_i_zero(hx_os_context *context, size_t bytes)
{
	void *memory = hx_os_i_alloc(context, bytes, 0);
	if (memory)
		memset(memory, 0, bytes);
	return memory;
}

hx_os_status hx_os_memory_alloc(hx_os_context *context, size_t bytes,
	size_t alignment, void **out, hx_os_error *error)
{
	if (!out || !hx_os_i_alignment_valid(alignment))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = hx_os_i_alloc(context, bytes, alignment);
	return hx_os_i_error(error, *out ? HX_OS_OK : HX_OS_NO_MEMORY, 0, 0);
}

void hx_os_memory_free(void *memory)
{
	hx_os_i_memory_header *header;
	hx_os_context *context;
	void *original;
	if (!memory)
		return;
	header = (hx_os_i_memory_header *)memory - 1;
	context = header->fields.context;
	original = header->fields.original;
	if (context) {
		context->allocator.deallocate(
			original, context->allocator.user);
		(void)hx_os_atomic_fetch_add(
			&context->allocations, -1, HX_OS_RELEASE);
	}
	else
		free(original);
}

hx_os_status hx_os_i_path_validate(const char *path, hx_os_error *error)
{
	size_t offset;
	if (!path || !path[0])
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_os_i_error(error,
		hx_os_text_validate_utf8(path, strlen(path), &offset), 0, 0);
}

uint64_t hx_os_capabilities(void)
{
	uint64_t result = HX_OS_CAP_THREADS;
#if HX_OS_ENABLE_SYSINFO
	result |= HX_OS_CAP_SYSINFO;
#endif
#if HX_OS_ENABLE_LIBRARY
	result |= HX_OS_CAP_LIBRARY;
#endif
#if HX_OS_ENABLE_ENV
	result |= HX_OS_CAP_ENV;
#endif
#if HX_OS_ENABLE_PATHS
	result |= HX_OS_CAP_PATHS;
#endif
#if HX_OS_ENABLE_THREAD_EX
	result |= HX_OS_CAP_THREAD_EX;
#endif
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
	result |= HX_OS_CAP_NET_OPTIONS;
#endif
#if HX_OS_ENABLE_POSITIONED_IO
	result |= HX_OS_CAP_POSITIONED_IO;
#endif
#if HX_OS_ENABLE_FILE_INFO
	result |= HX_OS_CAP_FILE_INFO;
#endif
#if HX_OS_ENABLE_BOM
	result |= HX_OS_CAP_BOM;
#endif
#if HX_OS_ENABLE_TEXT_NUMBER && HX_OS_HAVE_LOCAL_STRTOD &&                     \
	HX_OS_HAVE_LOCAL_PRINTF
	result |= HX_OS_CAP_TEXT_NUMBER;
#endif
#if HX_OS_ENABLE_CONFIG && HX_OS_ENABLE_CONFIG_SERIALIZE
	result |= HX_OS_CAP_CONFIG_SERIALIZE;
#endif
#if HX_OS_ENABLE_TIMER && HX_OS_ENABLE_LOOP
	result |= HX_OS_CAP_TIMER;
#endif
#if HX_OS_ENABLE_RANDOM
	result |= HX_OS_CAP_RANDOM;
#endif
#if HX_OS_ENABLE_VIRTUAL_MEMORY
	result |= HX_OS_CAP_VIRTUAL_MEMORY;
#endif
#if HX_OS_ENABLE_FILE_EXTRA
	result |= HX_OS_CAP_FILE_EXTRA;
#endif
#if HX_OS_ENABLE_WATCH
	result |= HX_OS_CAP_WATCH;
#endif
#if HX_OS_ENABLE_SYSTEM_UTILS
	result |= HX_OS_CAP_SYSTEM_UTILS;
#endif
#if HX_OS_ENABLE_PROCESS
	result |= HX_OS_CAP_PROCESS;
#endif
#if HX_OS_ENABLE_LOCAL_SOCKETS && HX_OS_ENABLE_NETWORK
	result |= HX_OS_CAP_LOCAL_SOCKETS;
#endif
#if HX_OS_ENABLE_DUPLEX_PIPE
	result |= HX_OS_CAP_DUPLEX_PIPE;
#endif
#if HX_OS_ENABLE_REGISTRY && defined(_WIN32)
	result |= HX_OS_CAP_REGISTRY;
#endif
#if HX_OS_ENABLE_SLEEP_INHIBIT && (defined(_WIN32) || defined(__APPLE__))
	result |= HX_OS_CAP_SLEEP_INHIBIT;
#endif
#if HX_OS_ENABLE_PERFORMANCE_REQUEST && (defined(_WIN32) || defined(__APPLE__))
	result |= HX_OS_CAP_PERFORMANCE_REQUEST;
#endif
#if HX_OS_ENABLE_OBFUSCATION
	result |= HX_OS_CAP_OBFUSCATION;
#endif
#if HX_OS_ENABLE_CALENDAR
	result |= HX_OS_CAP_CALENDAR;
#endif
#if HX_OS_ENABLE_TEXT_LOOKUP
	result |= HX_OS_CAP_TEXT_LOOKUP;
#endif
#if HX_OS_ENABLE_BITSTREAM
	result |= HX_OS_CAP_BITSTREAM;
#endif
#if HX_OS_ENABLE_SIGNALS
	result |= HX_OS_CAP_SIGNALS;
#endif
#if HX_OS_ENABLE_LOOP_FD && HX_OS_ENABLE_LOOP && !defined(_WIN32)
	result |= HX_OS_CAP_LOOP_FD;
#endif
#if HX_OS_ENABLE_PIPE
	result |= HX_OS_CAP_PIPE;
#if HX_OS_ENABLE_LOOP
	result |= HX_OS_CAP_ASYNC_PIPE;
#endif
#endif
#if HX_OS_ENABLE_FILE_UTILS
	result |= HX_OS_CAP_FILE_UTILS;
#if HX_OS_ENABLE_CONTAINERS
	result |= HX_OS_CAP_GLOB;
#endif
#endif
#if HX_OS_HAVE_TIMED_JOIN
	result |= HX_OS_CAP_TIMED_JOIN;
#endif
#if HX_OS_ENABLE_NETWORK
	result |= HX_OS_CAP_NETWORK;
#endif
#if HX_OS_ENABLE_LOOP
	result |= HX_OS_CAP_LOOP;
#endif
#if HX_OS_ENABLE_THREAD_POOL
	result |= HX_OS_CAP_THREAD_POOL;
#if HX_OS_ENABLE_DELAYED_TASKS
	result |= HX_OS_CAP_DELAYED_TASKS;
#endif
#endif
#if HX_OS_ENABLE_MEMORY_POOLS
	result |= HX_OS_CAP_MEMORY_POOLS;
#endif
#if HX_OS_HAVE_ICONV
	result |= HX_OS_CAP_LEGACY_TEXT;
#endif
#if HX_OS_ENABLE_CONFIG
	result |= HX_OS_CAP_CONFIG;
#if HX_OS_HAVE_LOCAL_STRTOD
	result |= HX_OS_CAP_CONFIG_DOUBLE;
#endif
#endif
	if (hx_os_atomic_is_lock_free())
		result |= HX_OS_CAP_ATOMIC64_LOCK_FREE;
#if HX_OS_ENABLE_CONTAINERS
	result |= HX_OS_CAP_CONTAINERS;
#endif
	return result;
}

const char *hx_os_platform_name(void)
{
	return HX_OS_PLATFORM_NAME;
}

hx_os_status hx_os_i_text_copy(const char *text, char *buffer, size_t capacity,
	size_t *required, hx_os_error *error)
{
	size_t bytes = strlen(text) + 1;
	if (!required || (!buffer && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*required = bytes;
	if (capacity < bytes) {
		if (capacity)
			buffer[0] = 0;
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);
	}
	memcpy(buffer, text, bytes);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_version_string(
	char *buffer, size_t capacity, size_t *required)
{
	return hx_os_i_text_copy(
		HX_OS_BUILD_VERSION, buffer, capacity, required, NULL);
}

uint32_t hx_os_abi_version(void)
{
	return HX_OS_ABI_VERSION;
}

const char *hx_os_cpu_name(void)
{
#if defined(_M_X64) || defined(__x86_64__)
	return "x86_64";
#elif defined(_M_IX86) || defined(__i386__)
	return "x86";
#elif defined(_M_ARM64) || defined(__aarch64__)
	return "aarch64";
#elif defined(__arm__)
	return "arm";
#else
	return "generic";
#endif
}
