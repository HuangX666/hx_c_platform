#include "watch_private.h"
#include <CoreServices/CoreServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <sys/stat.h>

typedef struct hx_os_watch_apple {
	FSEventStreamRef stream;
	hx_os_atomic_ptr run_loop;
	hx_os_error failure;
	int scheduled;
	int started;
} hx_os_watch_apple;

static void hx_os_watch_apple_events(ConstFSEventStreamRef stream, void *user,
	size_t count, void *paths, const FSEventStreamEventFlags flags[],
	const FSEventStreamEventId identifiers[])
{
	hx_os_watch *watch = user;
	hx_os_watch_apple *native = watch->native;
	char **names = paths;

	size_t root_length = strlen(watch->root), i;
	(void)stream;
	(void)identifiers;
	if (count > watch->options.buffer_bytes / sizeof(hx_os_watch_event)) {
		hx_os_i_watch_emit(
			watch, HX_OS_WATCH_OVERFLOW, NULL, NULL, 0, NULL);

		return;
	}

	for (i = 0; i < count && !hx_os_i_watch_stopping(watch); ++i) {
		const char *relative;
		uint32_t kind, attributes;

		if (flags[i] &
			(kFSEventStreamEventFlagRootChanged |
				kFSEventStreamEventFlagUnmount)) {
			(void)hx_os_i_error(
				&native->failure, HX_OS_NOT_FOUND, 0, 0);
			CFRunLoopStop(CFRunLoopGetCurrent());

			return;
		}

		if (flags[i] &
			(kFSEventStreamEventFlagMustScanSubDirs |
				kFSEventStreamEventFlagUserDropped |
				kFSEventStreamEventFlagKernelDropped |
				kFSEventStreamEventFlagEventIdsWrapped)) {
			hx_os_i_watch_emit(watch, HX_OS_WATCH_OVERFLOW, NULL,
				NULL, 0, NULL);
			continue;
		}

		if (strncmp(names[i], watch->root, root_length))
			continue;

		relative = names[i] + root_length;
		if (root_length != 1 && *relative && *relative != '/')
			continue;

		if (*relative == '/')
			++relative;

		if (!watch->recursive && strchr(relative, '/'))
			continue;

		kind = flags[i] & kFSEventStreamEventFlagItemRenamed
			? HX_OS_WATCH_RENAMED
			: flags[i] & kFSEventStreamEventFlagItemRemoved
			? HX_OS_WATCH_REMOVED
			: flags[i] & kFSEventStreamEventFlagItemCreated
			? HX_OS_WATCH_ADDED
			: HX_OS_WATCH_CHANGED;
		attributes = flags[i] & kFSEventStreamEventFlagItemIsDir
			? HX_OS_WATCH_DIRECTORY
			: 0;
		hx_os_i_watch_emit(
			watch, kind, relative, NULL, attributes, NULL);
	}
}

hx_os_status hx_os_i_watch_native_init(hx_os_watch *watch, hx_os_error *error)
{
	char resolved[PATH_MAX];
	struct stat info;
	CFStringRef path;
	CFArrayRef paths;
	FSEventStreamContext context = {0, watch, NULL, NULL, NULL};
	hx_os_watch_apple *native;
	hx_os_status status;

	if (!realpath(watch->root, resolved) || stat(resolved, &info))
		return hx_os_i_errno(error, errno);

	if (!S_ISDIR(info.st_mode))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_i_watch_root(watch, resolved, error);
	if (status != HX_OS_OK)
		return status;

	native = hx_os_i_watch_alloc(watch, sizeof(*native), error);
	if (!native)
		return error->status;

	watch->native = native;
	path = CFStringCreateWithCString(
		NULL, watch->root, kCFStringEncodingUTF8);
	if (!path)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	paths = CFArrayCreate(
		NULL, (const void **)&path, 1, &kCFTypeArrayCallBacks);
	CFRelease(path);
	if (!paths)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	native->stream = FSEventStreamCreate(NULL, hx_os_watch_apple_events,
		&context, paths, kFSEventStreamEventIdSinceNow, 0.05,
		kFSEventStreamCreateFlagFileEvents |
			kFSEventStreamCreateFlagWatchRoot |
			kFSEventStreamCreateFlagNoDefer);
	CFRelease(paths);
	if (!native->stream)
		return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_watch_native_start(hx_os_watch *watch, hx_os_error *error)
{
	hx_os_watch_apple *native = watch->native;

	CFRunLoopRef loop = CFRunLoopGetCurrent();
	CFRetain(loop);
	(void)hx_os_atomic_ptr_exchange(&native->run_loop, loop, HX_OS_RELEASE);
	// Keep callbacks on our joinable worker, including the 10.13 baseline.
	// Dispatch-queue delivery would change that ownership contract.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
	FSEventStreamScheduleWithRunLoop(
		native->stream, loop, kCFRunLoopDefaultMode);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
	native->scheduled = 1;
	if (!FSEventStreamStart(native->stream)) {
		FSEventStreamInvalidate(native->stream);
		native->scheduled = 0;

		return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
	}

	native->started = 1;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_watch_native_run(hx_os_watch *watch, hx_os_error *error)
{
	hx_os_watch_apple *native = watch->native;

	while (!hx_os_i_watch_stopping(watch) &&
		native->failure.status == HX_OS_OK)
		(void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, 0);
	FSEventStreamStop(native->stream);
	native->started = 0;
	FSEventStreamInvalidate(native->stream);
	native->scheduled = 0;
	if (error)
		*error = native->failure;

	return native->failure.status;
}

void hx_os_i_watch_native_stop(hx_os_watch *watch)
{
	hx_os_watch_apple *native = watch->native;
	CFRunLoopRef loop;

	if (!native)
		return;

	loop = (CFRunLoopRef)hx_os_atomic_ptr_load(
		&native->run_loop, HX_OS_ACQUIRE);
	if (loop) {
		CFRunLoopStop(loop);
		CFRunLoopWakeUp(loop);
	}
}

void hx_os_i_watch_native_destroy(hx_os_watch *watch)
{
	hx_os_watch_apple *native = watch->native;
	CFRunLoopRef loop;

	if (!native)
		return;

	if (native->stream) {
		if (native->started)
			FSEventStreamStop(native->stream);

		if (native->scheduled)
			FSEventStreamInvalidate(native->stream);

		FSEventStreamRelease(native->stream);
	}

	loop = (CFRunLoopRef)hx_os_atomic_ptr_load(
		&native->run_loop, HX_OS_RELAXED);
	if (loop)
		CFRelease(loop);

	hx_os_i_watch_free(watch, native);
}
