#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_os_stream *stream = NULL;
	hx_os_frame *frame = NULL;
	hx_string message = HX_STRING_INIT;
	hx_os_status status;
	int accepted = 0;
	uint64_t position;

	status = hx_os_stream_memory(NULL, 4096, &stream, NULL);
	if (status == HX_OS_OK)
		status = hx_os_frame_from_stream(
			NULL, stream, 4096, &frame, NULL);
	if (status == HX_OS_OK)
		status = hx_os_frame_send_text_until(frame,
			hx_string_view_from_cstr(
				"{\"device\":\"door-1\",\"request\":42}"),
			HX_OS_DEADLINE_INFINITE, &accepted, NULL);
	if (status == HX_OS_OK)
		status = hx_os_stream_seek(stream, 0, 0, &position, NULL);
	if (status == HX_OS_OK)
		status = hx_os_frame_receive_text_until(
			frame, HX_OS_DEADLINE_INFINITE, &message, NULL);
	if (status == HX_OS_OK)
		puts(hx_string_c_str(&message));
	else
		fprintf(stderr, "Frame: %s\n", hx_os_status_name(status));

	if (frame && status != HX_OS_OK)
		(void)hx_os_frame_abort(frame, NULL);
	(void)hx_os_frame_destroy(&frame, NULL);
	(void)hx_os_stream_close(&stream, NULL);
	hx_string_destroy(&message);
	return status == HX_OS_OK ? 0 : 1;
}
