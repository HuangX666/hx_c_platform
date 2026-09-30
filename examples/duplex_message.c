#include <hx_os.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
	hx_os_file *first = NULL, *second = NULL;
	hx_os_error error;
	hx_os_status status;
	char bytes[16];
	size_t transferred, received = 0;
	status = hx_os_pipe_pair_socket(NULL, &first, &second, &error);
	if (status != HX_OS_OK)
		return 1;
	status = hx_os_file_write_all(first, "duplex", 6, &transferred, &error);
	if (status == HX_OS_OK)
		status = hx_os_pipe_socket_shutdown(
			first, HX_OS_SHUT_WRITE, &error);
	while (status == HX_OS_OK && received < 6) {
		status = hx_os_file_read_some(second, bytes + received,
			6 - received, &transferred, &error);
		received += transferred;
	}
	if (status == HX_OS_OK && received == 6 && !memcmp(bytes, "duplex", 6))
		puts("Created and used a portable duplex channel.");
	(void)hx_os_file_close(&first, NULL);
	(void)hx_os_file_close(&second, NULL);
	return status == HX_OS_OK && received == 6 ? 0 : 1;
}
