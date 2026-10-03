#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_os_container_options options;
	hx_os_map *map = NULL;
	hx_os_map_iterator iterator;
	hx_os_status status;
	int values[] = {20, 10, 21};
	const char keys[] = {'b', 'a', 'b'};
	size_t i;
	if (!(hx_os_capabilities() & HX_OS_CAP_CONTAINERS))
		return 0;
	hx_os_container_options_init(&options, sizeof(int));
	status = hx_os_map_create(NULL, HX_OS_MAP_RED_BLACK | HX_OS_MAP_MULTI,
		&options, NULL, &map, NULL);
	if (status != HX_OS_OK)
		return 1;
	for (i = 0; i < 3; ++i) {
		status =
			hx_os_map_insert(map, &keys[i], 1, &values[i], 0, NULL);
		if (status != HX_OS_OK)
			goto done;
	}
	HX_OS_MAP_FOR_EACH(iterator, map, status)
	{
		const void *key, *value;
		size_t bytes;
		status = hx_os_map_entry(&iterator, &key, &bytes, &value);
		if (status != HX_OS_OK)
			goto done;
		printf("%c: %d\n", *(const char *)key, *(const int *)value);
	}
done:
	(void)hx_os_map_destroy(&map, NULL);
	return status == HX_OS_EOF ? 0 : 1;
}
