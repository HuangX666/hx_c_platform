#include "file_private.h"
#include <sys/types.h>
#include <unistd.h>

hx_os_status hx_os_i_file_at(hx_os_file *file, int writing, uint64_t offset,
	void *data, size_t bytes, size_t *completed, hx_os_error *error)
{
	ssize_t count;

	if ((off_t)offset < 0 || (uint64_t)(off_t)offset != offset)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	do {
		count = writing
			? pwrite(file->descriptor, data, bytes, (off_t)offset)
			: pread(file->descriptor, data, bytes, (off_t)offset);
	} while (count < 0 && errno == EINTR);
	if (count < 0)
		return hx_os_i_errno(error, errno);

	*completed = (size_t)count;

	return hx_os_i_error(error,
		count		  ? HX_OS_OK
			: writing ? HX_OS_IO_ERROR
				  : HX_OS_EOF,
		0, 0);
}
