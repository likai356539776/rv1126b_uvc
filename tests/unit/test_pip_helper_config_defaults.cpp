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
static constexpr int kDefPipX = 20;
static constexpr int kDefPipY = 20;
static constexpr int kDefPipW = 640;
static constexpr int kDefPipH = 480;
static constexpr int kDefPipJpegQ = 85;

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
	out->pip_enable = app.pip_enable ? 1 : 0;
	out->canvas_width = app.width;
	out->canvas_height = app.height;
	out->pip_x = app.pip_x;
	out->pip_y = app.pip_y;
	out->pip_w = app.pip_w;
	out->pip_h = app.pip_h;
	out->pip_jpeg_quality = app.pip_jpeg_quality;
	out->pip_overlay_path = app.pip_overlay_path.c_str();
}

static int check_default_pip_fields(const AppConfig &cfg)
{
	if (cfg.pip_enable != false)
		return 1;
	if (!cfg.pip_overlay_path.empty())
		return 1;
	if (cfg.pip_x != kDefPipX || cfg.pip_y != kDefPipY || cfg.pip_w != kDefPipW || cfg.pip_h != kDefPipH)
		return 1;
	if (cfg.pip_jpeg_quality != kDefPipJpegQ)
		return 1;
	pip_helper_config_t p{};
	fill_pip_helper_from_app(cfg, &p);
	if (p.pip_enable != 0 || p.canvas_width != cfg.width || p.canvas_height != cfg.height)
		return 1;
	if (p.pip_x != kDefPipX || p.pip_y != kDefPipY || p.pip_w != kDefPipW || p.pip_h != kDefPipH)
		return 1;
	if (p.pip_jpeg_quality != kDefPipJpegQ)
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
	if (merged.width != 1280 || merged.height != 720)
		return 1;
	if (check_default_pip_fields(merged))
		return 1;

	return 0;
}
