#include "my_uvc_video_id_resolve.h"

extern "C" int uvc_video_id_get(unsigned int seq);

static int (*g_video_id_hook)(unsigned int) = nullptr;

extern "C" void my_uvc_test_set_video_id_hook(int (*fn)(unsigned int channel_id))
{
	g_video_id_hook = fn;
}

extern "C" int my_uvc_resolve_video_id_for_submit(unsigned int channel_id)
{
	if (g_video_id_hook)
		return g_video_id_hook(channel_id);
	return uvc_video_id_get(channel_id);
}
