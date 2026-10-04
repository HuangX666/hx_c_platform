#include "ipc_demo.h"

typedef struct device {
	hx_ipc_peer owner;
	unsigned long interval_ms;
} device;

static hx_os_status reply_json(hx_ipc *server, const hx_ipc_message *request,
	const json_field *fields, size_t count, hx_os_error *error)
{
	hx_string wire = HX_STRING_INIT;
	hx_os_status status = HX_OS_IO_ERROR;

	if (json_encode(fields, count, &wire))
		status = hx_ipc_reply_text(
			server, request, hx_string_c_str(&wire), error);

	hx_string_destroy(&wire);

	return status;
}

static hx_os_status handle_request(hx_ipc *server,
	const hx_ipc_message *request, device devices[2],
	hx_ipc_peer *controller, int *finished, hx_os_error *error)
{
	hx_os_config_document *document = request->encoding == HX_IPC_UTF8
		? json_decode(request->data, request->bytes)
		: NULL;

	const hx_os_config_node *root = hx_os_config_root(document);

	int64_t device_id = 0, interval = 0;
	device *value = NULL;
	const char *type = "error", *code = "unknown_operation";
	int changed = 0;
	hx_os_status status;

	if (!document || !json_get_int(root, "device_id", &device_id) ||
		device_id < 0 || (uint64_t)device_id != request->channel) {
		json_field fields[] = {json_text("type", "error"),
			json_bool("ok", 0),
			json_text("code", "invalid_request")};

		hx_os_config_release(document);

		return reply_json(server, request, fields, 3, error);
	}

	printf("[IPC server] %.*s\n", (int)request->bytes,
		(const char *)request->data);
	if (device_id >= 101 && device_id <= 102)
		value = &devices[device_id - 101];

	if (json_has_text(root, "type", "quit") && device_id == 0)
		if (request->peer == *controller && !devices[0].owner &&
			!devices[1].owner) {
			type = "bye";
			*finished = 1;
		}
		else
			code = "close_devices_first";
	else if (!value)
		code = "unknown_device";
	else if (json_has_text(root, "type", "open")) {
		if (value->owner ||
			(*controller && *controller != request->peer))
			code = "already_owned";
		else {
			*controller = request->peer;
			value->owner = request->peer;
			value->interval_ms = 1000;
			type = "opened";
		}
	}
	else if (value->owner != request->peer)
		code = "open_device_first";
	else if (json_has_text(root, "type", "get"))
		type = "state";
	else if (json_has_text(root, "type", "close")) {
		value->owner = 0;
		type = "closed";
	}
	else if (json_has_text(root, "type", "set")) {
		if (!json_get_int(root, "interval_ms", &interval) ||
			interval < 50 || interval > 5000)
			code = "invalid_interval";
		else {
			value->interval_ms = (unsigned long)interval;
			type = "updated";
			changed = 1;
		}
	}

	{
		int ok = strcmp(type, "error") != 0;

		json_field fields[] = {json_text("type", type),
			json_int("device_id", device_id), json_bool("ok", ok),
			json_int("interval_ms", value ? value->interval_ms : 0),
			json_text("code", ok ? "" : code)};

		status = reply_json(server, request, fields, 5, error);
	}

	hx_os_config_release(document);
	if (status == HX_OS_OK && changed) {
		json_field fields[] = {json_text("type", "changed"),
			json_int("device_id", device_id),
			json_int("interval_ms", interval)};

		hx_string wire = HX_STRING_INIT;

		status = HX_OS_IO_ERROR;
		if (json_encode(fields, 3, &wire)) {
			status = hx_ipc_send_text_until(server, request->peer,
				request->channel, hx_string_c_str(&wire),
				hx_os_deadline_after_ms(5000), error);
			if (status == HX_OS_OK)
				printf("[IPC push] %s\n",
					hx_string_c_str(&wire));
		}

		hx_string_destroy(&wire);
	}

	return status;
}

int main(int argc, char **argv)
{
	const char *address = argc > 1 ? argv[1] : "local://device-demo";
	hx_ipc *server = NULL;
	hx_string actual = HX_STRING_INIT;
	hx_os_error error = {0};
	device devices[2] = {{0}, {0}};
	hx_ipc_peer controller = 0;

	hx_os_deadline deadline = hx_os_deadline_after_ms(30000);

	unsigned int requests = 0;
	int finished = 0, ok;
	hx_os_status status;

	if (strncmp(address, "local://", 8) && strncmp(address, "pipe://", 7) &&
		strncmp(address, "unix://", 7)) {
		fputs("This device demo only accepts native local IPC.\n",
			stderr);

		return 2;
	}

	status = hx_ipc_listen(address, &server, &error);
	if (status == HX_OS_OK)
		status = hx_ipc_address(server, &actual, &error);

	if (status == HX_OS_OK) {
		printf("Listening: %s\n", hx_string_c_str(&actual));
		fflush(stdout);
	}

	while (status == HX_OS_OK && !finished) {
		hx_ipc_message *message = NULL;

		status = hx_ipc_receive_until(
			server, deadline, &message, &error);
		if (status != HX_OS_OK)
			break;

		if (message->kind == HX_IPC_REQUEST) {
			if (++requests > 64)
				status = HX_OS_LIMIT;
			else
				status =
					handle_request(server, message, devices,
						&controller, &finished, &error);
		}
		else if (message->kind == HX_IPC_DISCONNECTED) {
			unsigned int index;

			for (index = 0; index < 2; ++index)
				if (devices[index].owner == message->peer)
					devices[index].owner = 0;

			if (controller == message->peer)
				controller = 0;

			puts("[server] disconnected session released its "
			     "devices");
		}
		else if (message->kind == HX_IPC_OVERFLOW)
			status = HX_OS_LIMIT;

		hx_ipc_message_release(message);
	}

	ok = demo_check(status, &error, "device service") && finished;
	if (!demo_close(&server))
		ok = 0;

	hx_string_destroy(&actual);
	if (ok)
		puts("DEVICE SERVER PASS: devices closed, service stopped");

	return ok ? 0 : 1;
}
