#include "my_uvc/my_uvc.h"

#include "my_uvc_internal.hpp"
#include "my_uvc_video_id_resolve.h"

#include <new>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "uvc_control.h"
#include "uvc-gadget.h"
#include "uvc_video.h"
}

static my_uvc_t *g_trampoline_ctx;

static int open_trampoline(int width, int height, int fcc, int fps)
{
	if (!g_trampoline_ctx)
		return 0;
	auto *impl = reinterpret_cast<my_uvc_opaque *>(g_trampoline_ctx);
	if (!impl->cfg.on_open)
		return 0;
	return impl->cfg.on_open(impl->cfg.user_data, width, height, fcc, fps);
}

static void close_trampoline(void)
{
	if (!g_trampoline_ctx)
		return;
	auto *impl = reinterpret_cast<my_uvc_opaque *>(g_trampoline_ctx);
	if (impl->cfg.on_close)
		impl->cfg.on_close(impl->cfg.user_data);
}

static my_uvc_err_t submit_by_channel(my_uvc_opaque *impl, int channel_id, void *data, size_t len)
{
	my_uvc_err_t v = my_uvc_validate_submit_buffer(impl, channel_id, data, len);
	if (v != MY_UVC_OK)
		return v;

	int vid = my_uvc_resolve_video_id_for_submit(static_cast<unsigned int>(channel_id));
	if (vid < 0)
		return MY_UVC_ERR_NO_DEVICE;
	if (!uvc_video_get_uvc_process(vid))
		return MY_UVC_ERR_NOT_STREAMING;

	uvc_read_camera_buffer_by_id(data, -1, len, nullptr, 0, vid);
	return MY_UVC_OK;
}

extern "C" MY_UVC_API my_uvc_t *my_uvc_create(const my_uvc_config_t *cfg)
{
	if (!cfg || cfg->channels < 1 || cfg->channels > MY_UVC_MAX_CHANNELS || cfg->width <= 0 ||
	    cfg->height <= 0)
		return nullptr;
	auto *impl = new (std::nothrow) my_uvc_opaque{};
	if (!impl)
		return nullptr;
	impl->cfg = *cfg;
	return reinterpret_cast<my_uvc_t *>(impl);
}

extern "C" MY_UVC_API void my_uvc_destroy(my_uvc_t *ctx)
{
	if (!ctx)
		return;
	delete reinterpret_cast<my_uvc_opaque *>(ctx);
}

extern "C" MY_UVC_API int my_uvc_start(my_uvc_t *ctx, uint32_t uvc_control_flags)
{
	if (!ctx)
		return -1;
	auto *impl = reinterpret_cast<my_uvc_opaque *>(ctx);

	char buf[16];
	std::snprintf(buf, sizeof(buf), "%d", impl->cfg.channels);
	setenv("UVC_CNT", buf, 1);

	const char *fmt = impl->cfg.is_mjpeg ? "MJPEG" : "H.264";
	uvc_formats_init(fmt, impl->cfg.width, impl->cfg.height);

	g_trampoline_ctx = ctx;
	register_uvc_open_camera(open_trampoline);
	register_uvc_close_camera(close_trampoline);

	if (uvc_control_run(uvc_control_flags) != 0) {
		g_trampoline_ctx = nullptr;
		uvc_formats_deinit();
		return -1;
	}
	return 0;
}

extern "C" MY_UVC_API void my_uvc_control_join(my_uvc_t *ctx, uint32_t uvc_control_flags)
{
	(void)ctx;
	uvc_control_join(uvc_control_flags);
	g_trampoline_ctx = nullptr;
}

extern "C" MY_UVC_API void my_uvc_formats_deinit(my_uvc_t *ctx)
{
	(void)ctx;
	uvc_formats_deinit();
}

extern "C" MY_UVC_API int my_uvc_channel_video_id(int channel_id)
{
	if (channel_id < 0)
		return -1;
	return uvc_video_id_get(static_cast<unsigned int>(channel_id));
}

extern "C" MY_UVC_API int my_uvc_video_streaming(int video_id)
{
	if (video_id < 0)
		return 0;
	return uvc_video_get_uvc_process(video_id) ? 1 : 0;
}

extern "C" MY_UVC_API my_uvc_err_t my_uvc_submit_mjpeg(my_uvc_t *ctx, int channel_id, const void *data,
                                                       size_t len)
{
	if (!ctx)
		return MY_UVC_ERR_INVALID_ARG;
	auto *impl = reinterpret_cast<my_uvc_opaque *>(ctx);
	return submit_by_channel(impl, channel_id, const_cast<void *>(data), len);
}

extern "C" MY_UVC_API my_uvc_err_t my_uvc_submit_h264(my_uvc_t *ctx, int channel_id, const void *data,
                                                      size_t len)
{
	if (!ctx)
		return MY_UVC_ERR_INVALID_ARG;
	auto *impl = reinterpret_cast<my_uvc_opaque *>(ctx);
	return submit_by_channel(impl, channel_id, const_cast<void *>(data), len);
}
