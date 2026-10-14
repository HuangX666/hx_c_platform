#include <hx_bitset.h>
#include <stdio.h>

int main(void)
{
	hx_os_bitset *completed = NULL;
	hx_os_status status;
	size_t next = 0;

	/* Track 100 numbered jobs, permitting growth to 1,000 jobs. */
	status = hx_os_bitset_create(NULL, 100, 1000, &completed, NULL);
	if (status == HX_OS_OK)
		status = hx_os_bitset_set_range(completed, 0, 10, 1, NULL);
	if (status == HX_OS_OK)
		status = hx_os_bitset_set(completed, 42, 1, NULL);

	if (status == HX_OS_OK)
		status = hx_os_bitset_find_next(completed, 0, 0, &next, NULL);
	if (status == HX_OS_OK) {
		printf("Completed: %zu of %zu\n", hx_os_bitset_count(completed),
			hx_os_bitset_size(completed));
		printf("Next unfinished job: %zu\n", next);
	}
	else
		fprintf(stderr, "Bitset failed: %s\n",
			hx_os_status_name(status));

	(void)hx_os_bitset_destroy(&completed, NULL);

	return status == HX_OS_OK ? 0 : 1;
}
