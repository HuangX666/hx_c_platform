#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <hx_os.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#if defined(__APPLE__)
#include <mach/mach_error.h>
#endif
#endif

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

static void check_message(hx_os_error error, int expect_text)
{
	char text[4096], prefix[256], limited[5];
	size_t required, measured, invalid;

	(void)snprintf(prefix, sizeof(prefix),
		"%s [domain=%u code=%" PRId64 "]",
		hx_os_status_name(error.status), error.domain,
		error.native_code);
	CHECK(hx_os_error_message(&error, NULL, 0, &required) ==
		HX_OS_OUTPUT_FULL);
	CHECK(required <= sizeof(text));
	CHECK(hx_os_error_message(&error, text, sizeof(text), &measured) ==
			HX_OS_OK &&
		measured == required);
	CHECK(strlen(text) + 1 == required);
	CHECK(!strncmp(prefix, text, strlen(prefix)));
	CHECK(expect_text ? text[strlen(prefix)] == ':'
			  : !strcmp(prefix, text));
	CHECK(hx_os_text_validate_utf8(text, strlen(text), &invalid) ==
		HX_OS_OK);

	memset(limited, 0xa5, sizeof(limited));
	CHECK(hx_os_error_message(&error, limited, 4, &measured) ==
			HX_OS_OUTPUT_FULL &&
		measured == required);
	CHECK(!limited[3] && (unsigned char)limited[4] == 0xa5);
}

static void *check_worker(void *user)
{
	hx_os_error error = {HX_OS_NOT_FOUND, HX_OS_ERROR_GAI, EAI_NONAME, 0};
	int i;

	(void)user;

	for (i = 0; i < 500; ++i) {
		check_message(error, 1);
		error.native_code = error.native_code == EAI_NONAME
			? EAI_AGAIN
			: EAI_NONAME;
	}

	return NULL;
}

int main(void)
{
	hx_os_error error = {HX_OS_IO_ERROR, HX_OS_ERROR_NONE, 123, 0};
	hx_os_thread *threads[4] = {NULL, NULL, NULL, NULL};
	size_t i;

	check_message(error, 0);
	error.domain = HX_OS_ERROR_ERRNO;
	error.native_code = EINVAL;
	check_message(error, 1);
	error.domain = HX_OS_ERROR_GAI;
	error.native_code = EAI_NONAME;
	check_message(error, 1);
	error.native_code = INT64_MAX;
	check_message(error, 0);
	error.domain = HX_OS_ERROR_MACH;
#if defined(__APPLE__)
	error.native_code = KERN_INVALID_ADDRESS;
	check_message(error, 1);
#else
	error.native_code = 1;
	check_message(error, 0);
#endif
	error.domain = UINT32_MAX;
	check_message(error, 0);

	for (i = 0; i < 4; ++i)
		CHECK(hx_os_thread_create(NULL, check_worker, NULL, &threads[i],
			      NULL) == HX_OS_OK);

	for (i = 0; i < 4; ++i) {
		CHECK(hx_os_thread_join_until(threads[i],
			      hx_os_deadline_after_ms(2000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&threads[i], NULL) == HX_OS_OK);
	}

	puts("native error text contracts passed");

	return 0;
}
