/** Grid layout keys under [libmy_uvc_pip]. */
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

int main()
{
	const fs::path root =
	    fs::temp_directory_path() / (std::string("uvc_pip_tile_ini_") + std::to_string(getpid()));
	fs::remove_all(root);
	fs::create_directories(root);

	const char *libpip = R"([libmy_uvc_pip]
pip_enable = 1
pip_overlay_path = /tmp/o.jpg
pip_tile_n_tiles = 4
pip_tile_gap_px = 3
pip_tile_margin_px = 5
)";
	if (write_file(root / "libmy_uvc_pip.ini", libpip))
		return 1;
	const char *libmy = R"([libmy_uvc]
channels = 1
width = 960
height = 540
video_codec = mjpeg
)";
	if (write_file(root / "libmy_uvc.ini", libmy))
		return 1;

	AppConfig cfg{};
	std::string err;
	if (!load_app_config(root.string(), &cfg, &err))
		return 1;
	if (cfg.libmy_uvc_pip.pip_tile_n_tiles != 4 || cfg.libmy_uvc_pip.pip_tile_gap_px != 3 ||
	    cfg.libmy_uvc_pip.pip_tile_margin_px != 5)
		return 1;

	fs::remove_all(root);
	return 0;
}
