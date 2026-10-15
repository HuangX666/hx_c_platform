#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	const char json[] = "{\"device_id\":7,\"state\":\"online\"}";
	hx_os_network *network = NULL;
	hx_os_socket *socket = NULL;
	hx_os_address bound, peer;
	hx_os_packet_info info;
	hx_os_status status;
	hx_os_deadline deadline = hx_os_deadline_after_ms(3000);
	char buffer[256], destination[64];
	size_t completed;

	status = hx_os_network_create(NULL, &network, NULL);
	if (status == HX_OS_OK)
		status = hx_os_socket_create(
			network, HX_OS_IPV4, HX_OS_UDP, &socket, NULL);
	if (status == HX_OS_OK)
		status = hx_os_socket_set_packet_info(socket, 1, NULL);
	if (status == HX_OS_OK)
		status = hx_os_address_parse("127.0.0.1", 0, &bound, NULL);
	if (status == HX_OS_OK)
		status = hx_os_socket_bind(socket, &bound, NULL);
	if (status == HX_OS_OK)
		status = hx_os_socket_local_address(socket, &bound, NULL);
	if (status == HX_OS_OK)
		status = hx_os_socket_send_to_until(socket, &bound, json,
			sizeof(json) - 1, deadline, &completed, NULL);
	if (status == HX_OS_OK)
		status = hx_os_socket_receive_packet_until(socket, &peer, &info,
			buffer, sizeof(buffer), deadline, &completed, NULL);
	if (status == HX_OS_OK) {
		printf("JSON: %.*s\n", (int)completed, buffer);
		if ((info.flags & HX_OS_PACKET_LOCAL_ADDRESS) &&
			hx_os_address_format(&info.local, destination,
				sizeof(destination), NULL) == HX_OS_OK)
			printf("Destination: %s\n", destination);
		if (info.flags & HX_OS_PACKET_INTERFACE)
			printf("Interface: %u\n",
				(unsigned)info.interface_index);
	}
	else
		fprintf(stderr, "UDP: %s\n", hx_os_status_name(status));

	if (hx_os_socket_close(&socket, NULL) != HX_OS_OK)
		return 1;
	if (hx_os_network_destroy(&network, NULL) != HX_OS_OK)
		return 1;
	return status == HX_OS_OK ? 0 : 1;
}
