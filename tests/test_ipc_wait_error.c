/* A static test fixture injects a native wait error into the real IPC code. */
#include "../src/ipc_private.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

#if HX_OS_HAVE_IPC
static hx_os_status test_condition_wait(hx_os_condition *condition,
	hx_os_mutex *mutex, hx_os_deadline deadline, hx_os_error *error)
{
	(void)condition;
	(void)mutex;
	(void)deadline;
	if (error)
		*error = (hx_os_error){
			HX_OS_IO_ERROR, HX_OS_ERROR_ERRNO, 123, 7};

	return HX_OS_IO_ERROR;
}
#endif

#define hx_os_condition_wait_until test_condition_wait
#include "../src/ipc.c"
#undef hx_os_condition_wait_until

int main(void)
{
	hx_ipc endpoint = {0};
	hx_ipc_connection connection = {0};
	hx_ipc_peer peer = 42;
	hx_os_error error = {0};

	if (!(hx_os_capabilities() & HX_OS_CAP_IPC))
		return 0;

	CHECK(hx_os_mutex_create(NULL, 0, &endpoint.mutex, NULL) == HX_OS_OK);
	CHECK(hx_os_condition_create(NULL, &endpoint.condition, NULL) ==
		HX_OS_OK);
	connection.state = 1;
	endpoint.peers = &connection;
	CHECK(hx_ipc_wait_connected_until(&endpoint, HX_OS_DEADLINE_NOW, &peer,
		      &error) == HX_OS_IO_ERROR);
	CHECK(!peer && !endpoint.users);
	CHECK(error.status == HX_OS_IO_ERROR &&
		error.domain == HX_OS_ERROR_ERRNO);
	CHECK(error.native_code == 123 && error.cause_code == 7);

	hx_os_condition_destroy(endpoint.condition);
	hx_os_mutex_destroy(endpoint.mutex);
	puts("IPC native wait error details preserved");

	return 0;
}
