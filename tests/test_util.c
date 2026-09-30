#if defined(__linux__)
#define _GNU_SOURCE
#endif
#include <hx_os.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

static void *random_worker(void *user)
{
	unsigned char uuid[16];
	int i;
	(void)user;
	for (i = 0; i < 100; ++i) {
		CHECK(hx_os_uuid_generate(uuid, NULL) == HX_OS_OK);
		CHECK((uuid[6] & 0xf0) == 0x40 && (uuid[8] & 0xc0) == 0x80);
	}
	return NULL;
}

#if defined(__linux__) && defined(SYS_getrandom)
static int test_linux_fault(int code)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_getrandom, 0, 1),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (unsigned)code),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)};
	struct sock_fprog program = {
		(unsigned short)(sizeof(filter) / sizeof(filter[0])), filter};
	unsigned char buffer[32], uuid[16];
	hx_os_error error;
	size_t i;
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
		prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) != 0) {
		puts("SKIP: test-process seccomp unavailable");
		return 77;
	}
	memset(buffer, 0xa5, sizeof(buffer));
	memset(uuid, 0xa5, sizeof(uuid));
	if (code == ENOSYS) {
		CHECK(hx_os_random_bytes(buffer, sizeof(buffer), &error) ==
			HX_OS_OK);
		CHECK(hx_os_uuid_generate(uuid, &error) == HX_OS_OK);
		CHECK((uuid[6] & 0xf0) == 0x40 && (uuid[8] & 0xc0) == 0x80);
		puts("native random-device fallback passed");
	}
	else {
		CHECK(hx_os_random_bytes(buffer, sizeof(buffer), &error) ==
			HX_OS_IO_ERROR);
		CHECK(error.domain == HX_OS_ERROR_ERRNO &&
			error.native_code == code);
		for (i = 0; i < sizeof(buffer); ++i)
			CHECK(buffer[i] == 0);
		CHECK(hx_os_uuid_generate(uuid, &error) == HX_OS_IO_ERROR);
		for (i = 0; i < sizeof(uuid); ++i)
			CHECK(uuid[i] == 0xa5);
		puts("random failure clearing and UUID preservation passed");
	}
	return 0;
}
#endif

int main(int argc, char **argv)
{
	static const unsigned char uuid[16] = {0x00, 0x11, 0x22, 0x33, 0x44,
		0x55, 0x46, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,
		0xff};
	unsigned char output[18], erase[67];
	char text[37], overlap[37];
	hx_os_thread *threads[4] = {NULL, NULL, NULL, NULL};
	unsigned char *large;
	size_t i;
	uint32_t crc;
	hx_string generated = HX_STRING_INIT;
	if (argc > 1) {
#if defined(__linux__) && defined(SYS_getrandom)
		return test_linux_fault(
			!strcmp(argv[1], "fallback") ? ENOSYS : EIO);
#else
		(void)argv;
		return 77;
#endif
	}
	CHECK(hx_os_crc32(0, "123456789", 9) == UINT32_C(0xcbf43926));
	CHECK(hx_os_crc32(0, NULL, 0) == 0);
	crc = hx_os_crc32(0, "1234", 4);
	CHECK(hx_os_crc32(crc, "56789", 5) == UINT32_C(0xcbf43926));
	CHECK(hx_os_crc32(crc, NULL, 0) == crc);
	memset(erase, 0xa5, sizeof(erase));
	hx_os_secure_zero(erase + 1, sizeof(erase) - 2);
	CHECK(erase[0] == 0xa5 && erase[66] == 0xa5);
	for (i = 1; i < 66; ++i)
		CHECK(!erase[i]);
	hx_os_secure_zero(NULL, 0);
	CHECK(hx_os_uuid_format(uuid, text) == HX_OS_OK);
	CHECK(!strcmp(text, "00112233-4455-4677-8899-aabbccddeeff"));
	memcpy(overlap, uuid, sizeof(uuid));
	CHECK(hx_os_uuid_format((unsigned char *)overlap, overlap) == HX_OS_OK);
	CHECK(!strcmp(text, overlap));
	CHECK(hx_os_uuid_format(NULL, text) == HX_OS_INVALID);
	memset(output, 0xa5, sizeof(output));
	if (!(hx_os_capabilities() & HX_OS_CAP_RANDOM)) {
		CHECK(hx_string_assign_cstr(&generated, "kept", NULL) ==
			HX_OS_OK);
		CHECK(hx_os_uuid_generate_string(&generated, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(!strcmp(hx_string_c_str(&generated), "kept"));
		hx_string_destroy(&generated);
		CHECK(hx_os_random_bytes(output, sizeof(output), NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_uuid_generate(output, NULL) == HX_OS_NOT_SUPPORTED);
		for (i = 0; i < sizeof(output); ++i)
			CHECK(output[i] == 0xa5);
		return 0;
	}
	CHECK(hx_os_random_bytes(NULL, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_uuid_generate_string(&generated, NULL) == HX_OS_OK);
	CHECK(hx_string_size_bytes(&generated) == 36);
	hx_string_destroy(&generated);
	CHECK(hx_os_random_bytes(NULL, 1, NULL) == HX_OS_INVALID);
	CHECK(hx_os_uuid_generate(NULL, NULL) == HX_OS_INVALID);
	CHECK(hx_os_uuid_generate(output + 1, NULL) == HX_OS_OK);
	CHECK(output[0] == 0xa5 && output[17] == 0xa5);
	CHECK((output[7] & 0xf0) == 0x40 && (output[9] & 0xc0) == 0x80);
	large = malloc(1024 * 1024);
	CHECK(large &&
		hx_os_random_bytes(large, 1024 * 1024, NULL) == HX_OS_OK);
	hx_os_secure_zero(large, 1024 * 1024);
	free(large);
	for (i = 0; i < 4; ++i)
		CHECK(hx_os_thread_create(NULL, random_worker, NULL,
			      &threads[i], NULL) == HX_OS_OK);
	for (i = 0; i < 4; ++i) {
		CHECK(hx_os_thread_join_until(threads[i],
			      hx_os_deadline_after_ms(2000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&threads[i], NULL) == HX_OS_OK);
	}
	puts("random, UUID, CRC32 and secure clearing contracts passed");
	return 0;
}
