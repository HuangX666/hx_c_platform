#ifndef HX_C_PLATFORM_KCP_SYMBOLS_H
#define HX_C_PLATFORM_KCP_SYMBOLS_H

// Private names prevent collisions with an application's own KCP copy.
#define ikcp_create hx_os_i_kcp_create
#define ikcp_release hx_os_i_kcp_release
#define ikcp_setoutput hx_os_i_kcp_setoutput
#define ikcp_recv hx_os_i_kcp_recv
#define ikcp_send hx_os_i_kcp_send
#define ikcp_update hx_os_i_kcp_update
#define ikcp_check hx_os_i_kcp_check
#define ikcp_input hx_os_i_kcp_input
#define ikcp_flush hx_os_i_kcp_flush
#define ikcp_peeksize hx_os_i_kcp_peeksize
#define ikcp_setmtu hx_os_i_kcp_setmtu
#define ikcp_wndsize hx_os_i_kcp_wndsize
#define ikcp_waitsnd hx_os_i_kcp_waitsnd
#define ikcp_nodelay hx_os_i_kcp_nodelay
#define ikcp_log hx_os_i_kcp_log
#define ikcp_allocator hx_os_i_kcp_allocator
#define ikcp_getconv hx_os_i_kcp_getconv
#define ikcp_qprint hx_os_i_kcp_qprint
#define ikcp_parse_data hx_os_i_kcp_parse_data
#define IKCP_RTO_NDL hx_os_i_kcp_rto_ndl
#define IKCP_RTO_MIN hx_os_i_kcp_rto_min
#define IKCP_RTO_DEF hx_os_i_kcp_rto_def
#define IKCP_RTO_MAX hx_os_i_kcp_rto_max
#define IKCP_CMD_PUSH hx_os_i_kcp_cmd_push
#define IKCP_CMD_ACK hx_os_i_kcp_cmd_ack
#define IKCP_CMD_WASK hx_os_i_kcp_cmd_wask
#define IKCP_CMD_WINS hx_os_i_kcp_cmd_wins
#define IKCP_ASK_SEND hx_os_i_kcp_ask_send
#define IKCP_ASK_TELL hx_os_i_kcp_ask_tell
#define IKCP_WND_SND hx_os_i_kcp_wnd_snd
#define IKCP_WND_RCV hx_os_i_kcp_wnd_rcv
#define IKCP_MTU_DEF hx_os_i_kcp_mtu_def
#define IKCP_ACK_FAST hx_os_i_kcp_ack_fast
#define IKCP_INTERVAL hx_os_i_kcp_interval
#define IKCP_OVERHEAD hx_os_i_kcp_overhead
#define IKCP_DEADLINK hx_os_i_kcp_deadlink
#define IKCP_THRESH_INIT hx_os_i_kcp_thresh_init
#define IKCP_THRESH_MIN hx_os_i_kcp_thresh_min
#define IKCP_PROBE_INIT hx_os_i_kcp_probe_init
#define IKCP_PROBE_LIMIT hx_os_i_kcp_probe_limit
#define IKCP_FASTACK_LIMIT hx_os_i_kcp_fastack_limit

#endif
