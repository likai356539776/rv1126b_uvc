/** P3-U5: [libmy_uvc_pip] pip_overlay_stale_timeout_ms 解析与缺省。 */
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
	    fs::temp_directory_path() / (std::string("uvc_pip_stale_ini_") + std::to_string(getpid()));
	fs::remove_all(root);
	fs::create_directories(root);

	const char *libmy = R"([libmy_uvc]
channels = 1
width = 640
height = 480
video_codec = mjpeg
)";
	/* 未写 stale 键 → 保持 default_app_config 5000 */
	const char *libpip_min = R"([libmy_uvc_pip]
pip_enable = 0
)";
	const char *uvctest = R"([uvctest]
h264_path = /x.h264
)";
	if (write_file(root / "libmy_uvc.ini", libmy))
		return 1;
	if (write_file(root / "libmy_uvc_pip.ini", libpip_min))
		return 1;
	if (write_file(root / "uvctest.ini", uvctest))
		return 1;

	AppConfig cfg{};
	std::string err;
	if (!load_app_config(root.string(), &cfg, &err))
		return 1;
	if (cfg.libmy_uvc_pip.pip_overlay_stale_timeout_ms != 5000)
		return 1;

	const char *libpip0 = R"([libmy_uvc_pip]
pip_enable = 0
pip_overlay_stale_timeout_ms = 0
)";
	if (write_file(root / "libmy_uvc_pip.ini", libpip0))
		return 1;
	if (!load_app_config(root.string(), &cfg, &err))
		return 1;
	if (cfg.libmy_uvc_pip.pip_overlay_stale_timeout_ms != 0)
		return 1;

	const char *libpip250 = R"([libmy_uvc_pip]
pip_enable = 0
pip_overlay_stale_timeout_ms = 250
)";
	if (write_file(root / "libmy_uvc_pip.ini", libpip250))
		return 1;
	if (!load_app_config(root.string(), &cfg, &err))
		return 1;
	if (cfg.libmy_uvc_pip.pip_overlay_stale_timeout_ms != 250)
		return 1;

	fs::remove_all(root);
	return 0;
}
