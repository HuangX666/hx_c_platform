#include "tls_json.h"

static int process_readings(tls_session *session)
{
	int i;

	for (i = 1; i <= 3; ++i) {
		hx_os_config_document *document = tls_json_receive(session);
		const hx_os_config_node *root = hx_os_config_root(document);
		int64_t id = 0, temperature = 0;
		int ok = document && json_has_text(root, "type", "reading") &&
			json_get_int(root, "id", &id) && id == i &&
			json_get_int(root, "celsius", &temperature);

		hx_os_config_release(document);
		if (!ok)
			return 0;
		{
			json_field reply[] = {json_text("type", "accepted"),
				json_int("id", id)};

			if (!tls_json_send(session, reply, 2))
				return 0;
		}
		if (temperature > 80) {
			json_field alarm[] = {json_text("type", "alarm"),
				json_int("id", id),
				json_text("reason", "too_hot")};

			if (!tls_json_send(session, alarm, 3))
				return 0;
		}
	}

	return 1;
}

int main(int argc, char **argv)
{
	hx_os_tls *owner = NULL;
	hx_os_tls_options options = HX_OS_TLS_OPTIONS_INIT;
	hx_os_address address;
	hx_os_error error = {0};
	tls_session session = {0};
	hx_os_status status;
	int ok;

	if (argc != 4) {
		fputs("Usage: tls_json_server PORT CERT_PEM KEY_PEM\n", stderr);

		return 2;
	}
	options.certificate_file = argv[2];
	options.private_key_file = argv[3];
	status = hx_os_tls_listen(
		"127.0.0.1", net_demo_port(argv[1]), &options, &owner, &error);
	if (status == HX_OS_OK)
		status = hx_os_tls_local_address(owner, &address, &error);
	ok = net_demo_check(status, &error, "TLS listen");
	if (ok) {
		printf("Listening: %u\n", (unsigned)address.port);
		(void)fflush(stdout);
		session.tls = owner;
		session.deadline = hx_os_deadline_after_ms(10000);
		status = hx_os_tls_accept_until(
			owner, session.deadline, &session.peer, &error);
		ok = net_demo_check(status, &error, "TLS accept");
	}
	if (ok)
		ok = process_readings(&session);
	if (!tls_json_close(&owner, ok))
		ok = 0;
	if (ok)
		puts("TLS SERVER PASS: 3 JSON readings processed");

	return ok ? 0 : 1;
}
