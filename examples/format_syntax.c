#include <hx_format_syntax.h>

typedef struct user_info {
	const char *name;
	int age;
} user_info;

int main(void)
{
	user_info user = {"张三", 18};
	double price = 1.25;
	int count = 3;

	if (!(hx_os_capabilities() & HX_OS_CAP_FORMAT))
		return 0;

	return HX_FPRINTLN("{user.name}: {user.age}, "
			   "total={price * count:.2f}") == HX_OS_OK
		? 0
		: 1;
}
