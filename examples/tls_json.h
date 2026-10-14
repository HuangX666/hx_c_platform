#ifndef HX_EXAMPLE_TLS_JSON_H
#define HX_EXAMPLE_TLS_JSON_H

#include "network_demo.h"
#include "business_json.h"
#include <hx_os_tls.h>

typedef struct tls_session {
	hx_os_tls *tls;
	uint64_t peer;
	hx_os_deadline deadline;
} tls_session;

/**
 * \brief This example's wire format is uint32 big-endian length + UTF-8 JSON.
 *
 * \details One send call owns the whole frame. A partial frame on failure ends
 * this demo session; never resend its prefix or parse an incomplete document.
 */
static inline int tls_json_send(
	tls_session *session, const json_field *fields, size_t count)
{
	hx_string text = HX_STRING_INIT;
	unsigned char frame[4 + JSON_MESSAGE_CAPACITY];
	size_t bytes, accepted = 0;
	hx_os_error error = {0};
	hx_os_status status;

	if (!json_encode(fields, count, &text))
		return 0;
	bytes = hx_string_size_bytes(&text);
	if (bytes >= JSON_MESSAGE_CAPACITY) {
		hx_string_destroy(&text);

		return net_demo_check(HX_OS_LIMIT, NULL, "JSON length");
	}
	frame[0] = (unsigned char)(bytes >> 24);
	frame[1] = (unsigned char)(bytes >> 16);
	frame[2] = (unsigned char)(bytes >> 8);
	frame[3] = (unsigned char)bytes;
	memcpy(frame + 4, hx_string_c_str(&text), bytes);
	hx_string_destroy(&text);
	status = hx_os_tls_send_until(session->tls, session->peer, frame,
		bytes + 4, session->deadline, &accepted, &error);
	if (status != HX_OS_OK)
		fprintf(stderr, "TLS accepted %zu of %zu frame bytes\n",
			accepted, bytes + 4);

	return net_demo_check(status, &error, "TLS send");
}

static inline hx_os_config_document *tls_json_receive(tls_session *session)
{
	unsigned char header[4];
	char text[JSON_MESSAGE_CAPACITY];
	size_t received = 0;
	uint32_t bytes;
	hx_os_error error = {0};
	hx_os_status status = hx_os_tls_receive_exact_until(session->tls,
		session->peer, header, sizeof(header), session->deadline,
		&received, &error);

	if (!net_demo_check(status, &error, "TLS JSON header"))
		return NULL;
	bytes = ((uint32_t)header[0] << 24) | ((uint32_t)header[1] << 16) |
		((uint32_t)header[2] << 8) | header[3];
	if (!bytes || bytes >= sizeof(text)) {
		(void)net_demo_check(HX_OS_LIMIT, NULL, "TLS JSON length");

		return NULL;
	}
	status = hx_os_tls_receive_exact_until(session->tls, session->peer,
		text, bytes, session->deadline, &received, &error);
	if (!net_demo_check(status, &error, "TLS JSON body"))
		return NULL;
	text[bytes] = 0;
	printf("[TLS receive] %s\n", text);

	return json_decode(text, bytes);
}

static inline int tls_json_close(hx_os_tls **owner, int success)
{
	hx_os_error error = {0};
	hx_os_status status = success
		? hx_os_tls_close_until(
			  owner, hx_os_deadline_after_ms(2000), &error)
		: hx_os_tls_abort_until(
			  owner, hx_os_deadline_after_ms(2000), &error);
	int ok = net_demo_check(status, &error, "TLS close");

	if (*owner) {
		// This finite CLI demo explicitly abandons unsent data on
		// failure. A long-lived application can retain its owner and
		// retry close.
		(void)hx_os_tls_abort_until(
			owner, hx_os_deadline_after_ms(2000), NULL);
		if (*owner)
			exit(EXIT_FAILURE);
	}

	return ok;
}
#endif
