#include "app_config.h"
#include "my_uvc/my_uvc.h"

#include <cstring>
#include <string>

extern "C" MY_UVC_API my_uvc_err_t my_uvc_load_ini_section_only(const char *path, const char *section,
                                                                my_uvc_config_t *out, my_uvc_on_open_fn on_open,
                                                                my_uvc_on_close_fn on_close, void *user_data,
                                                                char *errbuf, size_t errbuf_len)
{
	if (!path || !section || !out)
		return MY_UVC_ERR_INVALID_ARG;

	std::string err;
	AppConfig cfg = default_app_config();
	if (!load_app_config_section_from_file(path, section, &cfg, &err)) {
		if (errbuf && errbuf_len > 0) {
			std::strncpy(errbuf, err.c_str(), errbuf_len - 1);
			errbuf[errbuf_len - 1] = '\0';
		}
		return MY_UVC_ERR_INTERNAL;
	}

	std::memset(out, 0, sizeof(*out));
	out->channels = cfg.channels;
	out->width = cfg.width;
	out->height = cfg.height;
	out->is_mjpeg = (cfg.video_codec == "mjpeg") ? 1 : 0;
	out->on_open = on_open;
	out->on_close = on_close;
	out->user_data = user_data;
	return MY_UVC_OK;
}
