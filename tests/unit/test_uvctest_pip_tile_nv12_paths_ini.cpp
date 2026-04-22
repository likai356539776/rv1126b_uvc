/** [uvctest] pip_tile_test_nv12_paths 解析。 */
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
	    fs::temp_directory_path() / (std::string("uvc_pip_nv12_paths_ini_") + std::to_string(getpid()));
	fs::remove_all(root);
	fs::create_directories(root);

	const char *libmy = R"([libmy_uvc]
channels = 1
width = 960
height = 540
video_codec = mjpeg
)";
	const char *libpip = R"([libmy_uvc_pip]
pip_enable = 1
pip_overlay_path = /tmp/o.jpg
)";
	const char *uvc = R"([uvctest]
pip_tile_test_nv12_paths = /data/a.nv12, /data/b.nv12
pip_tile_test_nv12_src_w = 640
pip_tile_test_nv12_src_h = 480
)";
	if (write_file(root / "libmy_uvc.ini", libmy))
		return 1;
	if (write_file(root / "libmy_uvc_pip.ini", libpip))
		return 1;
	if (write_file(root / "uvctest.ini", uvc))
		return 1;

	AppConfig cfg{};
	std::string err;
	if (!load_app_config(root.string(), &cfg, &err))
		return 1;
	if (cfg.uvctest.pip_tile_test_nv12_paths != "/data/a.nv12, /data/b.nv12")
		return 1;
	if (cfg.uvctest.pip_tile_test_nv12_src_w != 640 || cfg.uvctest.pip_tile_test_nv12_src_h != 480)
		return 1;

	fs::remove_all(root);
	return 0;
}
