#include "watch_private.h"
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

typedef struct hx_os_watch_node {
	struct hx_os_watch_node *next;
	size_t depth;
	int identifier;
	char path[1];
} hx_os_watch_node;
typedef struct hx_os_watch_tree {
	hx_os_watch_node *nodes;
	size_t count;
	int descriptor;
} hx_os_watch_tree;
typedef struct hx_os_watch_linux {
	hx_os_watch_tree tree;
	unsigned char *buffer;
	int stopped;
} hx_os_watch_linux;

static void hx_os_watch_tree_clear(hx_os_watch *watch, hx_os_watch_tree *tree)
{
	hx_os_watch_node *node = tree->nodes;
	if (tree->descriptor >= 0)
		(void)close(tree->descriptor);
	while (node) {
		hx_os_watch_node *next = node->next;
		hx_os_i_watch_free(watch, node);
		node = next;
	}
	tree->descriptor = -1;
	tree->nodes = NULL;
	tree->count = 0;
}

static hx_os_status hx_os_watch_tree_add(hx_os_watch *watch,
	hx_os_watch_tree *tree, int descriptor, const char *path, size_t depth,
	hx_os_error *error)
{
	hx_os_watch_node *node;
	DIR *directory;
	struct dirent *entry;
	hx_os_status status = HX_OS_OK;
	char proc_path[64];
	size_t length = strlen(path), invalid;
	int identifier;
	struct statfs filesystem;
	if (fstatfs(descriptor, &filesystem) != 0) {
		int code = errno;
		(void)close(descriptor);
		return hx_os_i_errno(error, code);
	}
	/* 9p can accept inotify marks without delivering modifications.
	 * Reject it conservatively instead of advertising silent coverage. */
	if (filesystem.f_type == 0x01021997L) {
		(void)close(descriptor);
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	}
	if (depth > watch->options.max_depth ||
		tree->count == watch->options.max_directories ||
		length >= watch->options.max_path_bytes) {
		(void)close(descriptor);
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}
	if (hx_os_text_validate_utf8(path, length, &invalid) != HX_OS_OK) {
		(void)close(descriptor);
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	}
	/* Watch the directory opened with openat/O_NOFOLLOW, not a racy name.
	 */
	(void)snprintf(
		proc_path, sizeof(proc_path), "/proc/self/fd/%d", descriptor);
	identifier = inotify_add_watch(tree->descriptor, proc_path,
		IN_ONLYDIR | IN_CREATE | IN_DELETE | IN_MODIFY | IN_ATTRIB |
			IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO |
			IN_DELETE_SELF | IN_MOVE_SELF);
	if (identifier < 0) {
		int code = errno;
		(void)close(descriptor);
		return code == ENOENT
			? hx_os_i_error(error, HX_OS_NOT_SUPPORTED,
				  HX_OS_ERROR_ERRNO, code)
			: hx_os_i_errno(error, code);
	}
	for (node = tree->nodes; node; node = node->next)
		if (node->identifier == identifier) {
			(void)close(descriptor);
			return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
		}
	node = hx_os_i_watch_alloc(watch, sizeof(*node) + length, error);
	if (!node) {
		(void)close(descriptor);
		return error->status;
	}
	node->identifier = identifier;
	node->depth = depth;
	memcpy(node->path, path, length + 1);
	node->next = tree->nodes;
	tree->nodes = node;
	++tree->count;
	if (!watch->recursive) {
		(void)close(descriptor);
		return HX_OS_OK;
	}
	directory = fdopendir(descriptor);
	if (!directory) {
		int code = errno;
		(void)close(descriptor);
		return hx_os_i_errno(error, code);
	}
	errno = 0;
	while ((entry = readdir(directory)) != NULL) {
		int child;
		char *child_path;
		size_t name_length, total;
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		child = openat(dirfd(directory), entry->d_name,
			O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW |
				O_NONBLOCK);
		if (child < 0) {
			int code = errno;
			if (code == ENOTDIR || code == ELOOP ||
				code == ENOENT) {
				errno = 0;
				continue;
			}
			status = hx_os_i_errno(error, code);
			break;
		}
		name_length = strlen(entry->d_name);
		total = length + name_length + 2;
		if (total > watch->options.max_path_bytes) {
			(void)close(child);
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
			break;
		}
		child_path = hx_os_i_watch_alloc(watch, total, error);
		if (!child_path) {
			(void)close(child);
			status = error->status;
			break;
		}
		(void)snprintf(child_path, total, "%s%s%s", path,
			length && path[length - 1] == '/' ? "" : "/",
			entry->d_name);
		status = hx_os_watch_tree_add(
			watch, tree, child, child_path, depth + 1, error);
		hx_os_i_watch_free(watch, child_path);
		if (status != HX_OS_OK)
			break;
		errno = 0;
	}
	if (status == HX_OS_OK && errno)
		status = hx_os_i_errno(error, errno);
	(void)closedir(directory);
	return status;
}

static hx_os_status hx_os_watch_tree_build(
	hx_os_watch *watch, hx_os_watch_tree *tree, hx_os_error *error)
{
	int descriptor;
	tree->descriptor = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (tree->descriptor < 0)
		return hx_os_i_errno(error, errno);
	descriptor = open(watch->root,
		O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK);
	if (descriptor < 0)
		return hx_os_i_errno(error, errno);
	return hx_os_watch_tree_add(
		watch, tree, descriptor, watch->root, 0, error);
}

hx_os_status hx_os_i_watch_native_init(hx_os_watch *watch, hx_os_error *error)
{
	hx_os_watch_linux *native;
	char resolved[PATH_MAX];
	hx_os_status status;
	if (!realpath(watch->root, resolved))
		return hx_os_i_errno(error, errno);
	status = hx_os_i_watch_root(watch, resolved, error);
	if (status != HX_OS_OK)
		return status;
	native = hx_os_i_watch_alloc(watch, sizeof(*native), error);
	if (!native)
		return error->status;
	watch->native = native;
	native->tree.descriptor = -1;
	native->stopped = -1;
	native->buffer =
		hx_os_i_watch_alloc(watch, watch->options.buffer_bytes, error);
	if (!native->buffer)
		return error->status;
	native->stopped = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (native->stopped < 0)
		return hx_os_i_errno(error, errno);
	return hx_os_watch_tree_build(watch, &native->tree, error);
}

hx_os_status hx_os_i_watch_native_start(hx_os_watch *watch, hx_os_error *error)
{
	/* inotify coverage was established during initialization. */
	(void)watch;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_watch_linux_records(
	hx_os_watch *watch, size_t bytes, int *rebuild, hx_os_error *error)
{
	hx_os_watch_linux *native = watch->native;
	size_t offset = 0, root_length = strlen(watch->root);
	while (offset < bytes && !hx_os_i_watch_stopping(watch)) {
		struct inotify_event event;
		hx_os_watch_node *node;
		const char *relative, *name;
		char *path;
		size_t parent_length, name_length, total;
		uint32_t kind, flags;
		if (bytes - offset < sizeof(event))
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		memcpy(&event, native->buffer + offset, sizeof(event));
		if (event.len > bytes - offset - sizeof(event))
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		name = (const char *)native->buffer + offset + sizeof(event);
		offset += sizeof(event) + event.len;
		if (event.mask & IN_Q_OVERFLOW) {
			*rebuild = watch->recursive;
			hx_os_i_watch_emit(watch, HX_OS_WATCH_OVERFLOW, NULL,
				NULL, 0, NULL);
			continue;
		}
		for (node = native->tree.nodes; node; node = node->next)
			if (node->identifier == event.wd)
				break;
		if (!node)
			continue;
		if (!node->depth &&
			(event.mask &
				(IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED)))
			return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
		if (event.mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED)) {
			*rebuild = watch->recursive;
			continue;
		}
		if (event.len && !memchr(name, '\0', event.len))
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		if (!event.len)
			name = "";
		relative = node->depth ? node->path + root_length +
				(watch->root[root_length - 1] == '/' ? 0 : 1)
				       : "";
		parent_length = strlen(relative);
		name_length = strlen(name);
		total = parent_length + name_length +
			(parent_length && name_length ? 2 : 1);
		path = hx_os_i_watch_scratch(watch, total, error);
		if (!path)
			return error->status;
		(void)snprintf(path, total, "%s%s%s", relative,
			parent_length && name_length ? "/" : "", name);
		flags = event.mask & IN_ISDIR ? HX_OS_WATCH_DIRECTORY : 0;
		/* inotify can split rename pairs across reads: report observed
		 * rename paths without manufacturing an atomic old/new pair. */
		kind = event.mask & (IN_MOVED_FROM | IN_MOVED_TO)
			? HX_OS_WATCH_RENAMED
			: event.mask & IN_CREATE ? HX_OS_WATCH_ADDED
			: event.mask & IN_DELETE ? HX_OS_WATCH_REMOVED
						 : HX_OS_WATCH_CHANGED;
		hx_os_i_watch_emit(watch, kind, path, NULL, flags, NULL);
		if (flags &&
			(event.mask &
				(IN_CREATE | IN_DELETE | IN_MOVED_FROM |
					IN_MOVED_TO)))
			*rebuild = watch->recursive;
	}
	return HX_OS_OK;
}

hx_os_status hx_os_i_watch_native_run(hx_os_watch *watch, hx_os_error *error)
{
	hx_os_watch_linux *native = watch->native;
	while (!hx_os_i_watch_stopping(watch)) {
		struct pollfd pollers[2];
		ssize_t bytes;
		int result, rebuild = 0;
		hx_os_status status;
		memset(pollers, 0, sizeof(pollers));
		pollers[0].fd = native->stopped;
		pollers[0].events = POLLIN;
		pollers[1].fd = native->tree.descriptor;
		pollers[1].events = POLLIN;
		result = poll(pollers, 2, -1);
		if (result < 0) {
			if (errno == EINTR)
				continue;
			return hx_os_i_errno(error, errno);
		}
		if (pollers[0].revents)
			break;
		if (pollers[1].revents & (POLLERR | POLLHUP | POLLNVAL))
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		bytes = read(native->tree.descriptor, native->buffer,
			watch->options.buffer_bytes);
		if (bytes < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return hx_os_i_errno(error, errno);
		}
		if (!bytes)
			return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);
		status = hx_os_watch_linux_records(
			watch, (size_t)bytes, &rebuild, error);
		if (status != HX_OS_OK)
			return status;
		if (rebuild && !hx_os_i_watch_stopping(watch)) {
			hx_os_watch_tree replacement = {NULL, 0, -1};
			status = hx_os_watch_tree_build(
				watch, &replacement, error);
			if (status != HX_OS_OK) {
				hx_os_watch_tree_clear(watch, &replacement);
				return status;
			}
			hx_os_watch_tree_clear(watch, &native->tree);
			native->tree = replacement;
			/* Topology changed while rescanning; callers reconcile
			 * a snapshot after new subscriptions are armed. */
			hx_os_i_watch_emit(watch, HX_OS_WATCH_OVERFLOW, NULL,
				NULL, 0, NULL);
		}
	}
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_i_watch_native_stop(hx_os_watch *watch)
{
	hx_os_watch_linux *native = watch->native;
	uint64_t value = 1;
	ssize_t result;
	if (!native || native->stopped < 0)
		return;
	do {
		result = write(native->stopped, &value, sizeof(value));
	} while (result < 0 && errno == EINTR);
}

void hx_os_i_watch_native_destroy(hx_os_watch *watch)
{
	hx_os_watch_linux *native = watch->native;
	if (!native)
		return;
	hx_os_watch_tree_clear(watch, &native->tree);
	if (native->stopped >= 0)
		(void)close(native->stopped);
	hx_os_i_watch_free(watch, native->buffer);
	hx_os_i_watch_free(watch, native);
}
