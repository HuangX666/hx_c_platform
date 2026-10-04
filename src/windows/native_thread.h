#ifndef HX_C_PLATFORM_WINDOWS_NATIVE_THREAD_H
#define HX_C_PLATFORM_WINDOWS_NATIVE_THREAD_H

#include <windows.h>

typedef union hx_os_i_mutex_native {
	SRWLOCK normal;
	CRITICAL_SECTION recursive;
} hx_os_i_mutex_native;

typedef CONDITION_VARIABLE hx_os_i_condition_native;
typedef struct hx_os_i_thread_native {
	HANDLE handle;
	unsigned int id;
} hx_os_i_thread_native;

typedef DWORD hx_os_i_tls_native;

#endif
