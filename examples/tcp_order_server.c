#include "tcp_business.h"

static int send_order_json(
	tcp_stream *stream, const json_field *fields, size_t count)
{
	hx_string wire = HX_STRING_INIT;

	int ok = json_encode(fields, count, &wire) &&
		tcp_send_text(stream, hx_string_c_str(&wire));

	hx_string_destroy(&wire);

	return ok;
}

static int process_orders(tcp_stream *stream)
{
	unsigned int received, seen = 0, accepted = 0, total = 0;

	for (received = 0; received < 3; ++received) {
		char request[TCP_TEXT_CAPACITY];
		hx_os_config_document *document;
		const hx_os_config_node *root;
		hx_os_config_slice note;
		int64_t id = 0, sku = 0, quantity = 0;
		int ok;

		if (!tcp_receive_text(stream, request))
			return 0;

		printf("[TCP server] %s\n", request);
		document = json_decode(request, strlen(request));
		root = hx_os_config_root(document);
		if (!document || !json_has_text(root, "type", "order") ||
			!json_get_int(root, "id", &id) || id < 1 || id > 3 ||
			(seen & (1u << (unsigned int)id))) {
			json_field fields[] = {json_text("type", "error"),
				json_bool("ok", 0),
				json_text("reason", "invalid_json_request")};
			ok = send_order_json(stream, fields, 3);

			hx_os_config_release(document);
			if (!ok)
				return 0;

			continue;
		}

		seen |= 1u << (unsigned int)id;
		if (!json_get_int(root, "sku", &sku) ||
			!json_get_int(root, "quantity", &quantity) ||
			(sku != 101 && sku != 102) || quantity < 1 ||
			quantity > 10 ||
			hx_os_config_get_string(
				json_member(root, "note"), &note) != HX_OS_OK ||
			note.bytes > 128) {
			json_field fields[] = {json_text("type", "rejected"),
				json_int("id", id), json_bool("ok", 0),
				json_text("reason", "invalid_order")};
			ok = send_order_json(stream, fields, 4);
		}
		else {
			unsigned int amount = (unsigned int)quantity *
				(sku == 101 ? 1250u : 9000u);

			json_field receipt[] = {json_text("type", "receipt"),
				json_int("id", id), json_bool("ok", 1),
				json_int("total_cents", amount),
				json_slice("note", note)};

			json_field shipped[] = {json_text("type", "shipped"),
				json_int("id", id), json_bool("ok", 1)};
			total += amount;
			++accepted;
			ok = send_order_json(stream, receipt, 5) &&
				send_order_json(stream, shipped, 3);
		}

		hx_os_config_release(document);
		if (!ok)
			return 0;
	}

	printf("TCP SERVER PASS: accepted=%u total_cents=%u\n", accepted,
		total);

	return accepted == 2 && total == 11500;
}

int main(int argc, char **argv)
{
	uint16_t port = argc > 1 ? net_demo_port(argv[1]) : 39001;

	hx_os_network *network = NULL;
	hx_os_socket *listener = NULL;
	tcp_stream stream = {0};
	hx_os_error error = {0};
	hx_os_status status = HX_OS_OK;

	int ok = net_demo_open(&network, &listener, HX_OS_TCP, port);
	if (ok)
		ok = net_demo_check(hx_os_socket_listen(listener, 1, &error),
			     &error, "listen") &&
			net_demo_listening(listener);

	stream.deadline = hx_os_deadline_after_ms(15000);
	if (ok) {
		status = hx_os_socket_wait_until(
			listener, HX_OS_WAIT_READ, stream.deadline, &error);
		if (status == HX_OS_OK)
			status = hx_os_socket_accept(
				listener, &stream.socket, NULL, &error);

		ok = net_demo_check(status, &error, "accept");
	}

	if (ok)
		ok = net_demo_check(
			hx_os_mutex_create(NULL, 0, &stream.access, &error),
			&error, "socket mutex");

	if (ok)
		ok = process_orders(&stream);

	hx_os_mutex_destroy(stream.access);
	if (!net_demo_check(hx_os_socket_close(&stream.socket, &error), &error,
		    "client close"))
		ok = 0;
	if (!net_demo_close(&listener, &network))
		ok = 0;

	return ok ? 0 : 1;
}
