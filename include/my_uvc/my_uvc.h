/*
 * libmy_uvc — UVC stack + frame submit (stable C ABI).
 */

#ifndef MY_UVC_H
#define MY_UVC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bump when the public ABI or semantics change incompatibly. */
#define MY_UVC_API_VERSION 1

/** Max logical channels (matches application kMaxUvcChannels). */
#define MY_UVC_MAX_CHANNELS 16

#if defined(_WIN32) && defined(MY_UVC_BUILD_SHARED)
#ifdef my_uvc_lib_EXPORTS
#define MY_UVC_API __declspec(dllexport)
#else
#define MY_UVC_API __declspec(dllimport)
#endif
#else
#define MY_UVC_API __attribute__((visibility("default")))
#endif

typedef struct my_uvc_opaque my_uvc_t;

typedef enum my_uvc_err {
	MY_UVC_OK = 0,
	MY_UVC_ERR_INVALID_ARG = -1,
	MY_UVC_ERR_INTERNAL = -2,
	MY_UVC_ERR_NO_DEVICE = -3,
	MY_UVC_ERR_NOT_STREAMING = -4,
} my_uvc_err_t;

typedef int (*my_uvc_on_open_fn)(void *user, int width, int height, int fcc, int fps);
typedef void (*my_uvc_on_close_fn)(void *user);

/**
 * Runtime config for the shared library.
 * INI keys live under **[libmy_uvc]** in ``config/libmy_uvc.ini``; the C++ mirror is
 * **LibmyUvcIniFields** (`app_config.h`, member `AppConfig::libmy_uvc`) used by `uvctest`.
 */
typedef struct my_uvc_config {
	int channels;
	int width;
	int height;
	/** 1 = MJPEG, 0 = H.264 */
	int is_mjpeg;
	my_uvc_on_open_fn on_open;
	my_uvc_on_close_fn on_close;
	void *user_data;
} my_uvc_config_t;

/**
 * Merge keys from a single INI `[section]` in one file into defaults, then fill `my_uvc_config_t`.
 * Does not load directories — one file only (same rules as `load_app_config_section_from_file` in app_config).
 * Typical sections: "libmy_uvc", "my_uvc" (legacy). Other sections apply their keys onto defaults; only fields
 * used by `my_uvc_create` are copied here.
 *
 * @param errbuf optional; on failure, short error message (truncated to errbuf_len - 1).
 * @return MY_UVC_OK or MY_UVC_ERR_INVALID_ARG / MY_UVC_ERR_INTERNAL.
 */
MY_UVC_API my_uvc_err_t my_uvc_load_ini_section_only(const char *path, const char *section, my_uvc_config_t *out,
                                                     my_uvc_on_open_fn on_open, my_uvc_on_close_fn on_close,
                                                     void *user_data, char *errbuf, size_t errbuf_len);

MY_UVC_API my_uvc_t *my_uvc_create(const my_uvc_config_t *cfg);
MY_UVC_API void my_uvc_destroy(my_uvc_t *ctx);

/**
 * Sets UVC_CNT, negotiates formats, registers open/close hooks, starts uvc_control.
 * On failure, streaming is torn down; caller may my_uvc_destroy(ctx).
 */
MY_UVC_API int my_uvc_start(my_uvc_t *ctx, uint32_t uvc_control_flags);

MY_UVC_API void my_uvc_control_join(my_uvc_t *ctx, uint32_t uvc_control_flags);
MY_UVC_API void my_uvc_formats_deinit(my_uvc_t *ctx);

/** Logical channel index → current kernel video id, or -1 if none. */
MY_UVC_API int my_uvc_channel_video_id(int channel_id);
/** Non-zero if this video_id is actively streaming UVC. */
MY_UVC_API int my_uvc_video_streaming(int video_id);

MY_UVC_API my_uvc_err_t my_uvc_submit_mjpeg(my_uvc_t *ctx, int channel_id, const void *data, size_t len);
MY_UVC_API my_uvc_err_t my_uvc_submit_h264(my_uvc_t *ctx, int channel_id, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
