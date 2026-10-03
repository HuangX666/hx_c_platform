#include "ipc_private.h"
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>

static hx_os_status hx_ipc_directory(
	char *directory, size_t capacity, hx_os_error *error)
{
	struct stat info;
#if defined(__ANDROID__)
	(void)snprintf(directory, capacity, "/data/local/tmp/hx-ipc-%lu",
#else
	(void)snprintf(directory, capacity, "/tmp/hx-ipc-%lu",
#endif
		(unsigned long)geteuid());
	if (mkdir(directory, 0700) && errno != EEXIST)
		return hx_os_i_errno(error, errno);
	if (lstat(directory, &info))
		return hx_os_i_errno(error, errno);
	if (!S_ISDIR(info.st_mode) || info.st_uid != geteuid() ||
		(info.st_mode & 077) != 0)
		return hx_os_i_error(error, HX_OS_ACCESS_DENIED, 0, 0);
	return HX_OS_OK;
}

static hx_os_status hx_ipc_lock_path(hx_ipc *endpoint, hx_os_error *error)
{
	char lock_path[120];
	struct stat info;
	hx_os_socket *probe = NULL;
	hx_os_status status;
	hx_os_error detail;
	(void)snprintf(lock_path, sizeof(lock_path), "%s.lock", endpoint->name);
	endpoint->lock_descriptor = open(
		lock_path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (endpoint->lock_descriptor < 0)
		return hx_os_i_errno(error, errno);
	if (fstat(endpoint->lock_descriptor, &info))
		return hx_os_i_errno(error, errno);
	if (!S_ISREG(info.st_mode) || info.st_uid != geteuid() ||
		(info.st_mode & 077))
		return hx_os_i_error(error, HX_OS_ACCESS_DENIED, 0, 0);
	if (flock(endpoint->lock_descriptor, LOCK_EX | LOCK_NB))
		return hx_os_i_errno(error, errno);
	if (lstat(endpoint->name, &info))
		return errno == ENOENT ? HX_OS_OK : hx_os_i_errno(error, errno);
	if (!S_ISSOCK(info.st_mode) || info.st_uid != geteuid())
		return hx_os_i_error(error, HX_OS_EXISTS, 0, 0);
	status = hx_os_local_connect(endpoint->network, endpoint->name,
		HX_OS_DEADLINE_NOW, &probe, &detail);
	(void)hx_os_socket_close(&probe, NULL);
	if (status == HX_OS_OK || status == HX_OS_TIMEOUT ||
		detail.native_code != ECONNREFUSED)
		return hx_os_i_error(error, HX_OS_EXISTS, 0, 0);
	if (unlink(endpoint->name))
		return hx_os_i_errno(error, errno);
	return HX_OS_OK;
}

hx_os_status hx_ipc_i_native_prepare(
	hx_ipc *endpoint, const char *address, hx_os_error *error)
{
	char path[108], directory[64];
	hx_os_local_address checked;
	hx_os_status status;
	struct stat info;
	if (!strncmp(address, "local://", 8)) {
		const char *name = address + 8;
		size_t index;
		int length;
		if (!*name || strlen(name) > 64)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		for (index = 0; name[index]; ++index)
			if (!((name[index] >= 'a' && name[index] <= 'z') ||
				    (name[index] >= 'A' &&
					    name[index] <= 'Z') ||
				    (name[index] >= '0' &&
					    name[index] <= '9') ||
				    strchr("._-", name[index])))
				return hx_os_i_error(
					error, HX_OS_INVALID, 0, 0);
		status = hx_ipc_directory(directory, sizeof(directory), error);
		if (status != HX_OS_OK)
			return status;
		length = snprintf(
			path, sizeof(path), "%s/%s.sock", directory, name);
		if (length < 0 || (size_t)length >= sizeof(path))
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	}
	else if (!strncmp(address, "unix:///", 8)) {
		if (strlen(address + 7) >= sizeof(path))
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
		memcpy(path, address + 7, strlen(address + 7) + 1);
	}
	else
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	status = hx_os_local_address_init(&checked, path, error);
	if (status != HX_OS_OK)
		return status;
	endpoint->name = hx_os_i_alloc(endpoint->context, strlen(path) + 1, 0);
	if (!endpoint->name)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	memcpy(endpoint->name, path, strlen(path) + 1);
	status = hx_string_assign_cstr(
		&endpoint->address_text, "unix://", error);
	if (status == HX_OS_OK)
		status = hx_string_append_cstr(
			&endpoint->address_text, path, error);
	if (status != HX_OS_OK || !endpoint->server)
		return status;
	status = hx_ipc_lock_path(endpoint, error);
	if (status == HX_OS_OK)
		status = hx_os_local_listen(endpoint->network, path,
			(int)endpoint->options.max_peers, &endpoint->listener,
			error);
	if (status != HX_OS_OK)
		return status;
	if (lstat(path, &info))
		return hx_os_i_errno(error, errno);
	endpoint->path_device = (uint64_t)info.st_dev;
	endpoint->path_inode = (uint64_t)info.st_ino;
	if (chmod(path, 0600))
		return hx_os_i_errno(error, errno);
	return HX_OS_OK;
}

hx_os_status hx_ipc_i_native_connect(
	hx_ipc_connection *peer, hx_os_error *error)
{
	hx_os_status status;
	if (peer->socket)
		status = hx_os_local_socket_finish_connect(
			peer->socket, HX_OS_DEADLINE_NOW, error);
	else
		status = hx_os_local_connect(peer->endpoint->network,
			peer->endpoint->name, HX_OS_DEADLINE_NOW, &peer->socket,
			error);
	if (status == HX_OS_TIMEOUT || status == HX_OS_NOT_FOUND ||
		(status != HX_OS_OK && error &&
			error->domain == HX_OS_ERROR_ERRNO &&
			error->native_code == ECONNREFUSED))
		return hx_os_i_error(error, HX_OS_WOULD_BLOCK,
			error ? error->domain : 0,
			error ? error->native_code : 0);
	return status;
}

static void hx_ipc_local_ready(hx_os_loop_watch *watch, int events, void *user)
{
	hx_ipc *endpoint = user;
	size_t count;
	(void)watch;
	(void)events;
	if (hx_ipc_i_stopping(endpoint))
		return;
	for (count = 0; count < endpoint->options.max_peers; ++count) {
		hx_os_socket *socket = NULL;
		hx_os_error error;
		hx_os_status status =
			hx_os_local_socket_accept_until(endpoint->listener,
				HX_OS_DEADLINE_NOW, &socket, &error);
		if (status == HX_OS_TIMEOUT)
			break;
		if (status != HX_OS_OK) {
			hx_ipc_i_failed(endpoint, &error);
			break;
		}
		hx_ipc_i_accepted(endpoint, socket, NULL, NULL);
	}
}

hx_os_status hx_ipc_i_native_accept(
	hx_ipc *endpoint, int first, hx_os_error *error)
{
	(void)first;
	return hx_os_loop_attach_fd(endpoint->loop, endpoint->listener->native,
		HX_OS_WAIT_READ, hx_ipc_local_ready, endpoint,
		&endpoint->local_watch, error);
}

hx_os_status hx_ipc_i_pipe_write_end(hx_ipc *endpoint, hx_os_pipe *reader,
	hx_os_pipe **writer, hx_os_error *error)
{
	(void)endpoint;
	(void)reader;
	(void)writer;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

void hx_ipc_i_native_cleanup(hx_ipc *endpoint)
{
	struct stat info;
	if (endpoint->path_inode && endpoint->name &&
		!lstat(endpoint->name, &info) &&
		(uint64_t)info.st_dev == endpoint->path_device &&
		(uint64_t)info.st_ino == endpoint->path_inode)
		(void)unlink(endpoint->name);
	if (endpoint->lock_descriptor >= 0)
		(void)close(endpoint->lock_descriptor);
}
