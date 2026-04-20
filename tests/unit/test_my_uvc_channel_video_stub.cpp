/** P1-U3: video_id hook for submit routing (my_uvc_resolve_video_id_for_submit). */
#include "my_uvc_video_id_resolve.h"

extern "C" int uvc_video_id_get(unsigned int seq)
{
	(void)seq;
	return 99;
}

static int hook_times10(unsigned int c)
{
	return static_cast<int>(c) * 10;
}

int main()
{
	my_uvc_test_set_video_id_hook(nullptr);
	if (my_uvc_resolve_video_id_for_submit(0) != 99)
		return 1;

	my_uvc_test_set_video_id_hook(hook_times10);
	if (my_uvc_resolve_video_id_for_submit(3) != 30)
		return 1;

	my_uvc_test_set_video_id_hook(nullptr);
	if (my_uvc_resolve_video_id_for_submit(0) != 99)
		return 1;

	return 0;
}
