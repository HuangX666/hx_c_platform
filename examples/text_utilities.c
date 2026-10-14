#include <hx_text_utils.h>
#include <inttypes.h>
#include <stdio.h>

int main(void)
{
	hx_os_text_split split;
	hx_string_view part;
	hx_os_status status;
	uint64_t port = 0;
	int64_t offset = 0;

	status = hx_os_text_split_init(&split,
		hx_string_view_from_cstr("alpha::beta::::gamma::"),
		hx_string_view_from_cstr("::"), 1, NULL);
	if (status != HX_OS_OK)
		goto failed;

	while ((status = hx_os_text_split_next(&split, &part, NULL)) ==
		HX_OS_OK) {
		printf("Part (%zu bytes): ", part.size);
		if (part.size)
			(void)fwrite(part.data, 1, part.size, stdout);
		putchar('\n');
	}
	if (status != HX_OS_EOF)
		goto failed;

	status = hx_os_text_parse_u64(
		hx_string_view_from_cstr("443"), 10, &port, NULL);
	if (status == HX_OS_OK)
		status = hx_os_text_parse_i64(
			hx_string_view_from_cstr("-42"), 10, &offset, NULL);
	if (status != HX_OS_OK)
		goto failed;

	printf("Port: %" PRIu64 ", offset: %" PRId64 "\n", port, offset);
	printf("file2 sorts before file10: %s\n",
		hx_os_text_compare_natural(hx_string_view_from_cstr("file2"),
			hx_string_view_from_cstr("file10")) < 0
			? "yes"
			: "no");

	return 0;

failed:
	fprintf(stderr, "Text operation failed: %s\n",
		hx_os_status_name(status));

	return 1;
}
