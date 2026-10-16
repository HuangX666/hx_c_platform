#include "tls_json.h"

typedef struct telemetry_job {
	tls_session *session;
	int ok;
} telemetry_job;

static void *HX_OS_CALL send_telemetry(void *user)
{
	telemetry_job *job = user;
	const int temperature[] = {20, 81, 22};
	int i;

	for (i = 0; i < 3; ++i) {
		json_field fields[] = {json_text("type", "reading"),
			json_text("device", "温度传感器🌐"),
			json_int("id", i + 1),
			json_int("celsius", temperature[i])};

		if (!tls_json_send(job->session, fields, 4))
			return NULL;
		printf("[TLS send thread] reading id=%d\n", i + 1);
	}
	job->ok = 1;

	return NULL;
}

static void *HX_OS_CALL receive_responses(void *user)
{
	telemetry_job *job = user;
	const char *types[] = {"accepted", "accepted", "alarm", "accepted"};
	const int ids[] = {1, 2, 2, 3};
	size_t i;

	for (i = 0; i < 4; ++i) {
		hx_os_config_document *document =
			tls_json_receive(job->session);
		const hx_os_config_node *root = hx_os_config_root(document);
		int ok = document && json_has_text(root, "type", types[i]) &&
			json_has_int(root, "id", ids[i]);

		hx_os_config_release(document);
		if (!ok)
			return NULL;
	}
	job->ok = 1;

	return NULL;
}

int main(int argc, char **argv)
{
	hx_os_tls *owner = NULL;
	hx_os_tls_options_ex options = HX_OS_TLS_OPTIONS_EX_INIT;
	hx_os_error error = {0};
	tls_session session = {0};
	telemetry_job sender = {&session, 0}, receiver = {&session, 0};
	hx_os_thread *send_thread = NULL, *receive_thread = NULL;
	hx_os_status status;
	int ok, basic = argc == 4 && !strcmp(argv[3], "--basic");

	if (argc != 3 && !basic) {
		fputs("Usage: tls_json_client PORT CA_PEM [--basic]\n", stderr);

		return 2;
	}
	options.base.ca_file = argv[2];
	options.base.server_name = "localhost";
	options.base.flags = HX_OS_TLS_NODELAY;
	options.socket.nodelay = 1;
	options.socket.keepalive = 1;
	options.socket.keepalive_idle_seconds = 30;
	options.socket.keepalive_interval_seconds = 5;
	if (basic)
		status = hx_os_tls_connect("localhost", net_demo_port(argv[1]),
			&options.base, &owner, &error);
	else
		status = hx_os_tls_connect_ex_until(NULL, "localhost",
			net_demo_port(argv[1]), &options,
			hx_os_deadline_after_ms(5000), &owner, &error);
	ok = net_demo_check(status, &error, "verified TLS connect");
	session.tls = owner;
	session.deadline = hx_os_deadline_after_ms(10000);
	if (ok) {
		status = hx_os_thread_create(NULL, receive_responses, &receiver,
			&receive_thread, &error);
		if (status == HX_OS_OK)
			status = hx_os_thread_create(NULL, send_telemetry,
				&sender, &send_thread, &error);
		ok = net_demo_check(status, &error, "TLS application threads");
	}
	if (send_thread)
		net_demo_join(&send_thread, hx_os_deadline_after_ms(12000));
	if (receive_thread)
		net_demo_join(&receive_thread, hx_os_deadline_after_ms(12000));
	ok = ok && sender.ok && receiver.ok;
	if (!tls_json_close(&owner, ok))
		ok = 0;
	if (ok)
		puts("TLS CLIENT PASS: 3 JSON readings, 3 replies, 1 alarm");

	return ok ? 0 : 1;
}
