#include "ipc_demo.h"
#include <errno.h>

typedef struct device {
	hx_ipc_peer owner;
	unsigned long interval_ms;
} device;

static int set_interval(const hx_ipc_message *message, unsigned long *value)
{
	const char *text = message->data;
	char *end;
	unsigned long parsed;
	if (message->bytes < 5 || memcmp(text, "SET ", 4) || text[4] < '0' ||
		text[4] > '9')
		return 0;
	errno = 0;
	parsed = strtoul(text + 4, &end, 10);
	if (errno || end != text + message->bytes || parsed < 50 ||
		parsed > 5000)
		return 0;
	*value = parsed;
	return 1;
}

static hx_os_status handle_request(hx_ipc *server,
	const hx_ipc_message *request, device devices[2],
	hx_ipc_peer *controller, int *finished, hx_os_error *error)
{
	device *value = NULL;
	char reply[96] = "ERROR unknown command";
	char notification[96];
	int changed = 0;
	hx_os_status status;
	if (request->encoding != HX_IPC_UTF8 || request->bytes > 64)
		return hx_ipc_reply_text(server, request,
			"ERROR expected a short UTF-8 command", error);
	printf("[server] device=%llu request=%.*s\n",
		(unsigned long long)request->channel, (int)request->bytes,
		(const char *)request->data);
	if (request->channel >= 101 && request->channel <= 102)
		value = &devices[request->channel - 101];
	if (demo_message_is(request, "QUIT") && !request->channel)
		if (request->peer == *controller && !devices[0].owner &&
			!devices[1].owner) {
			strcpy(reply, "BYE");
			*finished = 1;
		}
		else
			strcpy(reply, "ERROR close both devices first");
	else if (!value)
		strcpy(reply, "ERROR unknown device");
	else if (demo_message_is(request, "OPEN")) {
		if (value->owner ||
			(*controller && *controller != request->peer))
			strcpy(reply, "ERROR device already owned");
		else {
			*controller = request->peer;
			value->owner = request->peer;
			value->interval_ms = 1000;
			strcpy(reply, "OK online");
		}
	}
	else if (value->owner != request->peer)
		strcpy(reply, "ERROR open device first");
	else if (demo_message_is(request, "GET"))
		(void)snprintf(reply, sizeof(reply), "STATE interval_ms=%lu",
			value->interval_ms);
	else if (demo_message_is(request, "CLOSE")) {
		value->owner = 0;
		strcpy(reply, "OK offline");
	}
	else if (request->bytes >= 4 && !memcmp(request->data, "SET ", 4)) {
		unsigned long interval;
		if (!set_interval(request, &interval))
			strcpy(reply, "ERROR interval must be 50..5000");
		else {
			value->interval_ms = interval;
			(void)snprintf(reply, sizeof(reply),
				"OK interval_ms=%lu", interval);
			(void)snprintf(notification, sizeof(notification),
				"CHANGED interval_ms=%lu", interval);
			changed = 1;
		}
	}
	status = hx_ipc_reply_text(server, request, reply, error);
	if (status == HX_OS_OK && changed) {
		/* This is an unsolicited business message, not the request
		 * reply. */
		status = hx_ipc_send_text_until(server, request->peer,
			request->channel, notification,
			hx_os_deadline_after_ms(5000), error);
		if (status == HX_OS_OK)
			printf("[server push] device=%llu %s\n",
				(unsigned long long)request->channel,
				notification);
	}
	return status;
}

int main(int argc, char **argv)
{
	const char *address = argc > 1 ? argv[1] : "tcp://127.0.0.1:39001";
	hx_ipc *server = NULL;
	hx_string actual = HX_STRING_INIT;
	hx_os_error error = {0};
	device devices[2] = {{0}, {0}};
	hx_ipc_peer controller = 0;
	hx_os_deadline deadline = hx_os_deadline_after_ms(30000);
	unsigned int requests = 0;
	int finished = 0, ok;
	hx_os_status status;
	if (!strncmp(address, "udp://", 6)) {
		fputs("Use ipc_udp_sensor/dashboard for ordinary UDP.\n",
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
