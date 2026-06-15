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
 * 调用方应 `memset(...,0)` 或 C++ 值初始化后再填字段，避免未初始化成员。
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
	/**
	 * 断流超时（毫秒，设计 v1.12）：`-1` = 库缺省 5000；`0` = 不因超时露背图；`>0` = 自定义。
	 * 从 AppConfig / ini 传入时仅为非负；第三方可直接设 `-1`。
	 */
	int pip_overlay_stale_timeout_ms;
	/**
	 * 下三分之一网格（§2.4）：`0` = 仅主讲人小窗，无网格；`1~16` = 槽位数，与 pip_tile_layout_bottom_third 一致。
	 */
	int pip_tile_n_tiles;
	int pip_tile_gap_px;
	int pip_tile_margin_px;
} pip_helper_config_t;

/**
 * Optional per-frame controls for single-file / NV12 主讲人路径（目录轮播 overlay 仍忽略 presenter 字段）。
 * 使用前应整块零初始化，避免 `n_active` / `tile_nv12` 等未赋值字段为脏数据。
 * @param now_ms 与 last_update 同域的单调毫秒；0 表示由库内 steady_clock 填充。
 * @param presenter_nv12_updated 非 0：本帧提交新 NV12 入主讲人缓存。
 * @param presenter_nv12 presenter_nv12_updated 非 0 时必填。
 * @param presenter_nv12_src_w/h 均 >0：源为该尺寸，将缩放到主讲人窗 ALIGN2(pip_w)×ALIGN2(pip_h)；均为 0：源须已是窗尺寸（与旧行为一致）。
 */
typedef struct pip_helper_composite_opts {
	int64_t now_ms;
	int presenter_nv12_updated;
	const uint8_t *presenter_nv12;
	int presenter_nv12_src_w;
	int presenter_nv12_src_h;
	/**
	 * 本帧参与合成的网格路数：`0 … pip_tile_n_tiles`（create 时布局上限）。
	 * `tile_nv12[i]`：源尺寸为 `tile_src_w[i]×tile_src_h[i]`（均 >0 且可与槽位不同则 RGA 缩放）；`tile_src_w`/`tile_src_h` 为 NULL 时源须已等于槽位 ALIGN2 尺寸。
	 */
	int n_active;
	const uint8_t **tile_nv12;
	const int *tile_src_w;
	const int *tile_src_h;
	/**
	 * 可选，长度 ≥ n_active。NULL：旧语义，每帧 tile_nv12[i] 均为新帧且不可为 NULL。
	 * 非 NULL：tile_nv12_updated[i]≠0 时须提供 tile_nv12[i]；为 0 时可不提供指针，库内沿用该槽
	 * NV12 缓存并套用 pip_overlay_stale_timeout_ms（与主讲人一致）；超时则该格不叠画（露背图）。
	 */
	const int *tile_nv12_updated;
} pip_helper_composite_opts_t;

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

/**
 * 同 pip_helper_composite_mjpeg，附加断流语义（§2.4）：opts 为 NULL 时等价于旧 API（每帧刷新主讲人时间戳，不触发超时）。
 * 目录 overlay 时 opts 中的 presenter 字段被忽略。
 */
int pip_helper_composite_mjpeg_ex(pip_helper_t *h, const uint8_t *bg_jpeg, size_t bg_jpeg_len,
                                  const pip_helper_composite_opts_t *opts, const uint8_t **out_jpeg,
                                  size_t *out_jpeg_len);

/** Legacy alias for logs; same as pip_helper_version(). */
const char *pip_helper_stub_version(void);

#ifdef __cplusplus
}
#endif

#endif
