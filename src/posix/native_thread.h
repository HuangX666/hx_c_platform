#ifndef HX_C_PLATFORM_POSIX_NATIVE_THREAD_H
#define HX_C_PLATFORM_POSIX_NATIVE_THREAD_H

#include <pthread.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif

typedef pthread_mutex_t hx_os_i_mutex_native;
typedef pthread_cond_t hx_os_i_condition_native;
typedef struct hx_os_i_thread_native {
	pthread_t id;
#if defined(__APPLE__)
	mach_port_t termination;
#endif
} hx_os_i_thread_native;

typedef pthread_key_t hx_os_i_tls_native;

#endif
