#include <hx/Crypto.hpp>
#include "cpp_check.hpp"

int main()
{
	auto hash = hx::Crypto::sha256("abc");
	if (!hx_os_crypto_available()) {
		CHECK(hash.status() == HX_OS_NOT_SUPPORTED);

		return 0;
	}
	CHECK(hash && hash.value().size() == 64);
	CHECK(hash.value().startsWith("ba7816bf"));
	auto mac = hx::Crypto::hmac({"secret", 6}, {"message", 7});
	CHECK(mac &&
		hx::Crypto::verifyHmac(
			{"secret", 6}, {"message", 7}, mac.value()));
	auto encrypted = hx::Crypto::encryptText("example password", "Hello");
	CHECK(encrypted);
	auto plaintext =
		hx::Crypto::decryptText("example password", encrypted.value());
	CHECK(plaintext && plaintext.value() == "Hello");
	CHECK(hx::Crypto::decryptText("bad password", encrypted.value())
			.status() == HX_OS_AUTH_FAILED);

	auto engine = hx::Crypto::create();
	CHECK(engine);
	auto key = hx::CryptoKey::generate(
		HX_OS_CRYPTO_ED25519, engine.value().native());
	CHECK(key);
	auto publicPem = key.value().exportPublic();
	CHECK(publicPem);
	auto publicKey = hx::CryptoKey::importPublic(publicPem.value());
	CHECK(publicKey);
	auto signature = key.value().sign({"test", 4});
	CHECK(signature);
	CHECK(publicKey.value().verify({"test", 4}, signature.value().view()));

	return 0;
}
