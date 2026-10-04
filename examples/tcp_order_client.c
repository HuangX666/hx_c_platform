#include "tcp_business.h"

static const char order_note[] = "桌面 \"设备\"\\A🌐";

typedef struct order_job {
	tcp_stream *stream;
	int ok;
} order_job;

static void *HX_OS_CALL send_orders(void *user)
{
	order_job *job = user;
	int id;

	for (id = 1; id <= 3; ++id) {
		json_field fields[] = {json_text("type", "order"),
			json_int("id", id),
			json_int("sku", id == 2 ? 102 : 101),
			json_int("quantity",
				id == 1		  ? 2
					: id == 2 ? 1
						  : 0),
			json_text("note", order_note)};

		hx_string wire = HX_STRING_INIT;

		int ok = json_encode(fields, 5, &wire) &&
			tcp_send_text(job->stream, hx_string_c_str(&wire));
		if (ok)
			printf("[TCP send thread] %s\n",
				hx_string_c_str(&wire));

		hx_string_destroy(&wire);
		if (!ok)
			return NULL;
	}

	job->ok = 1;

	return NULL;
}

static void *HX_OS_CALL receive_receipts(void *user)
{
	order_job *job = user;
	const char *types[] = {
		"receipt", "shipped", "receipt", "shipped", "rejected"};
	const int ids[] = {1, 1, 2, 2, 3};
	size_t index;

	for (index = 0; index < 5; ++index) {
		char reply[TCP_TEXT_CAPACITY];
		hx_os_config_document *document;
		const hx_os_config_node *root;
		int ok;

		if (!tcp_receive_text(job->stream, reply))
			return NULL;

		document = json_decode(reply, strlen(reply));
		root = hx_os_config_root(document);
		ok = document && json_has_text(root, "type", types[index]) &&
			json_has_int(root, "id", ids[index]) &&
			json_has_bool(root, "ok", index != 4);
		if (index == 0 || index == 2)
			ok = ok &&
				json_has_int(root, "total_cents",
					index == 0 ? 2500 : 9000) &&
				json_has_text(root, "note", order_note);

		if (index == 4)
			ok = ok &&
				json_has_text(root, "reason", "invalid_order");

		hx_os_config_release(document);
		printf("[TCP receive thread] %s\n", reply);
		if (!ok) {
			fputs("Unexpected JSON order result.\n", stderr);

			return NULL;
		}
	}

	job->ok = 1;

	return NULL;
}

int main(int argc, char **argv)
{
	uint16_t port = argc > 1 ? net_demo_port(argv[1]) : 39001;

	hx_os_network *network = NULL;
	tcp_stream stream = {0};
	hx_os_address address;
	hx_os_thread *threads[2] = {NULL};
	order_job sender = {&stream, 0}, receiver = {&stream, 0};
	hx_os_error error = {0};
	hx_os_status status = HX_OS_OK;
	unsigned int started = 0, index;

	int ok = net_demo_open(&network, &stream.socket, HX_OS_TCP, 0);
	stream.deadline = hx_os_deadline_after_ms(10000);
	if (ok) {
		status = hx_os_address_parse(
			"127.0.0.1", port, &address, &error);
		if (status == HX_OS_OK)
			status = hx_os_socket_connect_until(stream.socket,
				&address, stream.deadline, &error);

		if (status == HX_OS_OK)
			status = hx_os_mutex_create(
				NULL, 0, &stream.access, &error);

		ok = net_demo_check(status, &error, "connect TCP");
	}

	if (ok) {
		status = hx_os_thread_create(
			NULL, receive_receipts, &receiver, &threads[0], &error);
		if (status == HX_OS_OK) {
			started = 1;

			status = hx_os_thread_create(NULL, send_orders, &sender,
				&threads[1], &error);
			if (status == HX_OS_OK)
				started = 2;
		}

		ok = net_demo_check(status, &error, "start TCP threads");
	}

	for (index = 0; index < started; ++index)
		net_demo_join(&threads[index], hx_os_deadline_after_ms(12000));

	ok = ok && sender.ok && receiver.ok;

	hx_os_mutex_destroy(stream.access);
	if (!net_demo_close(&stream.socket, &network))
		ok = 0;

	if (ok)
		puts("TCP CLIENT PASS: 3 orders, 2 receipts, 2 notices, 1 "
		     "rejection");

	return ok ? 0 : 1;
}
