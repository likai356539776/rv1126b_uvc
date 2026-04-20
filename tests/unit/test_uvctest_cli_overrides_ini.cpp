/** P2-U1: CLI overrides merged ini (§3.5). */
#include "app_config.h"
#include "uvctest/uvctest_cli.hpp"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

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
	    fs::temp_directory_path() / (std::string("uvctest_cli_p2u1_") + std::to_string(getpid()));
	fs::remove_all(root);
	fs::create_directories(root);
	const std::string dir = root.string();

	const char *libmy = R"([libmy_uvc]
channels = 1
width = 1920
height = 1080
video_codec = h264
fps = 25
)";
	if (write_file(root / "libmy_uvc.ini", libmy))
		return 1;

	std::vector<std::string> args = {"uvctest", "-c", dir, "--channels", "4", "--codec", "mjpeg"};
	std::vector<char *> av;
	for (auto &s : args)
		av.push_back(s.data());

	uvctest::CliState cli{};
	if (uvctest::parse_cli(static_cast<int>(av.size()), av.data(), &cli) != uvctest::CliParseResult::Ok)
		return 1;

	AppConfig cfg = default_app_config();
	std::string err;
	if (!load_app_config(cli.config_path, &cfg, &err))
		return 1;

	uvctest::merge_cli_into_config(&cfg, cli);
	if (!uvctest::validate_config(&cfg, &err))
		return 1;

	if (cfg.channels != 4)
		return 1;
	if (cfg.video_codec != "mjpeg")
		return 1;
	if (cfg.width != 1920 || cfg.height != 1080)
		return 1;

	std::error_code ec;
	fs::remove_all(root, ec);
	(void)ec;

	/* --pip-enable 0 must not be overridden by --pip-overlay (P2-U1 regression). */
	{
		std::vector<std::string> args = {"uvctest", "-c",         "/userdata",
		                                   "--codec",   "mjpeg",
		                                   "--pip-enable", "0",
		                                   "--pip-overlay", "/userdata/mjpeg_overlay"};
		std::vector<char *> av;
		for (auto &s : args)
			av.push_back(s.data());
		uvctest::CliState cli{};
		if (uvctest::parse_cli(static_cast<int>(av.size()), av.data(), &cli) != uvctest::CliParseResult::Ok)
			return 2;
		if (cli.cli_cfg.pip_enable)
			return 3;
		AppConfig cfg = default_app_config();
		cfg.pip_enable = true;
		cfg.pip_overlay_path = "/userdata/pip_logo.jpg";
		uvctest::merge_cli_into_config(&cfg, cli);
		if (cfg.pip_enable)
			return 4;
		std::string verr;
		if (!uvctest::validate_config(&cfg, &verr))
			return 5;
	}
	return 0;
}
