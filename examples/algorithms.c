#include <hx_algorithm.h>
#include <stdio.h>

typedef struct device {
	int id;
	int pending;
} device;

static int compare_device(const void *left, const void *right, void *user)
{
	const device *a = left;
	const device *b = right;

	(void)user;

	return (a->id > b->id) - (a->id < b->id);
}

static int device_pending(const void *value, void *user)
{
	const device *item = value;

	(void)user;

	return item->pending != 0;
}

static hx_os_status sum_pending(
	void *accumulator, const void *value, void *user, hx_os_error *error)
{
	uint64_t *total = accumulator;
	const device *item = value;

	(void)user;
	(void)error;
	if (item->pending < 0 || (uint64_t)item->pending > UINT64_MAX - *total)
		return HX_OS_RANGE;

	*total += (uint64_t)item->pending;

	return HX_OS_OK;
}

int main(void)
{
	device devices[] = {{31, 1}, {17, 0}, {23, 1}};
	device work[3];
	device key = {23, 0};
	int32_t ids[] = {31, 17, 23};
	const size_t count = sizeof(devices) / sizeof(devices[0]);
	size_t index = 0, pending = 0, i;
	size_t completed = 0;
	uint64_t total = 0;
	hx_os_array_progress progress;
	hx_os_status status;
	int found = 0;

	// Ordinary integer arrays need no comparator callback.
	status = hx_os_sort_i32(ids, count, NULL);
	if (status == HX_OS_OK)
		status = hx_os_binary_search_i32(ids, count, 23, &found, NULL);
	if (status != HX_OS_OK || !found)
		return 1;

	status = hx_os_array_sort(
		devices, count, sizeof(devices[0]), compare_device, NULL, NULL);
	if (status == HX_OS_OK)
		status = hx_os_array_lower_bound(devices, count,
			sizeof(devices[0]), &key, compare_device, NULL, &index,
			NULL);
	if (status == HX_OS_OK)
		status =
			hx_os_array_count_if(devices, count, sizeof(devices[0]),
				device_pending, NULL, &pending, NULL);
	if (status != HX_OS_OK) {
		fprintf(stderr, "Array operation failed: %s\n",
			hx_os_status_name(status));

		return 1;
	}

	for (i = 0; i < count; ++i)
		printf("Device %d, pending=%d\n", devices[i].id,
			devices[i].pending);
	if (index < count && devices[index].id == key.id)
		printf("Found device %d\n", devices[index].id);
	else
		puts("Device not found");

	printf("Devices needing work: %zu\n", pending);

	// Build a separate work list, then collect its pending count.
	status = hx_os_array_copy_if(devices, count, sizeof(devices[0]),
		device_pending, NULL, work, 3, &progress, NULL);
	if (status == HX_OS_OK)
		status = hx_os_array_fold(work, progress.written,
			sizeof(work[0]), &total, sizeof(total), sum_pending,
			NULL, &completed, NULL);
	if (status != HX_OS_OK)
		return 1;

	printf("Copied work items: %zu\n", progress.written);
	printf("Pending work total: %llu\n", (unsigned long long)total);

	return 0;
}
