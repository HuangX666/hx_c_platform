#ifndef HX_C_PLATFORM_COORD_PRIVATE_H
#define HX_C_PLATFORM_COORD_PRIVATE_H

#include "internal.h"
#include "hx_os_coord.h"

typedef struct hx_os_cancel_watch {
	hx_os_mutex *mutex;
	hx_os_condition *condition;
	hx_os_cancel_token *token;
	uint64_t id;
} hx_os_cancel_watch;

/**
 * \brief Register before acquiring the target mutex; retire after releasing it.
 *
 * \details This lock order prevents a cancellation wake from being lost or
 * deadlocking.
 */
hx_os_status hx_os_i_cancel_watch(hx_os_cancel_watch *watch,
	hx_os_cancel_token *token, hx_os_mutex *mutex,
	hx_os_condition *condition, hx_os_error *error);
void hx_os_i_cancel_unwatch(hx_os_cancel_watch *watch);

#endif
