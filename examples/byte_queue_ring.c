#include <hx_container.h>
#include <hx_ring.h>
#include <stdio.h>
#include <string.h>

static hx_os_status example_queue(void)
{
	hx_os_byte_queue *queue = NULL;
	hx_os_status status;
	char output[16] = {0};
	size_t completed = 0;

	status = hx_os_byte_queue_create(NULL, 16, &queue, NULL);
	if (status != HX_OS_OK)
		return status;

	status = hx_os_byte_queue_reserve(queue, 16, NULL);
	if (status == HX_OS_OK)
		status = hx_os_byte_queue_write(queue, "hello", 5, NULL);
	if (status == HX_OS_OK)
		status = hx_os_byte_queue_read_some(
			queue, output, sizeof(output) - 1, &completed, NULL);
	if (status == HX_OS_OK)
		printf("ByteQueue: %zu bytes: %s\n", completed, output);

	(void)hx_os_byte_queue_destroy(&queue, NULL);

	return status;
}

static hx_os_status example_ring(void)
{
	unsigned char storage[8];
	hx_os_ring ring;
	hx_os_ring_write_span writable[2];
	hx_os_ring_read_span readable[2];
	hx_os_status status;
	size_t i, total;

	status = hx_os_ring_init(&ring, storage, sizeof(storage), NULL);
	if (status != HX_OS_OK)
		return status;

	status = hx_os_ring_write(&ring, "abcdef", 6, NULL);
	if (status == HX_OS_OK)
		status = hx_os_ring_read(&ring, NULL, 4, NULL);
	if (status == HX_OS_OK)
		status = hx_os_ring_write_spans(&ring, writable, NULL);
	if (status != HX_OS_OK)
		return status;

	/* Fill both free segments directly; data is invisible until commit. */
	for (i = 0; i < 2; ++i)
		if (writable[i].size)
			memset(writable[i].data, 'x', writable[i].size);

	total = writable[0].size + writable[1].size;
	status = hx_os_ring_commit(&ring, total, NULL);
	if (status == HX_OS_OK)
		status = hx_os_ring_read_spans(&ring, readable, NULL);
	if (status != HX_OS_OK)
		return status;

	printf("Ring: ");
	for (i = 0; i < 2; ++i)
		if (readable[i].size)
			(void)fwrite(readable[i].data, 1, readable[i].size,
				stdout);
	putchar('\n');

	total = readable[0].size + readable[1].size;

	return hx_os_ring_consume(&ring, total, NULL);
}

int main(void)
{
	hx_os_status status = example_queue();

	if (status == HX_OS_OK)
		status = example_ring();
	if (status != HX_OS_OK)
		fprintf(stderr, "Buffer failed: %s\n",
			hx_os_status_name(status));

	return status == HX_OS_OK ? 0 : 1;
}
