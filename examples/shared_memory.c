#include <hx_os.h>
#include <stdio.h>
#include <string.h>

typedef struct shared_payload {
	hx_os_atomic32 ready;
	char json[124];
} shared_payload;

static hx_os_status close_memory(hx_os_shared_memory **memory)
{
	hx_os_status status = hx_os_shared_memory_close(memory, NULL);

	if (*memory)
		status = hx_os_shared_memory_close(memory, NULL);

	if (*memory)
		fputs("Unmap pending; a long-lived host must keep the owner "
		      "and retry close.\n", stderr);

	return status;
}

static int read_message(const char *name, const char *directory)
{
	hx_os_shared_memory *memory = NULL;
	shared_payload *payload;
	hx_os_deadline deadline = hx_os_deadline_after_ms(2000);
	hx_os_status status = directory
		? hx_os_shared_memory_open_at(
			  NULL, directory, name, &memory, NULL)
		: hx_os_shared_memory_open(NULL, name, &memory, NULL);

	if (status != HX_OS_OK ||
		hx_os_shared_memory_size(memory) != sizeof(*payload)) {
		(void)close_memory(&memory);
		return 1;
	}
	payload = hx_os_shared_memory_data(memory);
	while (!hx_os_atomic32_load(&payload->ready, HX_OS_ACQUIRE)) {
		if (hx_os_deadline_expired(deadline)) {
			(void)close_memory(&memory);
			return 1;
		}
		(void)hx_os_sleep_until(hx_os_deadline_after_ms(1), NULL);
	}
	printf("Child received: %.*s\n", (int)sizeof(payload->json),
		payload->json);
	hx_os_atomic32_store(&payload->ready, 2, HX_OS_RELEASE);
	return close_memory(&memory) == HX_OS_OK ? 0 : 1;
}

static int write_message(const char *name, const char *directory)
{
	hx_os_shared_memory *memory = NULL;
	shared_payload *payload;
	hx_os_deadline deadline = hx_os_deadline_after_ms(10000);
	hx_os_status status = directory
		? hx_os_shared_memory_create_at(NULL, directory, name,
			  sizeof(*payload), &memory, NULL)
		: hx_os_shared_memory_create(
			  NULL, name, sizeof(*payload), &memory, NULL);

	if (status != HX_OS_OK) {
		(void)close_memory(&memory);

		return 1;
	}
	payload = hx_os_shared_memory_data(memory);
	(void)snprintf(payload->json, sizeof(payload->json),
		"{\"device\":\"door-1\",\"request\":42}");
	hx_os_atomic32_store(&payload->ready, 1, HX_OS_RELEASE);
	printf("Ready: %s\n", name);
	fflush(stdout);
	while (hx_os_atomic32_load(&payload->ready, HX_OS_ACQUIRE) != 2) {
		if (hx_os_deadline_expired(deadline)) {
			status = HX_OS_TIMEOUT;
			break;
		}
		(void)hx_os_sleep_until(hx_os_deadline_after_ms(1), NULL);
	}
	hx_os_status closed = close_memory(&memory);
	if (closed != HX_OS_OK)
		return 1;

	for (;;) {
		hx_os_status removed = directory
			? hx_os_shared_memory_unlink_at(directory, name, NULL)
			: hx_os_shared_memory_unlink(name, NULL);

		if (removed == HX_OS_OK || removed == HX_OS_NOT_SUPPORTED ||
			removed == HX_OS_NOT_FOUND)
			break;
		if (hx_os_deadline_expired(deadline)) {
			status = removed;
			break;
		}
		(void)hx_os_sleep_until(hx_os_deadline_after_ms(5), NULL);
	}
	return status == HX_OS_OK ? 0 : 1;
}

int main(int argc, char **argv)
{
	hx_os_shared_memory *memory = NULL;
	hx_os_process *child = NULL;
	hx_os_process_result exited;
	hx_string name = HX_STRING_INIT, executable = HX_STRING_INIT;
	const char *directory = argc == 2 ? argv[1] : NULL;
	shared_payload *payload;
	hx_os_status status;
	int created = 0;

	if (argc >= 3 && !strcmp(argv[1], "--write"))
		return write_message(argv[2], argc == 4 ? argv[3] : NULL);
	if (argc >= 3 && !strcmp(argv[1], "--read"))
		return read_message(argv[2], argc == 4 ? argv[3] : NULL);
	if (argc > 2) {
		fprintf(stderr,
			"Usage: hx_os_shared_memory [mapping-directory]\n");
		return 2;
	}
	status = hx_os_uuid_generate_string(&name, NULL);
	if (status == HX_OS_OK)
		status = directory ? hx_os_shared_memory_create_at(NULL,
					     directory, hx_string_c_str(&name),
					     sizeof(*payload), &memory, NULL)
				   : hx_os_shared_memory_create(NULL,
					     hx_string_c_str(&name),
					     sizeof(*payload), &memory, NULL);
	if (status == HX_OS_OK) {
		const char *arguments[] = {
			"--read", hx_string_c_str(&name), directory, NULL};

		created = 1;
		payload = hx_os_shared_memory_data(memory);
		(void)snprintf(payload->json, sizeof(payload->json),
			"{\"device\":\"door-1\",\"request\":42}");
		hx_os_atomic32_store(&payload->ready, 1, HX_OS_RELEASE);
		status = hx_os_path_executable_string(&executable, NULL);
		if (status == HX_OS_OK)
			status = hx_os_process_spawn(NULL,
				hx_string_c_str(&executable), arguments, NULL,
				NULL, &child, NULL);
	}
	if (status == HX_OS_OK) {
		status = hx_os_process_wait_until(
			child, hx_os_deadline_after_ms(5000), &exited, NULL);
		if (status == HX_OS_OK &&
			(exited.exit_code || exited.signal_number))
			status = HX_OS_IO_ERROR;
	}
	if (child)
		(void)hx_os_process_close_until(
			&child, hx_os_deadline_after_ms(5000), NULL);
	hx_os_status closed = close_memory(&memory);
	if (status == HX_OS_OK)
		status = closed;

	if (created && !memory) {
		hx_os_status removed = directory
			? hx_os_shared_memory_unlink_at(
				  directory, hx_string_c_str(&name), NULL)
			: hx_os_shared_memory_unlink(
				  hx_string_c_str(&name), NULL);

		if (removed != HX_OS_OK && removed != HX_OS_NOT_SUPPORTED &&
			status == HX_OS_OK)
			status = removed;
	}
	if (status != HX_OS_OK)
		fprintf(stderr, "Shared memory: %s\n",
			hx_os_status_name(status));
	hx_string_destroy(&name);
	hx_string_destroy(&executable);
	return status == HX_OS_OK ? 0 : 1;
}
