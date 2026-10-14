#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_os_crypto_digest_state *digest = NULL;
	hx_string result = HX_STRING_INIT;
	hx_os_bytes_view first = {"ab", 2}, second = {"c", 1};
	hx_os_status status = hx_os_crypto_digest_create(
		NULL, HX_OS_CRYPTO_SHA256, &digest, NULL);

	if (status == HX_OS_OK)
		status = hx_os_crypto_digest_update(digest, first, NULL);
	if (status == HX_OS_OK)
		status = hx_os_crypto_digest_update(digest, second, NULL);
	if (status == HX_OS_OK)
		status = hx_os_crypto_digest_finish(digest, &result, NULL);
	if (status == HX_OS_OK)
		puts(hx_string_c_str(&result));
	else
		fprintf(stderr, "Digest failed: %s\n",
			hx_os_status_name(status));

	(void)hx_os_crypto_digest_destroy(&digest, NULL);
	hx_string_destroy(&result);
	return status == HX_OS_OK ? 0 : 1;
}
