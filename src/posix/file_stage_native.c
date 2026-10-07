#include "file_utils_private.h"
#include "file_private.h"

#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/acl.h>
#endif

hx_os_status hx_os_i_file_check_private(hx_os_file *file, hx_os_error *error)
{
	struct stat info;
#if defined(__APPLE__)
	acl_t acl = acl_init(0);
	int code;

	if (!acl)
		return hx_os_i_errno(error, errno);
	code = acl_set_fd_np(file->descriptor, acl, ACL_TYPE_EXTENDED) ? errno
								       : 0;
	(void)acl_free(acl);
	if (code)
		return hx_os_i_errno(error, code);
#endif
	/* Mode bits alone do not exclude inherited named-user ACEs on Darwin.
	 */
	if (fstat(file->descriptor, &info))
		return hx_os_i_errno(error, errno);
	if ((info.st_mode & 077) || info.st_uid != geteuid())
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_file_create_private(hx_os_context *context,
	const char *path, hx_os_file **out, hx_os_error *error)
{
	hx_os_status status = hx_os_file_open(context, path,
		HX_OS_FILE_WRITE | HX_OS_FILE_CREATE | HX_OS_FILE_EXCLUSIVE,
		out, error);

	if (status != HX_OS_OK)
		return status;
	/* Some mounted filesystems ignore the 0600 creation mode. Never stage
	 * plaintext there under a false promise of private permissions. */
	status = hx_os_i_file_check_private(*out, error);
	if (status != HX_OS_OK) {
		(void)hx_os_file_close(out, NULL);
		(void)unlink(path);
	}

	return status;
}

hx_os_status hx_os_i_file_publish(const char *temporary, const char *path,
	int replace, hx_os_error *error)
{
	hx_os_status status = hx_os_file_move(temporary, path, replace, error);

	if (status != HX_OS_NOT_SUPPORTED || replace)
		return status;
	/* This is our completed regular temporary, not a general move. link()
	 * publishes the new name atomically without replacing any old name.
	 * Removing the private staging name is best-effort after publication.
	 */
	if (link(temporary, path))
		return hx_os_i_errno(error, errno);
	(void)unlink(temporary);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
