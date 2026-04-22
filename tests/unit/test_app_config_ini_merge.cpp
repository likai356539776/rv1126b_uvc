/** P0-T2: 目录下多文件 ini 合并序列（libmy_uvc → pip → uvctest）与区段约束。 */
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

static bool test_three_section_merge()
{
	const fs::path root =
	    fs::temp_directory_path() / (std::string("uvc_cfg_p0t2_merge_") + std::to_string(getpid()));
	fs::remove_all(root);
	fs::create_directories(root);

	const char *libmy = R"([libmy_uvc]
channels = 2
width = 1280
height = 720
video_codec = h264
fps = 24
idle_sleep_ms = 15
)";
	const char *libpip = R"([libmy_uvc_pip]
pip_enable = 1
pip_x = 100
pip_y = 80
pip_w = 400
pip_h = 300
pip_jpeg_quality = 77
pip_overlay_path = /tmp/bg_p0t2.jpg
)";
	const char *uvctest = R"([uvctest]
h264_path = /data/stream/ch0.h264
log_every_frames = 77
stats_interval_sec = 12
)";
	if (write_file(root / "libmy_uvc.ini", libmy))
		return false;
	if (write_file(root / "libmy_uvc_pip.ini", libpip))
		return false;
	if (write_file(root / "uvctest.ini", uvctest))
		return false;

	AppConfig cfg{};
	std::string err;
	if (!load_app_config(root.string(), &cfg, &err))
		return false;

	if (cfg.libmy_uvc.channels != 2 || cfg.libmy_uvc.width != 1280 || cfg.libmy_uvc.height != 720)
		return false;
	if (cfg.libmy_uvc.fps != 24 || cfg.libmy_uvc.idle_sleep_ms != 15)
		return false;
	if (cfg.libmy_uvc.video_codec != "h264")
		return false;
	if (cfg.uvctest.h264_path != "/data/stream/ch0.h264" || cfg.uvctest.log_every_frames != 77)
		return false;
	if (cfg.uvctest.stats_interval_sec != 12)
		return false;
	if (!cfg.libmy_uvc_pip.pip_enable || cfg.libmy_uvc_pip.pip_x != 100 || cfg.libmy_uvc_pip.pip_y != 80 ||
	    cfg.libmy_uvc_pip.pip_w != 400 || cfg.libmy_uvc_pip.pip_h != 300)
		return false;
	if (cfg.libmy_uvc_pip.pip_jpeg_quality != 77 || cfg.libmy_uvc_pip.pip_overlay_path != "/tmp/bg_p0t2.jpg")
		return false;
	if (cfg.libmy_uvc_pip.pip_overlay_stale_timeout_ms != 5000)
		return false;
	if (cfg.libmy_uvc_pip.pip_tile_n_tiles != 0 || cfg.libmy_uvc_pip.pip_tile_gap_px != 0 ||
	    cfg.libmy_uvc_pip.pip_tile_margin_px != 0)
		return false;

	fs::remove_all(root);
	return true;
}

/** uvctest.ini 按合并顺序最后加载；其中 [libmy_uvc] 覆盖先验文件中的同键。 */
static bool test_later_file_overrides_same_section()
{
	const fs::path root = fs::temp_directory_path() /
	                      (std::string("uvc_cfg_p0t2_ovr_") + std::to_string(getpid()));
	fs::remove_all(root);
	fs::create_directories(root);

	const char *libmy = R"([libmy_uvc]
channels = 2
width = 640
height = 480
fps = 25
idle_sleep_ms = 10
video_codec = h264
)";
	const char *libpip = R"([libmy_uvc_pip]
pip_enable = 0
)";
	const char *uvctest = R"([libmy_uvc]
channels = 6
[uvctest]
h264_path = /override/path.h264
)";
	if (write_file(root / "libmy_uvc.ini", libmy))
		return false;
	if (write_file(root / "libmy_uvc_pip.ini", libpip))
		return false;
	if (write_file(root / "uvctest.ini", uvctest))
		return false;

	AppConfig cfg{};
	std::string err;
	if (!load_app_config(root.string(), &cfg, &err))
		return false;
	if (cfg.libmy_uvc.channels != 6)
		return false;
	if (cfg.uvctest.h264_path != "/override/path.h264")
		return false;
	if (cfg.libmy_uvc_pip.pip_overlay_stale_timeout_ms != 5000)
		return false;

	fs::remove_all(root);
	return true;
}

/** 目录模式下必须存在至少一个分文件；仅含旧 my_uvc.ini 时失败。 */
static bool test_directory_requires_split_ini()
{
	const fs::path root = fs::temp_directory_path() /
	                      (std::string("uvc_cfg_p0t2_nosplit_") + std::to_string(getpid()));
	fs::remove_all(root);
	fs::create_directories(root);

	const char *mono = R"([my_uvc]
channels = 1
width = 800
height = 600
)";
	if (write_file(root / "my_uvc.ini", mono))
		return false;

	AppConfig cfg{};
	std::string err;
	if (load_app_config(root.string(), &cfg, &err))
		return false;
	if (err.find("libmy_uvc.ini") == std::string::npos)
		return false;

	fs::remove_all(root);
	return true;
}

/** 分文件中 [libmy_uvc] 不允许 pip 专用键（区段校验）。 */
static bool test_reject_pip_key_in_libmy_uvc_section()
{
	const fs::path root = fs::temp_directory_path() /
	                      (std::string("uvc_cfg_p0t2_bad_") + std::to_string(getpid()));
	fs::remove_all(root);
	fs::create_directories(root);

	const char *bad = R"([libmy_uvc]
channels = 2
width = 640
height = 480
fps = 25
idle_sleep_ms = 10
pip_x = 1
)";
	if (write_file(root / "libmy_uvc.ini", bad))
		return false;

	AppConfig cfg{};
	std::string err;
	if (load_app_config(root.string(), &cfg, &err))
		return false;
	if (err.find("pip_x") == std::string::npos && err.find("libmy_uvc") == std::string::npos)
		return false;

	fs::remove_all(root);
	return true;
}

int main()
{
	if (!test_three_section_merge())
		return 1;
	if (!test_later_file_overrides_same_section())
		return 1;
	if (!test_directory_requires_split_ini())
		return 1;
	if (!test_reject_pip_key_in_libmy_uvc_section())
		return 1;
	return 0;
}
