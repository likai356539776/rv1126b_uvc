/**
 * P1-U1: AppConfig → my_uvc_config_t mapping (same rules as uvctest_fill_my_uvc_config).
 */
#include "app_config.h"
#include "my_uvc/my_uvc.h"
#include "uvctest/libmy_uvc_config_bridge.hpp"

static int dummy_open(void *, int, int, int, int) {
	return 0;
}
static void dummy_close(void *) {}

int main()
{
	AppConfig app = default_app_config();
	app.channels = 4;
	app.width = 1280;
	app.height = 720;
	app.video_codec = "h264";

	my_uvc_config_t out{};
	uvctest_fill_my_uvc_config(app, dummy_open, dummy_close, reinterpret_cast<void *>(0x1), &out);

	if (out.channels != 4 || out.width != 1280 || out.height != 720 || out.is_mjpeg != 0)
		return 1;
	if (out.on_open != dummy_open || out.on_close != dummy_close || out.user_data != reinterpret_cast<void *>(0x1))
		return 1;

	app.video_codec = "mjpeg";
	uvctest_fill_my_uvc_config(app, nullptr, nullptr, nullptr, &out);
	if (out.is_mjpeg != 1)
		return 1;

	app.channels = MY_UVC_MAX_CHANNELS;
	app.width = 1920;
	app.height = 1080;
	uvctest_fill_my_uvc_config(app, nullptr, nullptr, nullptr, &out);
	if (out.channels != MY_UVC_MAX_CHANNELS)
		return 1;

	return 0;
}
