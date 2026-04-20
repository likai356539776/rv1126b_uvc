#include "uvctest/libmy_uvc_config_bridge.hpp"

#include <cstring>

void uvctest_fill_my_uvc_config(const AppConfig &app, my_uvc_on_open_fn on_open, my_uvc_on_close_fn on_close,
                                void *user_data, my_uvc_config_t *out)
{
	if (!out)
		return;
	std::memset(out, 0, sizeof(*out));
	out->channels = app.libmy_uvc.channels;
	out->width = app.libmy_uvc.width;
	out->height = app.libmy_uvc.height;
	out->is_mjpeg = (app.libmy_uvc.video_codec == "mjpeg") ? 1 : 0;
	out->on_open = on_open;
	out->on_close = on_close;
	out->user_data = user_data;
}
