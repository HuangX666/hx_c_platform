#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_string encrypted = HX_STRING_INIT, decoded = HX_STRING_INIT;
	hx_string_view message = {"Hello, UTF-8!", 13};
	hx_os_error error;

	// Demonstration only: obtain a real password from your application's
	// secret input/storage, rather than embedding it in source.
	hx_os_status status = hx_os_crypto_encrypt_text(
		"example password", message, &encrypted, &error);
	if (status == HX_OS_OK)
		status = hx_os_crypto_decrypt_text("example password",
			hx_string_view_of(&encrypted), &decoded, &error);
	if (status == HX_OS_OK) {
		printf("Encrypted: %s\n", hx_string_c_str(&encrypted));
		printf("Decrypted: %s\n", hx_string_c_str(&decoded));
	}
	else
		fprintf(stderr, "Encryption example failed: %s\n",
			hx_os_status_name(status));

	hx_string_destroy(&decoded);
	hx_string_destroy(&encrypted);
	hx_os_crypto_thread_cleanup();

	return status == HX_OS_OK ? 0 : 1;
}
