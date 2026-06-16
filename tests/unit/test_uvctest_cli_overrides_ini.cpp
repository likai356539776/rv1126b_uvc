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

	if (cfg.libmy_uvc.channels != 4)
		return 1;
	if (cfg.libmy_uvc.video_codec != "mjpeg")
		return 1;
	if (cfg.libmy_uvc.width != 1920 || cfg.libmy_uvc.height != 1080)
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
		if (cli.cli_cfg.libmy_uvc_pip.pip_enable)
			return 3;
		AppConfig cfg = default_app_config();
		cfg.libmy_uvc_pip.pip_enable = true;
		cfg.libmy_uvc_pip.pip_overlay_path = "/userdata/pip_logo.jpg";
		uvctest::merge_cli_into_config(&cfg, cli);
		if (cfg.libmy_uvc_pip.pip_enable)
			return 4;
		std::string verr;
		if (!uvctest::validate_config(&cfg, &verr))
			return 5;
	}

	/* Test new grid tile flags. */
	{
		std::vector<std::string> args = {"uvctest",
		                                   "--pip-tile-n-tiles",
		                                   "4",
		                                   "--pip-tile-test-nv12-paths",
		                                   "/tmp/a.nv12,/tmp/b.nv12",
		                                   "--pip-tile-test-nv12-src-w",
		                                   "320",
		                                   "--pip-tile-test-nv12-src-h",
		                                   "240"};
		std::vector<char *> av;
		for (auto &s : args)
			av.push_back(s.data());
		uvctest::CliState cli{};
		if (uvctest::parse_cli(static_cast<int>(av.size()), av.data(), &cli) != uvctest::CliParseResult::Ok)
			return 6;

		AppConfig cfg = default_app_config();
		uvctest::merge_cli_into_config(&cfg, cli);

		if (cfg.libmy_uvc_pip.pip_tile_n_tiles != 4)
			return 7;
		if (cfg.uvctest.pip_tile_test_nv12_paths != "/tmp/a.nv12,/tmp/b.nv12")
			return 8;
		if (cfg.uvctest.pip_tile_test_nv12_src_w != 320)
			return 9;
		if (cfg.uvctest.pip_tile_test_nv12_src_h != 240)
			return 10;
	}

	/* Test yolo_score_threshold flags. */
	{
		std::vector<std::string> args = {"uvctest", "--yolo-score-threshold", "75"};
		std::vector<char *> av;
		for (auto &s : args)
			av.push_back(s.data());
		uvctest::CliState cli{};
		if (uvctest::parse_cli(static_cast<int>(av.size()), av.data(), &cli) != uvctest::CliParseResult::Ok)
			return 11;

		AppConfig cfg = default_app_config();
		uvctest::merge_cli_into_config(&cfg, cli);

		if (std::abs(cfg.uvctest.yolo_score_threshold - 0.75f) > 0.0001f)
			return 12;
	}

	{
		std::vector<std::string> args = {"uvctest", "--yolo-score-threshold", "0.45"};
		std::vector<char *> av;
		for (auto &s : args)
			av.push_back(s.data());
		uvctest::CliState cli{};
		if (uvctest::parse_cli(static_cast<int>(av.size()), av.data(), &cli) != uvctest::CliParseResult::Ok)
			return 13;

		AppConfig cfg = default_app_config();
		uvctest::merge_cli_into_config(&cfg, cli);

		if (std::abs(cfg.uvctest.yolo_score_threshold - 0.45f) > 0.0001f)
			return 14;
	}

	return 0;
}
