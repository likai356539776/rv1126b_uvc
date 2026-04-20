/*
 * libmy_uvc_pip_helper — MJPEG PiP compose (RGA/MPP + software JPEG). Does not link libmy_uvc.
 *
 * INI: **LibmyUvcPipIniFields** in `app_config.h` (`AppConfig::libmy_uvc_pip`) maps **[libmy_uvc_pip]**
 * in ``config/libmy_uvc_pip.ini``; canvas **width/height** come from **[libmy_uvc]** (``libmy_uvc.ini``).
 */
#ifndef MY_UVC_PIP_HELPER_H
#define MY_UVC_PIP_HELPER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Implementation identifier (for logs). */
const char *pip_helper_version(void);

/** Last error from create/composite (thread-local), empty if none. */
const char *pip_helper_last_error(void);

/**
 * Mirrors [libmy_uvc_pip] + canvas size from [libmy_uvc]. Strings must outlive pip_helper_create
 * only for the duration of that call (copied internally).
 */
typedef struct pip_helper_config {
	int pip_enable;
	int canvas_width;
	int canvas_height;
	int pip_x;
	int pip_y;
	int pip_w;
	int pip_h;
	int pip_jpeg_quality;
	const char *pip_overlay_path;
} pip_helper_config_t;

typedef struct pip_helper pip_helper_t;

/**
 * One instance per logical channel (§9 ④). Returns NULL on failure (see pip_helper_last_error).
 * When pip_enable is 0, returns NULL (caller uses non-PiP path).
 */
pip_helper_t *pip_helper_create(int channel_id, const pip_helper_config_t *cfg);
void pip_helper_destroy(pip_helper_t *h);

/**
 * Composite one background MJPEG frame with preloaded overlay NV12. On success returns 0 and sets
 * *out_jpeg / *out_len pointing to memory valid until the next composite call or destroy.
 */
int pip_helper_composite_mjpeg(pip_helper_t *h, const uint8_t *bg_jpeg, size_t bg_jpeg_len,
                               const uint8_t **out_jpeg, size_t *out_jpeg_len);

/** Legacy alias for logs; same as pip_helper_version(). */
const char *pip_helper_stub_version(void);

#ifdef __cplusplus
}
#endif

#endif
