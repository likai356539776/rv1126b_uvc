/** P3-U2: PiP 相关缺省与 default_app_config() 一致；合并 ini 未写 pip 键时保持不变。 */
#include "app_config.h"
#include "my_uvc_pip/pip_helper.h"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

/* 与 src/app_config.cpp 中 default_app_config() 的 pip 字段保持同步（回归防漂移）。 */
static constexpr int kDefPipX = 320;
static constexpr int kDefPipY = 720;
static constexpr int kDefPipW = 1280;
static constexpr int kDefPipH = 360;
static constexpr int kDefPipJpegQ = 85;
static constexpr int kDefPipStaleMs = 5000;
static constexpr float kDefPipStretchW = 1.3f;

static int write_file(const fs::path &p, const char *text)
{
	std::ofstream o(p);
	if (!o)
		return 1;
	o << text;
	return o.good() ? 0 : 1;
}

static void fill_pip_helper_from_app(const AppConfig &app, pip_helper_config_t *out)
{
	out->pip_enable = app.libmy_uvc_pip.pip_enable ? 1 : 0;
	out->canvas_width = app.libmy_uvc.width;
	out->canvas_height = app.libmy_uvc.height;
	out->pip_x = app.libmy_uvc_pip.pip_x;
	out->pip_y = app.libmy_uvc_pip.pip_y;
	out->pip_w = app.libmy_uvc_pip.pip_w;
	out->pip_h = app.libmy_uvc_pip.pip_h;
	out->pip_jpeg_quality = app.libmy_uvc_pip.pip_jpeg_quality;
	out->pip_overlay_path = app.libmy_uvc_pip.pip_overlay_path.c_str();
	out->pip_overlay_stale_timeout_ms = app.libmy_uvc_pip.pip_overlay_stale_timeout_ms;
	out->pip_tile_n_tiles = app.libmy_uvc_pip.pip_tile_n_tiles;
	out->pip_tile_gap_px = app.libmy_uvc_pip.pip_tile_gap_px;
	out->pip_tile_margin_px = app.libmy_uvc_pip.pip_tile_margin_px;
	out->pip_width_stretch_factor = app.libmy_uvc_pip.pip_width_stretch_factor;
}

static int check_default_pip_fields(const AppConfig &cfg)
{
	if (cfg.libmy_uvc_pip.pip_enable != false)
		return 1;
	if (!cfg.libmy_uvc_pip.pip_overlay_path.empty())
		return 1;
	if (cfg.libmy_uvc_pip.pip_x != kDefPipX || cfg.libmy_uvc_pip.pip_y != kDefPipY ||
	    cfg.libmy_uvc_pip.pip_w != kDefPipW || cfg.libmy_uvc_pip.pip_h != kDefPipH)
		return 1;
	if (cfg.libmy_uvc_pip.pip_jpeg_quality != kDefPipJpegQ)
		return 1;
	if (cfg.libmy_uvc_pip.pip_overlay_stale_timeout_ms != kDefPipStaleMs)
		return 1;
	if (cfg.libmy_uvc_pip.pip_tile_n_tiles != 0 || cfg.libmy_uvc_pip.pip_tile_gap_px != 0 ||
	    cfg.libmy_uvc_pip.pip_tile_margin_px != 0)
		return 1;
	if (cfg.libmy_uvc_pip.pip_width_stretch_factor != kDefPipStretchW)
		return 1;
	pip_helper_config_t p{};
	fill_pip_helper_from_app(cfg, &p);
	if (p.pip_enable != 0 || p.canvas_width != cfg.libmy_uvc.width || p.canvas_height != cfg.libmy_uvc.height)
		return 1;
	if (p.pip_x != kDefPipX || p.pip_y != kDefPipY || p.pip_w != kDefPipW || p.pip_h != kDefPipH)
		return 1;
	if (p.pip_jpeg_quality != kDefPipJpegQ)
		return 1;
	if (p.pip_overlay_stale_timeout_ms != kDefPipStaleMs)
		return 1;
	if (p.pip_tile_n_tiles != 0 || p.pip_tile_gap_px != 0 || p.pip_tile_margin_px != 0)
		return 1;
	if (p.pip_width_stretch_factor != kDefPipStretchW)
		return 1;
	if (p.pip_overlay_path != nullptr && p.pip_overlay_path[0] != '\0')
		return 1;
	return 0;
}

int main()
{
	AppConfig d = default_app_config();
	if (check_default_pip_fields(d))
		return 1;

	std::string err;
	const fs::path root = fs::temp_directory_path() / (std::string("uvctest_cfg_p3u2_") + std::to_string(getpid()));
	fs::remove_all(root);
	fs::create_directories(root);
	/* 目录合并：仅有 libmy_uvc.ini，无 pip 键 → pip 仍为 default_app_config() */
	const char *libmy = R"([libmy_uvc]
channels = 1
width = 1280
height = 720
video_codec = mjpeg
)";
	if (write_file(root / "libmy_uvc.ini", libmy))
		return 1;
	AppConfig merged{};
	if (!load_app_config(root.string(), &merged, &err))
		return 1;
	if (merged.libmy_uvc.width != 1280 || merged.libmy_uvc.height != 720)
		return 1;
	if (check_default_pip_fields(merged))
		return 1;

	return 0;
}
