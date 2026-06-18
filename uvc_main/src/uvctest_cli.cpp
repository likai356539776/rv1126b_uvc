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
				out->cli_cfg.libmy_uvc.video_codec = "h264";
			else if (c == "mjpeg" || c == "jpeg" || c == "jpg" || c == "mjpg")
				out->cli_cfg.libmy_uvc.video_codec = "mjpeg";
			else {
				std::fprintf(stderr, "[uvctest] invalid --codec (use h264 or mjpeg)\n");
				return CliParseResult::BadArg;
			}
			out->cli_codec = true;
		} else if (a == "--channels" && i + 1 < argc) {
			try {
				out->cli_cfg.libmy_uvc.channels = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_channels = true;
		} else if (a == "--file" && i + 1 < argc) {
			out->cli_cfg.uvctest.h264_path = argv[++i];
			out->cli_file = true;
		} else if (a == "--width" && i + 1 < argc) {
			try {
				out->cli_cfg.libmy_uvc.width = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_width = true;
		} else if (a == "--height" && i + 1 < argc) {
			try {
				out->cli_cfg.libmy_uvc.height = std::stoi(argv[++i]);
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
			out->cli_cfg.libmy_uvc.width = w;
			out->cli_cfg.libmy_uvc.height = h;
			out->cli_size = true;
		} else if (a == "--fps" && i + 1 < argc) {
			try {
				out->cli_cfg.libmy_uvc.fps = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_fps = true;
		} else if (a == "--log-every" && i + 1 < argc) {
			try {
				out->cli_cfg.uvctest.log_every_frames = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_log_every = true;
		} else if (a == "--log-level" && i + 1 < argc) {
			try {
				out->cli_cfg.libmy_uvc.log_level = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_log_level = true;
		} else if (a == "--stats-enable" && i + 1 < argc) {
			try {
				out->cli_cfg.uvctest.stats_enable = std::stoi(argv[++i]) != 0;
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_stats_enable = true;
		} else if (a == "--stats-interval" && i + 1 < argc) {
			try {
				out->cli_cfg.uvctest.stats_interval_sec = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_stats_interval = true;
		} else if (a == "--startup-prime-frames" && i + 1 < argc) {
			try {
				out->cli_cfg.libmy_uvc.startup_prime_frames = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_startup_prime_frames = true;
		} else if (a == "--pip-enable" && i + 1 < argc) {
			try {
				out->cli_cfg.libmy_uvc_pip.pip_enable = (std::stoi(argv[++i]) != 0);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_pip_enable = true;
		} else if (a == "--pip-overlay" && i + 1 < argc) {
			out->cli_cfg.libmy_uvc_pip.pip_overlay_path = argv[++i];
			out->cli_pip_overlay = true;
		} else if (a == "--pip-x" && i + 1 < argc) {
			try {
				out->cli_cfg.libmy_uvc_pip.pip_x = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_pip_x = true;
		} else if (a == "--pip-y" && i + 1 < argc) {
			try {
				out->cli_cfg.libmy_uvc_pip.pip_y = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_pip_y = true;
		} else if (a == "--pip-w" && i + 1 < argc) {
			try {
				out->cli_cfg.libmy_uvc_pip.pip_w = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_pip_w = true;
		} else if (a == "--pip-h" && i + 1 < argc) {
			try {
				out->cli_cfg.libmy_uvc_pip.pip_h = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_pip_h = true;
		} else if (a == "--pip-jpeg-quality" && i + 1 < argc) {
			try {
				out->cli_cfg.libmy_uvc_pip.pip_jpeg_quality = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_pip_quality = true;
		} else if (a == "--pip-tile-n-tiles" && i + 1 < argc) {
			try {
				out->cli_cfg.libmy_uvc_pip.pip_tile_n_tiles = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_pip_tile_n_tiles = true;
		} else if (a == "--pip-tile-test-nv12-paths" && i + 1 < argc) {
			out->cli_cfg.uvctest.pip_tile_test_nv12_paths = argv[++i];
			out->cli_pip_tile_test_nv12_paths = true;
		} else if (a == "--pip-tile-test-nv12-src-w" && i + 1 < argc) {
			try {
				out->cli_cfg.uvctest.pip_tile_test_nv12_src_w = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_pip_tile_test_nv12_src_w = true;
		} else if (a == "--pip-tile-test-nv12-src-h" && i + 1 < argc) {
			try {
				out->cli_cfg.uvctest.pip_tile_test_nv12_src_h = std::stoi(argv[++i]);
			} catch (...) {
				return CliParseResult::BadArg;
			}
			out->cli_pip_tile_test_nv12_src_h = true;
		} else if (a == "--yolo-model" && i + 1 < argc) {
			out->yolo_model = argv[++i];
			out->cli_yolo_model = true;
		} else if (a == "--yolo-labels" && i + 1 < argc) {
			out->yolo_labels = argv[++i];
			out->cli_yolo_labels = true;
		} else if (a == "--camera-node" && i + 1 < argc) {
			out->camera_node = argv[++i];
			out->cli_camera_node = true;
		} else if (a == "--camera-type" && i + 1 < argc) {
			out->camera_type = argv[++i];
			out->cli_camera_type = true;
		} else if (a == "--camera-size" && i + 1 < argc) {
			int w = 0;
			int h = 0;
			std::string size = argv[++i];
			if (std::sscanf(size.c_str(), "%dx%d", &w, &h) != 2 || w < 0 || h < 0) {
				std::fprintf(stderr, "[uvctest] invalid --camera-size: %s (expected WxH)\n", size.c_str());
				return CliParseResult::BadArg;
			}
			out->cli_cfg.uvctest.camera_width = w;
			out->cli_cfg.uvctest.camera_height = h;
			out->cli_camera_size = true;
		} else if (a == "--yolo-score-threshold" && i + 1 < argc) {
			try {
				float val = std::stof(argv[++i]);
				if (val > 1.0f) {
					val /= 100.0f;
				}
				out->yolo_score_threshold = val;
				out->cli_yolo_score_threshold = true;
			} catch (...) {
				return CliParseResult::BadArg;
			}
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
		cfg->uvctest.h264_path = cli.cli_cfg.uvctest.h264_path;
		for (int i = 0; i < kMaxUvcChannels; i++)
			cfg->uvctest.channel_h264_path[i] = cfg->uvctest.h264_path;
	}
	if (cli.cli_channels)
		cfg->libmy_uvc.channels = cli.cli_cfg.libmy_uvc.channels;
	if (cli.cli_width)
		cfg->libmy_uvc.width = cli.cli_cfg.libmy_uvc.width;
	if (cli.cli_height)
		cfg->libmy_uvc.height = cli.cli_cfg.libmy_uvc.height;
	if (cli.cli_size) {
		cfg->libmy_uvc.width = cli.cli_cfg.libmy_uvc.width;
		cfg->libmy_uvc.height = cli.cli_cfg.libmy_uvc.height;
	}
	if (cli.cli_fps)
		cfg->libmy_uvc.fps = cli.cli_cfg.libmy_uvc.fps;
	if (cli.cli_log_every)
		cfg->uvctest.log_every_frames = cli.cli_cfg.uvctest.log_every_frames;
	if (cli.cli_log_level)
		cfg->libmy_uvc.log_level = cli.cli_cfg.libmy_uvc.log_level;
	if (cli.cli_stats_enable)
		cfg->uvctest.stats_enable = cli.cli_cfg.uvctest.stats_enable;
	if (cli.cli_stats_interval)
		cfg->uvctest.stats_interval_sec = cli.cli_cfg.uvctest.stats_interval_sec;
	if (cli.cli_startup_prime_frames)
		cfg->libmy_uvc.startup_prime_frames = cli.cli_cfg.libmy_uvc.startup_prime_frames;
	if (cli.cli_codec)
		cfg->libmy_uvc.video_codec = cli.cli_cfg.libmy_uvc.video_codec;
	if (cli.cli_pip_overlay)
		cfg->libmy_uvc_pip.pip_overlay_path = cli.cli_cfg.libmy_uvc_pip.pip_overlay_path;
	/* --pip-overlay alone turns PiP on; explicit --pip-enable (including 0) must win (see merge order below). */
	if (cli.cli_pip_overlay && !cli.cli_pip_enable)
		cfg->libmy_uvc_pip.pip_enable = true;
	if (cli.cli_pip_enable)
		cfg->libmy_uvc_pip.pip_enable = cli.cli_cfg.libmy_uvc_pip.pip_enable;
	if (cli.cli_pip_x)
		cfg->libmy_uvc_pip.pip_x = cli.cli_cfg.libmy_uvc_pip.pip_x;
	if (cli.cli_pip_y)
		cfg->libmy_uvc_pip.pip_y = cli.cli_cfg.libmy_uvc_pip.pip_y;
	if (cli.cli_pip_w)
		cfg->libmy_uvc_pip.pip_w = cli.cli_cfg.libmy_uvc_pip.pip_w;
	if (cli.cli_pip_h)
		cfg->libmy_uvc_pip.pip_h = cli.cli_cfg.libmy_uvc_pip.pip_h;
	if (cli.cli_pip_quality)
		cfg->libmy_uvc_pip.pip_jpeg_quality = cli.cli_cfg.libmy_uvc_pip.pip_jpeg_quality;
	if (cli.cli_pip_tile_n_tiles)
		cfg->libmy_uvc_pip.pip_tile_n_tiles = cli.cli_cfg.libmy_uvc_pip.pip_tile_n_tiles;
	if (cli.cli_pip_tile_test_nv12_paths)
		cfg->uvctest.pip_tile_test_nv12_paths = cli.cli_cfg.uvctest.pip_tile_test_nv12_paths;
	if (cli.cli_pip_tile_test_nv12_src_w)
		cfg->uvctest.pip_tile_test_nv12_src_w = cli.cli_cfg.uvctest.pip_tile_test_nv12_src_w;
	if (cli.cli_pip_tile_test_nv12_src_h)
		cfg->uvctest.pip_tile_test_nv12_src_h = cli.cli_cfg.uvctest.pip_tile_test_nv12_src_h;
	if (cli.cli_yolo_score_threshold)
		cfg->uvctest.yolo_score_threshold = cli.yolo_score_threshold;
	if (cli.cli_camera_node)
		cfg->uvctest.camera_node = cli.camera_node;
	if (cli.cli_camera_type)
		cfg->uvctest.camera_type = cli.camera_type;
	if (cli.cli_camera_size) {
		cfg->uvctest.camera_width = cli.cli_cfg.uvctest.camera_width;
		cfg->uvctest.camera_height = cli.cli_cfg.uvctest.camera_height;
	}
}

bool validate_config(AppConfig *cfg, std::string *err)
{
	if (!cfg) {
		if (err)
			*err = "cfg is null";
		return false;
	}
	if (cfg->libmy_uvc.video_codec != "h264" && cfg->libmy_uvc.video_codec != "mjpeg") {
		if (err)
			*err = "[uvctest] invalid video_codec in config (use h264 or mjpeg)";
		return false;
	}
	if (cfg->libmy_uvc.channels < 1)
		cfg->libmy_uvc.channels = 1;
	if (cfg->libmy_uvc.channels > kMaxUvcChannels)
		cfg->libmy_uvc.channels = kMaxUvcChannels;
	if (cfg->libmy_uvc.log_level < 0)
		cfg->libmy_uvc.log_level = 0;
	if (cfg->libmy_uvc.log_level > 4)
		cfg->libmy_uvc.log_level = 4;
	if (cfg->uvctest.stats_interval_sec < 1)
		cfg->uvctest.stats_interval_sec = 1;
	if (cfg->libmy_uvc.startup_prime_frames < 0)
		cfg->libmy_uvc.startup_prime_frames = 0;
	if (cfg->libmy_uvc.startup_prime_frames > 120)
		cfg->libmy_uvc.startup_prime_frames = 120;
	if (cfg->libmy_uvc_pip.pip_jpeg_quality < 1)
		cfg->libmy_uvc_pip.pip_jpeg_quality = 1;
	if (cfg->libmy_uvc_pip.pip_jpeg_quality > 100)
		cfg->libmy_uvc_pip.pip_jpeg_quality = 100;

	if (cfg->libmy_uvc_pip.pip_tile_n_tiles > 0) {
		if (cfg->libmy_uvc_pip.pip_tile_n_tiles < 4)
			cfg->libmy_uvc_pip.pip_tile_n_tiles = 4;
		else if (cfg->libmy_uvc_pip.pip_tile_n_tiles > 16)
			cfg->libmy_uvc_pip.pip_tile_n_tiles = 16;
	}

	if (cfg->libmy_uvc_pip.pip_enable) {
		if (cfg->libmy_uvc.video_codec != "mjpeg") {
			if (err)
				*err = "[uvctest] pip_enable requires video_codec=mjpeg";
			return false;
		}
		if (cfg->libmy_uvc_pip.pip_overlay_path.empty()) {
			if (err)
				*err = "[uvctest] pip_enable requires pip_overlay_path (ini or --pip-overlay)";
			return false;
		}
		// pip_w and pip_h can now be 0 to trigger adaptive logic in pip_helper
	}

	if (cfg->uvctest.yolo_score_threshold < 0.0f || cfg->uvctest.yolo_score_threshold > 1.0f) {
		if (err)
			*err = "[uvctest] yolo_score_threshold must be in range [0.0, 1.0] (or [0, 100])";
		return false;
	}

	if (cfg->uvctest.camera_type != "rockit" && cfg->uvctest.camera_type != "v4l2") {
		if (err)
			*err = "[uvctest] camera_type must be 'rockit' or 'v4l2'";
		return false;
	}
	return true;
}

} // namespace uvctest

