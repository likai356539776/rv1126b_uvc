/** P4-T1: load_app_config_section_from_file — sequential section merges vs single-file load_app_config. */
#include "app_config.h"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static int write_file(const fs::path &p, const char *text)
{
	std::ofstream o(p);
	if (!o)
		return 1;
	o << text;
	return o.good() ? 0 : 1;
}

/** Same logic as finalize_channel_defaults in app_config.cpp (for parity after section-only loads). */
static void finalize_like_load_app_config(AppConfig *cfg)
{
	for (int i = 0; i < kMaxUvcChannels; i++) {
		if (cfg->uvctest.channel_h264_path[i].empty())
			cfg->uvctest.channel_h264_path[i] = cfg->uvctest.h264_path;
		if (cfg->uvctest.channel_fps[i] <= 0)
			cfg->uvctest.channel_fps[i] = cfg->libmy_uvc.fps;
	}
}

static int cmp_merge_vs_full(const AppConfig &a, const AppConfig &b)
{
	if (a.libmy_uvc.channels != b.libmy_uvc.channels || a.libmy_uvc.width != b.libmy_uvc.width ||
	    a.libmy_uvc.height != b.libmy_uvc.height || a.libmy_uvc.fps != b.libmy_uvc.fps)
		return 1;
	if (a.libmy_uvc.video_codec != b.libmy_uvc.video_codec)
		return 1;
	if (a.uvctest.h264_path != b.uvctest.h264_path || a.uvctest.log_every_frames != b.uvctest.log_every_frames)
		return 1;
	if (a.libmy_uvc_pip.pip_enable != b.libmy_uvc_pip.pip_enable || a.libmy_uvc_pip.pip_x != b.libmy_uvc_pip.pip_x ||
	    a.libmy_uvc_pip.pip_y != b.libmy_uvc_pip.pip_y || a.libmy_uvc_pip.pip_w != b.libmy_uvc_pip.pip_w ||
	    a.libmy_uvc_pip.pip_h != b.libmy_uvc_pip.pip_h || a.libmy_uvc_pip.pip_jpeg_quality != b.libmy_uvc_pip.pip_jpeg_quality)
		return 1;
	if (a.libmy_uvc_pip.pip_overlay_stale_timeout_ms != b.libmy_uvc_pip.pip_overlay_stale_timeout_ms)
		return 1;
	if (a.libmy_uvc_pip.pip_tile_n_tiles != b.libmy_uvc_pip.pip_tile_n_tiles ||
	    a.libmy_uvc_pip.pip_tile_gap_px != b.libmy_uvc_pip.pip_tile_gap_px ||
	    a.libmy_uvc_pip.pip_tile_margin_px != b.libmy_uvc_pip.pip_tile_margin_px)
		return 1;
	if (a.libmy_uvc_pip.pip_overlay_path != b.libmy_uvc_pip.pip_overlay_path)
		return 1;
	for (int i = 0; i < kMaxUvcChannels; i++) {
		if (a.uvctest.channel_fps[i] != b.uvctest.channel_fps[i])
			return 1;
		if (a.uvctest.channel_h264_path[i] != b.uvctest.channel_h264_path[i])
			return 1;
	}
	return 0;
}

int main()
{
	const fs::path root =
	    fs::temp_directory_path() / (std::string("uvctest_ini_sec_p4t1_") + std::to_string(getpid()));
	fs::remove_all(root);
	fs::create_directories(root);

	const char *merged = R"([libmy_uvc]
channels = 2
width = 800
height = 600
fps = 30
video_codec = mjpeg
[libmy_uvc_pip]
pip_enable = 1
pip_x = 1
pip_y = 2
pip_w = 320
pip_h = 240
pip_jpeg_quality = 90
pip_overlay_path = /tmp/o.jpg
pip_overlay_stale_timeout_ms = 2000
[uvctest]
h264_path = /media/x.h264
log_every_frames = 99
)";
	if (write_file(root / "all.ini", merged))
		return 1;

	AppConfig full{};
	std::string err;
	if (!load_app_config((root / "all.ini").string(), &full, &err))
		return 1;

	AppConfig piece = default_app_config();
	if (!load_app_config_section_from_file((root / "all.ini").string(), "libmy_uvc", &piece, &err))
		return 1;
	if (!load_app_config_section_from_file((root / "all.ini").string(), "libmy_uvc_pip", &piece, &err))
		return 1;
	if (!load_app_config_section_from_file((root / "all.ini").string(), "uvctest", &piece, &err))
		return 1;
	finalize_like_load_app_config(&piece);

	if (cmp_merge_vs_full(full, piece))
		return 1;

	/* Unknown section name */
	if (load_app_config_section_from_file((root / "all.ini").string(), "not_a_section", &piece, &err))
		return 1;

	std::error_code ec;
	fs::remove_all(root, ec);
	(void)ec;
	return 0;
}
