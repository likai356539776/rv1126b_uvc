/**
 * C API my_uvc_load_ini_section_only — same mapping as uvctest bridge for [libmy_uvc].
 */
#include "my_uvc/my_uvc.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static int dummy_open(void *, int, int, int, int) { return 0; }
static void dummy_close(void *) {}

int main()
{
	const fs::path root = fs::temp_directory_path() / "my_uvc_c_api_test";
	fs::create_directories(root);
	const std::string ini = (root / "one.ini").string();
	{
		std::ofstream o(ini);
		o << "[libmy_uvc]\n";
		o << "channels = 3\n";
		o << "width = 1280\n";
		o << "height = 720\n";
		o << "video_codec = mjpeg\n";
	}

	my_uvc_config_t cfg{};
	char err[256]{};
	my_uvc_err_t e = my_uvc_load_ini_section_only(ini.c_str(), "libmy_uvc", &cfg, dummy_open, dummy_close,
	                                              reinterpret_cast<void *>(0x42), err, sizeof(err));
	if (e != MY_UVC_OK)
		return 1;
	if (cfg.channels != 3 || cfg.width != 1280 || cfg.height != 720 || cfg.is_mjpeg != 1)
		return 1;
	if (cfg.on_open != dummy_open || cfg.on_close != dummy_close || cfg.user_data != reinterpret_cast<void *>(0x42))
		return 1;

	e = my_uvc_load_ini_section_only(ini.c_str(), "no_such_section_xyz", &cfg, nullptr, nullptr, nullptr, err,
	                                 sizeof(err));
	if (e == MY_UVC_OK)
		return 1;

	std::error_code ec;
	fs::remove_all(root, ec);
	return 0;
}
