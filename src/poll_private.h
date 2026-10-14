#ifndef HX_C_PLATFORM_POLL_PRIVATE_H
#define HX_C_PLATFORM_POLL_PRIVATE_H

/**
 * \brief Stored in backend event records; its owner outlives registration.
 */
typedef struct hx_os_i_poll_source {
	int ready;
	int watched;
} hx_os_i_poll_source;
#endif
