#include <hx_format.h>

int main(void)
{
	hx_format_arg args[] = {hx_fmt_str("张三"), hx_fmt_f64(3.75)};
	if (!(hx_os_capabilities() & HX_OS_CAP_FORMAT))
		return 0;
	return hx_print_args(HX_PRINT_STDOUT, 1,
		       hx_string_view_from_cstr("{}: {:.2f}"), args, 2,
		       NULL) == HX_OS_OK
		? 0
		: 1;
}
