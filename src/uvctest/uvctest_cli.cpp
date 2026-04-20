#include "uvctest/uvctest_cli.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace uvctest {

CliParseResult parse_cli(int argc, char **argv, CliState *out)
{
	if (!out)
		return CliParseResult::BadArg;
	*out = CliState{};
	out->cli_cfg = default_app_config();

	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "-c" && i + 1 < argc) {
			out->config_path = argv[++i];
		} else if (a == "--codec" && i + 1 < argc) {
			std::string c = argv[++i];
			for (auto &x : c)
				x = static_cast<char>(std::tolower(static_cast<unsigned char>(x)));
			if (c == "h264" || c == "264" || c == "avc")
				out->cli_cfg.video_codec = "h264";
			else if (c == "mjpeg" || c == "jpeg" || c == "jpg" || c == "mjpg")
				out->cli_cfg.video_codec = "mjpeg";
			else {
				std::fprintf(stderr, "[uvctest] invalid --codec (use h264 or mjpeg)\n");
				return CliParseResult::BadArg;
			}
			out->cli_codec = true;
		} else if (a == "--channels" && i + 1 < argc) {
			try {
				out->cli_cfg.channels = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_channels = true;
		} else if (a == "--file" && i + 1 < argc) {
			out->cli_cfg.h264_path = argv[++i];
			out->cli_file = true;
		} else if (a == "--width" && i + 1 < argc) {
			try {
				out->cli_cfg.width = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_width = true;
		} else if (a == "--height" && i + 1 < argc) {
			try {
				out->cli_cfg.height = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_height = true;
		} else if (a == "--size" && i + 1 < argc) {
			int w = 0;
			int h = 0;
			std::string size = argv[++i];
			if (std::sscanf(size.c_str(), "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) {
				std::fprintf(stderr, "[uvctest] invalid --size: %s (expected WxH)\n", size.c_str());
				return CliParseResult::BadArg;
			}
			out->cli_cfg.width = w;
			out->cli_cfg.height = h;
			out->cli_size = true;
		} else if (a == "--fps" && i + 1 < argc) {
			try {
				out->cli_cfg.fps = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_fps = true;
		} else if (a == "--log-every" && i + 1 < argc) {
			try {
				out->cli_cfg.log_every_frames = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_log_every = true;
		} else if (a == "--log-level" && i + 1 < argc) {
			try {
				out->cli_cfg.log_level = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_log_level = true;
		} else if (a == "--stats-enable" && i + 1 < argc) {
			try {
				out->cli_cfg.stats_enable = std::stoi(argv[++i]) != 0;
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_stats_enable = true;
		} else if (a == "--stats-interval" && i + 1 < argc) {
			try {
				out->cli_cfg.stats_interval_sec = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_stats_interval = true;
		} else if (a == "--startup-prime-frames" && i + 1 < argc) {
			try {
				out->cli_cfg.startup_prime_frames = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_startup_prime_frames = true;
		} else if (a == "--pip-enable" && i + 1 < argc) {
			try {
				out->cli_cfg.pip_enable = (std::stoi(argv[++i]) != 0);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_pip_enable = true;
		} else if (a == "--pip-overlay" && i + 1 < argc) {
			out->cli_cfg.pip_overlay_path = argv[++i];
			out->cli_pip_overlay = true;
		} else if (a == "--pip-x" && i + 1 < argc) {
			try {
				out->cli_cfg.pip_x = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_pip_x = true;
		} else if (a == "--pip-y" && i + 1 < argc) {
			try {
				out->cli_cfg.pip_y = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_pip_y = true;
		} else if (a == "--pip-w" && i + 1 < argc) {
			try {
				out->cli_cfg.pip_w = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_pip_w = true;
		} else if (a == "--pip-h" && i + 1 < argc) {
			try {
				out->cli_cfg.pip_h = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_pip_h = true;
		} else if (a == "--pip-jpeg-quality" && i + 1 < argc) {
			try {
				out->cli_cfg.pip_jpeg_quality = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_pip_quality = true;
		} else if (a == "-h" || a == "--help") {
			return CliParseResult::Help;
		} else {
			return CliParseResult::BadArg;
		}
	}
	return CliParseResult::Ok;
}

void merge_cli_into_config(AppConfig *cfg, const CliState &cli)
{
	if (!cfg)
		return;
	if (cli.cli_file) {
		cfg->h264_path = cli.cli_cfg.h264_path;
		for (int i = 0; i < kMaxUvcChannels; i++)
			cfg->channel_h264_path[i] = cfg->h264_path;
	}
	if (cli.cli_channels)
		cfg->channels = cli.cli_cfg.channels;
	if (cli.cli_width)
		cfg->width = cli.cli_cfg.width;
	if (cli.cli_height)
		cfg->height = cli.cli_cfg.height;
	if (cli.cli_size) {
		cfg->width = cli.cli_cfg.width;
		cfg->height = cli.cli_cfg.height;
	}
	if (cli.cli_fps)
		cfg->fps = cli.cli_cfg.fps;
	if (cli.cli_log_every)
		cfg->log_every_frames = cli.cli_cfg.log_every_frames;
	if (cli.cli_log_level)
		cfg->log_level = cli.cli_cfg.log_level;
	if (cli.cli_stats_enable)
		cfg->stats_enable = cli.cli_cfg.stats_enable;
	if (cli.cli_stats_interval)
		cfg->stats_interval_sec = cli.cli_cfg.stats_interval_sec;
	if (cli.cli_startup_prime_frames)
		cfg->startup_prime_frames = cli.cli_cfg.startup_prime_frames;
	if (cli.cli_codec)
		cfg->video_codec = cli.cli_cfg.video_codec;
	if (cli.cli_pip_overlay)
		cfg->pip_overlay_path = cli.cli_cfg.pip_overlay_path;
	/* --pip-overlay alone turns PiP on; explicit --pip-enable (including 0) must win (see merge order below). */
	if (cli.cli_pip_overlay && !cli.cli_pip_enable)
		cfg->pip_enable = true;
	if (cli.cli_pip_enable)
		cfg->pip_enable = cli.cli_cfg.pip_enable;
	if (cli.cli_pip_x)
		cfg->pip_x = cli.cli_cfg.pip_x;
	if (cli.cli_pip_y)
		cfg->pip_y = cli.cli_cfg.pip_y;
	if (cli.cli_pip_w)
		cfg->pip_w = cli.cli_cfg.pip_w;
	if (cli.cli_pip_h)
		cfg->pip_h = cli.cli_cfg.pip_h;
	if (cli.cli_pip_quality)
		cfg->pip_jpeg_quality = cli.cli_cfg.pip_jpeg_quality;
}

bool validate_config(AppConfig *cfg, std::string *err)
{
	if (!cfg) {
		if (err)
			*err = "cfg is null";
		return false;
	}
	if (cfg->video_codec != "h264" && cfg->video_codec != "mjpeg") {
		if (err)
			*err = "[uvctest] invalid video_codec in config (use h264 or mjpeg)";
		return false;
	}
	if (cfg->channels < 1)
		cfg->channels = 1;
	if (cfg->channels > kMaxUvcChannels)
		cfg->channels = kMaxUvcChannels;
	if (cfg->log_level < 0)
		cfg->log_level = 0;
	if (cfg->log_level > 2)
		cfg->log_level = 2;
	if (cfg->stats_interval_sec < 1)
		cfg->stats_interval_sec = 1;
	if (cfg->startup_prime_frames < 0)
		cfg->startup_prime_frames = 0;
	if (cfg->startup_prime_frames > 120)
		cfg->startup_prime_frames = 120;
	if (cfg->pip_jpeg_quality < 1)
		cfg->pip_jpeg_quality = 1;
	if (cfg->pip_jpeg_quality > 100)
		cfg->pip_jpeg_quality = 100;

	if (cfg->pip_enable) {
		if (cfg->video_codec != "mjpeg") {
			if (err)
				*err = "[uvctest] pip_enable requires video_codec=mjpeg";
			return false;
		}
		if (cfg->pip_overlay_path.empty()) {
			if (err)
				*err = "[uvctest] pip_enable requires pip_overlay_path (ini or --pip-overlay)";
			return false;
		}
		if (cfg->pip_w <= 0 || cfg->pip_h <= 0) {
			if (err)
				*err = "[uvctest] pip_w and pip_h must be positive";
			return false;
		}
	}
	return true;
}

} // namespace uvctest
