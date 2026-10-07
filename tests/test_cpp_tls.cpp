#include <hx/Tls.hpp>
#include "cpp_check.hpp"

int main(int argc, char **argv)
{
	hx_os_tls *serverOwner = NULL, *clientOwner = NULL;
	hx::Tls server(serverOwner), client(clientOwner);
	hx_os_tls_options options = HX_OS_TLS_OPTIONS_INIT;

	if (!hx_os_tls_available()) {
		CHECK(client.connect("127.0.0.1", 443).status() ==
			HX_OS_NOT_SUPPORTED);
		CHECK(!client.get());

		return 0;
	}
	if (argc != 4) {
		options.ca_file = "hx-missing-tls-ca.pem";
		CHECK(!client.connect("127.0.0.1", 443, &options));
		CHECK(client.get());
		(void)client.abortUntil(hx_os_deadline_after_ms(5000));
		CHECK(!client.get());

		return 0;
	}
	options.certificate_file = argv[1];
	options.private_key_file = argv[2];
	CHECK(server.listen("127.0.0.1", 0, options));
	auto address = server.localAddress();
	CHECK(address);
	options = hx_os_tls_options HX_OS_TLS_OPTIONS_INIT;
	options.ca_file = argv[3];
	CHECK(client.connect("127.0.0.1", address.value().port, &options));
	auto peer = server.acceptUntil(hx_os_deadline_after_ms(5000));
	CHECK(peer);
	CHECK(client.send("hello", 5));
	char buffer[5];
	auto read = server.receiveExact(buffer, sizeof(buffer), peer.value());
	CHECK(read && read.value() == 5 && !memcmp(buffer, "hello", 5));
	CHECK(server.send(buffer, sizeof(buffer), peer.value()));
	CHECK(client.receiveExact(buffer, sizeof(buffer)));
	CHECK(client.peerInfo().value().flags & HX_OS_TLS_PEER_VERIFIED);
	CHECK(client.closeUntil(hx_os_deadline_after_ms(5000)));
	CHECK(server.closeUntil(hx_os_deadline_after_ms(5000)));
	CHECK(!serverOwner && !clientOwner);

	return 0;
}
