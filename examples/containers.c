#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_os_container_options options;
	hx_os_queue *queue = NULL;
	int value, result = 1;
	if (!(hx_os_capabilities() & HX_OS_CAP_CONTAINERS)) {
		puts("Containers are disabled.");
		return 0;
	}
	hx_os_container_options_init(&options, sizeof(int));
	options.max_items = 32;
	if (hx_os_queue_create(NULL, &options, &queue, NULL) != HX_OS_OK)
		return 1;
	value = 42;
	if (hx_os_queue_push(queue, &value, NULL) == HX_OS_OK &&
		hx_os_queue_pop(queue, &value, NULL) == HX_OS_OK) {
		printf("Queue result: %d\n", value);
		result = value == 42 ? 0 : 1;
	}
	(void)hx_os_sequence_destroy(&queue, NULL);
	return result;
}
