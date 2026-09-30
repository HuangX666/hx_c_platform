#ifndef HX_C_PLATFORM_POWER_PRIVATE_H
#define HX_C_PLATFORM_POWER_PRIVATE_H
#include "internal.h"
#include "hx_os_power.h"
enum { HX_OS_I_SLEEP_REQUEST, HX_OS_I_PERFORMANCE_REQUEST };
typedef struct hx_os_i_power {
	hx_string reason;
	uintptr_t native;
	void *detail;
	int kind;
	int active;
} hx_os_i_power;
struct hx_os_sleep_inhibit {
	hx_os_i_power power;
};
struct hx_os_performance_token {
	hx_os_i_power power;
};
hx_os_status hx_os_i_power_open(hx_os_i_power *power, hx_os_error *error);
hx_os_status hx_os_i_power_set(
	hx_os_i_power *power, int active, hx_os_error *error);
hx_os_status hx_os_i_power_dispose(hx_os_i_power *power, hx_os_error *error);
#endif
