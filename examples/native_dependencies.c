#include <hx_os.h>
#include <sqlite3.h>
#include <openssl/evp.h>
#include <curl/curl.h>
#include <utf8proc.h>
#if defined(HX_OS_DEMO_ARCHIVE)
#include <archive.h>
#include <zlib.h>
#include <lzma.h>
#include <zstd.h>
#endif
#include <stdio.h>

int main(void)
{
	sqlite3 *database = NULL;
	CURLU *url = NULL;
	char *host = NULL;
	unsigned char digest[32];
	size_t bytes = 0;
	int ok = sqlite3_open(":memory:", &database) == SQLITE_OK;

	if (ok)
		ok = sqlite3_exec(database, "CREATE TABLE sample(id INTEGER)",
			     NULL, NULL, NULL) == SQLITE_OK;
	if (ok)
		ok = EVP_Q_digest(NULL, "SHA256", NULL, "abc", 3, digest,
			     &bytes) == 1 &&
			bytes == 32;
	if (ok)
		ok = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
	if (ok) {
		url = curl_url();
		ok = url &&
			curl_url_set(url, CURLUPART_URL,
				"https://example.invalid/records",
				0) == CURLUE_OK &&
			curl_url_get(url, CURLUPART_HOST, &host, 0) ==
				CURLUE_OK;
		if (ok)
			printf("Native URL host: %s\n", host);
		curl_free(host);
		curl_url_cleanup(url);
		curl_global_cleanup();
	}
	if (ok)
		printf("SQLite %s; libcurl %s; utf8proc %s; SHA256: 32 bytes\n",
			sqlite3_libversion(),
			curl_version_info(CURLVERSION_NOW)->version,
			utf8proc_version());

#if defined(HX_OS_DEMO_ARCHIVE)
	if (ok) {
		struct archive *reader = archive_read_new();

		ok = reader && archive_read_free(reader) == ARCHIVE_OK;
		if (ok)
			printf("libarchive %s; zlib %s; liblzma %s; zstd %s\n",
				archive_version_string(), zlibVersion(),
				lzma_version_string(), ZSTD_versionString());
	}
#endif

	if (database)
		(void)sqlite3_close(database);
	return ok ? 0 : 1;
}
