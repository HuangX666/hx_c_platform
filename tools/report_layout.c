#include "loop_private.h"
#include "thread_private.h"
#include "file_private.h"
#include "vm_private.h"
#include "hx_os_file_extra.h"
#include "watch_private.h"
#include "process_private.h"
#include "thread_pool_private.h"
#include "duplex_private.h"
#include "power_private.h"
#include "hx_os_obfuscate.h"
#include "hx_os_bitstream.h"
#include "hx_os_text_lookup.h"
#include "hx_os_wide.h"
#include "hx_os_lexer.h"
#include "hx_os_procedure.h"
#include "hx_variant.h"
#include "signal_private.h"
#include "settings_private.h"
#include "profiler_private.h"
#include "coroutine_private.h"
#include "hx_format.h"
#include "ipc_private.h"
#include <stdio.h>

#define HX_OS_LAYOUT(type)                                                     \
	do {                                                                   \
		typedef struct hx_os_layout_##type {                           \
			char prefix;                                           \
			type value;                                            \
		} hx_os_layout_##type;                                         \
		printf("%s|%zu|%zu\n", #type, sizeof(type),                    \
			offsetof(hx_os_layout_##type, value));                 \
	} while (0)

int main(void)
{
	HX_OS_LAYOUT(hx_string);
	HX_OS_LAYOUT(hx_ipc_message);
	HX_OS_LAYOUT(hx_ipc_options);
	HX_OS_LAYOUT(hx_ipc_statistics);
	HX_OS_LAYOUT(hx_os_kcp_options);
#if HX_OS_HAVE_IPC
	HX_OS_LAYOUT(hx_ipc);
	HX_OS_LAYOUT(hx_ipc_connection);
#endif
	HX_OS_LAYOUT(hx_format_arg);
	HX_OS_LAYOUT(hx_format_options);

	HX_OS_LAYOUT(hx_os_coro_errors);
	HX_OS_LAYOUT(hx_os_coro);
	HX_OS_LAYOUT(hx_os_coro_worker);
	HX_OS_LAYOUT(hx_os_i_coro_context);
	HX_OS_LAYOUT(hx_os_coro_runtime);

	HX_OS_LAYOUT(hx_variant);
	HX_OS_LAYOUT(hx_variant_type);

	HX_OS_LAYOUT(hx_os_uint128);
	HX_OS_LAYOUT(hx_os_token);
	HX_OS_LAYOUT(hx_os_lexer_options);
	HX_OS_LAYOUT(hx_os_proc_options);

	HX_OS_LAYOUT(hx_os_atomic);
	HX_OS_LAYOUT(hx_os_atomic32);
	HX_OS_LAYOUT(hx_os_atomic_ptr);

	HX_OS_LAYOUT(hx_os_error);
	HX_OS_LAYOUT(hx_os_operation);
	HX_OS_LAYOUT(hx_os_pipe_operation);
	HX_OS_LAYOUT(hx_os_thread_options);
	HX_OS_LAYOUT(hx_os_file_info_extended);
	HX_OS_LAYOUT(hx_os_thread);
	HX_OS_LAYOUT(hx_os_socket);
	HX_OS_LAYOUT(hx_os_file);
	HX_OS_LAYOUT(hx_os_loop);
	HX_OS_LAYOUT(hx_os_request);
	HX_OS_LAYOUT(hx_os_pipe);
	HX_OS_LAYOUT(hx_os_timer);
	HX_OS_LAYOUT(hx_os_pages);
	HX_OS_LAYOUT(hx_os_file_map);
	HX_OS_LAYOUT(hx_os_file_times);

	HX_OS_LAYOUT(hx_os_watch);
	HX_OS_LAYOUT(hx_os_watch_options);
	HX_OS_LAYOUT(hx_os_watch_event);
	HX_OS_LAYOUT(hx_os_loop_watch);

	HX_OS_LAYOUT(hx_os_string_list);
	HX_OS_LAYOUT(hx_os_process);
	HX_OS_LAYOUT(hx_os_process_options);
	HX_OS_LAYOUT(hx_os_process_result);
	HX_OS_LAYOUT(hx_os_process_capture_options);

	HX_OS_LAYOUT(hx_os_task);
	HX_OS_LAYOUT(hx_os_thread_pool);
	HX_OS_LAYOUT(hx_os_delayed_task);

	HX_OS_LAYOUT(hx_os_local_address);
	HX_OS_LAYOUT(hx_os_i_local_state);
	HX_OS_LAYOUT(hx_os_i_power);

	HX_OS_LAYOUT(hx_os_obfuscated);
	HX_OS_LAYOUT(hx_os_bit_reader);
	HX_OS_LAYOUT(hx_os_bit_writer);
	HX_OS_LAYOUT(hx_os_text_lookup_options);

	HX_OS_LAYOUT(hx_os_calldata);
	HX_OS_LAYOUT(hx_os_signal_options);
	HX_OS_LAYOUT(hx_os_signal_slot);
	HX_OS_LAYOUT(hx_os_signal_handler);

	HX_OS_LAYOUT(hx_settings_options);
	HX_OS_LAYOUT(hx_settings_change);
	HX_OS_LAYOUT(hx_settings_value);
	HX_OS_LAYOUT(hx_settings_entry);
	HX_OS_LAYOUT(hx_settings);

	HX_OS_LAYOUT(hx_os_profiler_options);
	HX_OS_LAYOUT(hx_os_profiler);
	HX_OS_LAYOUT(hx_os_profile_record);
	HX_OS_LAYOUT(hx_os_profile_frame);
	HX_OS_LAYOUT(hx_os_profile_thread);
#if defined(_WIN32)
	HX_OS_LAYOUT(hx_os_i_duplex_file);
#endif
	return 0;
}
