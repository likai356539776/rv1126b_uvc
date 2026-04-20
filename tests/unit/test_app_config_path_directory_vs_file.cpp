/** P2-U2: directory merge vs single legacy file — same effective AppConfig. */
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

static int cmp_key_fields(const AppConfig &a, const AppConfig &b)
{
	if (a.channels != b.channels || a.width != b.width || a.height != b.height || a.fps != b.fps)
		return 1;
	if (a.video_codec != b.video_codec)
		return 1;
	if (a.h264_path != b.h264_path)
		return 1;
	if (a.idle_sleep_ms != b.idle_sleep_ms)
		return 1;
	if (a.log_every_frames != b.log_every_frames)
		return 1;
	if (a.pip_enable != b.pip_enable || a.pip_x != b.pip_x || a.pip_y != b.pip_y || a.pip_w != b.pip_w ||
	    a.pip_h != b.pip_h || a.pip_jpeg_quality != b.pip_jpeg_quality)
		return 1;
	if (a.pip_overlay_path != b.pip_overlay_path)
		return 1;
	return 0;
}

int main()
{
	const fs::path root = fs::temp_directory_path() / (std::string("uvctest_cfg_p2u2_") + std::to_string(getpid()));
	fs::remove_all(root);
	fs::create_directories(root);

	const char *libmy = R"([libmy_uvc]
channels = 3
width = 960
height = 540
video_codec = h264
fps = 20
idle_sleep_ms = 7
)";
	const char *libpip = R"([libmy_uvc_pip]
pip_enable = 0
pip_x = 10
pip_y = 20
pip_w = 320
pip_h = 240
pip_jpeg_quality = 90
pip_overlay_path = /tmp/overlay.jpg
)";
	const char *uvctest = R"([uvctest]
h264_path = /media/clips/ch0.h264
log_every_frames = 100
)";
	if (write_file(root / "libmy_uvc.ini", libmy))
		return 1;
	if (write_file(root / "libmy_uvc_pip.ini", libpip))
		return 1;
	if (write_file(root / "uvctest.ini", uvctest))
		return 1;

	const char *mono = R"([my_uvc]
channels = 3
width = 960
height = 540
video_codec = h264
fps = 20
idle_sleep_ms = 7
h264_path = /media/clips/ch0.h264
log_every_frames = 100
pip_enable = 0
pip_x = 10
pip_y = 20
pip_w = 320
pip_h = 240
pip_jpeg_quality = 90
pip_overlay_path = /tmp/overlay.jpg
)";
	if (write_file(root / "merged.ini", mono))
		return 1;

	AppConfig from_dir{};
	std::string err;
	if (!load_app_config(root.string(), &from_dir, &err))
		return 1;

	AppConfig from_file{};
	if (!load_app_config((root / "merged.ini").string(), &from_file, &err))
		return 1;

	if (cmp_key_fields(from_dir, from_file) != 0)
		return 1;

	fs::remove_all(root);
	return 0;
}
