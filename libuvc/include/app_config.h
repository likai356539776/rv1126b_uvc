#ifndef MY_UVC_APP_CONFIG_H_
#define MY_UVC_APP_CONFIG_H_

#include <array>
#include <string>

constexpr int kMaxUvcChannels = 16;

/**
 * [libmy_uvc] — UVC 栈与编码尺寸、H.264 策略、库侧日志。
 * 物理配置见 config/libmy_uvc.ini；与 C API my_uvc_config_t 的映射见 uvctest_fill_my_uvc_config()、my_uvc_load_ini_section_only(..., "libmy_uvc", ...)。
 */
struct LibmyUvcIniFields {
	int channels;
	int width;
	int height;
	int fps;
	int idle_sleep_ms;
	bool loop_file;
	bool prefer_host_fps;
	bool sync_to_idr_on_open;
	bool inject_sps_pps_on_idr;
	int startup_prime_frames;
	int log_level;
	/** "h264" | "mjpeg" — must match my_uvc_usb_config.sh -f (H.264 / MJPEG). */
	std::string video_codec;
};

/**
 * [libmy_uvc_pip] — PiP 合成（静态库 pip_helper），与 include/my_uvc_pip/pip_helper.h / config/libmy_uvc_pip.ini 一致。
 * libmy_uvc.so 不包含 PiP；应用链接 pip_helper 时使用这些字段填 pip_helper_config_t。
 */
struct LibmyUvcPipIniFields {
	bool pip_enable;
	std::string pip_overlay_path;
	int pip_x;
	int pip_y;
	int pip_w;
	int pip_h;
	int pip_jpeg_quality;
	/** [libmy_uvc_pip] 非负：0=禁用超时（仅冻结上一帧）；缺省见 default_app_config()（5000）。 */
	int pip_overlay_stale_timeout_ms;
	/** 下三分之一网格槽位上限 0~16；0=无网格（仅主讲人）。 */
	int pip_tile_n_tiles;
	int pip_tile_gap_px;
	int pip_tile_margin_px;
	float pip_width_stretch_factor;
	/** Adaptive presenter window size factors relative to UVC output (canvas) resolution */
	float pip_adaptive_scale_w;
	float pip_adaptive_scale_h;
	bool pip_border_enable;
	int pip_border_radius;
	int pip_border_thickness;
	std::string pip_border_color;
};

/**
 * [uvctest] — 测试程序专用：媒体路径、周期日志、统计、逐路覆盖；见 config/uvctest.ini。
 * 不随 libmy_uvc.so 发布；仅 uvctest / load_app_config 使用。
 */
struct UvctestIniFields {
	int log_every_frames;
	bool stats_enable;
	int stats_interval_sec;
	std::string h264_path;
	/** 逗号分隔的 NV12 裸文件路径（`;` 为 ini 注释不可用），与 PiP 槽 0.. 对齐；仅 uvctest。 */
	std::string pip_tile_test_nv12_paths;
	/** 测试 NV12 文件的源分辨率（如 640×480）；0,0 表示与槽位显示尺寸一致。 */
	int pip_tile_test_nv12_src_w;
	int pip_tile_test_nv12_src_h;
	std::array<int, kMaxUvcChannels> channel_fps;
	std::array<std::string, kMaxUvcChannels> channel_h264_path;
	float yolo_score_threshold;
	std::string camera_type;
	std::string camera_node;
	int camera_width;
	int camera_height;
};

/**
 * 合并后的应用配置：目录模式下按 libmy_uvc.ini → libmy_uvc_pip.ini → uvctest.ini 叠加；
 * 单文件 legacy 仍为 [my_uvc] 全键。各段含义见上方三结构体。
 */
struct AppConfig {
	LibmyUvcIniFields libmy_uvc{};
	LibmyUvcPipIniFields libmy_uvc_pip{};
	UvctestIniFields uvctest{};
};

AppConfig default_app_config();

/**
 * Load configuration into cfg (starts from default_app_config(), then merges).
 *
 * - If `path` is a **directory**, merges in order (later files override earlier keys):
 *     libmy_uvc.ini, libmy_uvc_pip.ini, uvctest.ini
 *   If none of those exist, loading fails (at least one split file must be present).
 *   Single-file configs (e.g. legacy `[my_uvc]` in one file) still work when `-c` points to a file path.
 *   (legacy monolithic [my_uvc]).
 * - If `path` is a **file**, parses that file (supports [my_uvc]/[uvc] legacy all-in-one,
 *   or split sections [libmy_uvc], [libmy_uvc_pip], [uvctest] in one file).
 */
bool load_app_config(const std::string &path, AppConfig *cfg, std::string *err);

/**
 * Merge a single ini file into `cfg`, applying only keys under `[section]` (e.g. `libmy_uvc`, `libmy_uvc_pip`,
 * `uvctest`, or legacy `my_uvc`). Caller initializes `*cfg` (typically `default_app_config()`).
 * Does not load directories — one file only.
 */
bool load_app_config_section_from_file(const std::string &path, const std::string &section, AppConfig *cfg,
                                       std::string *err);

#endif // MY_UVC_APP_CONFIG_H_
