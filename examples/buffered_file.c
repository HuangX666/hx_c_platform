#include <hx_os.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	const char *records[] = {"{\"device\":1,\"temperature\":23.5}\n",
		"{\"device\":2,\"temperature\":24.0}\n",
		"{\"device\":3,\"temperature\":22.8}\n"};
	hx_os_file_writer *writer = NULL;
	hx_os_status status =
		hx_os_file_writer_open("samples.jsonl", &writer, NULL);
	size_t i;

	for (i = 0; status == HX_OS_OK && i < 3; ++i) {
		size_t accepted = 0;

		status = hx_os_file_writer_write_until(writer, records[i],
			strlen(records[i]), &accepted,
			hx_os_deadline_after_ms(2000), NULL);
		if (status != HX_OS_OK)
			fprintf(stderr, "Record %u: accepted %zu bytes; %s\n",
				(unsigned)(i + 1), accepted,
				hx_os_status_name(status));
	}
	if (status == HX_OS_OK)
		status = hx_os_file_writer_flush_until(
			writer, 1, hx_os_deadline_after_ms(2000), NULL);
	if (status == HX_OS_OK)
		status = hx_os_file_writer_close_until(
			&writer, hx_os_deadline_after_ms(2000), NULL);

	if (status != HX_OS_OK) {
		fprintf(stderr, "Save failed: %s\n", hx_os_status_name(status));
		/* This console example explicitly waits for safe abandonment.
		 * A GUI host keeps writer and retries close/abort on its own
		 * schedule.
		 */
		hx_os_file_writer_abort_until(
			&writer, HX_OS_DEADLINE_INFINITE, NULL);
		return 1;
	}

	puts("Saved 3 device readings to samples.jsonl");
	return 0;
}
