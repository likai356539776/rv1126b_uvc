#include "app_config.h"
#include "app_log.h"
#include "uvctest/uvctest_cli.hpp"
#include "pipeline_coordinator.h"

#include <csignal>
#include <cstdio>
#include <atomic>

namespace {
std::atomic<bool> g_shutdown_requested{false};

void on_signal(int signo) {
	(void)signo;
	g_shutdown_requested.store(true);
}

void print_usage(const char *argv0) {
	std::fprintf(stderr,
	             "Usage: %s [-c dir|file] [--codec mjpeg] [--channels n] [--width w] "
	             "[--height h] [--fps fps] "
	             "[--size WxH] [--camera-size WxH] "
	             "[--pip-enable 0|1] [--pip-x n] [--pip-y n] [--pip-w n] [--pip-h n] "
	             "[--pip-jpeg-quality 1-100] "
	             "[--log-every n] [--log-level 0|1|2] [--stats-enable 0|1] "
	             "[--stats-interval sec]\n"
	             "  Board/install binary name: `uvctest`.\n",
	             argv0);
}
} // namespace

int main(int argc, char **argv) {
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	std::signal(SIGINT, on_signal);
	std::signal(SIGTERM, on_signal);

	uvctest::CliState cli{};
	const uvctest::CliParseResult pr = uvctest::parse_cli(argc, argv, &cli);
	if (pr == uvctest::CliParseResult::Help) {
		print_usage(argv[0]);
		return 0;
	}
	if (pr == uvctest::CliParseResult::BadArg) {
		print_usage(argv[0]);
		return 1;
	}

	AppConfig cfg = default_app_config();
	std::string err;
	if (!load_app_config(cli.config_path, &cfg, &err))
		std::fprintf(stderr, "[uvctest] warning: %s, fallback to defaults\n", err.c_str());

	uvctest::merge_cli_into_config(&cfg, cli);

	// Force MJPEG and enable PiP
	cfg.libmy_uvc.video_codec = "mjpeg";
	cfg.libmy_uvc_pip.pip_enable = true;

	std::string verr;
	if (!uvctest::validate_config(&cfg, &verr)) {
		std::fprintf(stderr, "%s\n", verr.c_str());
		return 1;
	}

	// Set global application log level
	g_app_log_level = cfg.libmy_uvc.log_level;

	APP_LOGI("config: codec=%s channels=%d width=%d height=%d fps=%d camera_limit=%dx%d prefer_host_fps=%d "
	         "idle_sleep_ms=%d log_level=%d stats_enable=%d stats_interval_sec=%d\n",
	         cfg.libmy_uvc.video_codec.c_str(), cfg.libmy_uvc.channels,
	         cfg.libmy_uvc.width, cfg.libmy_uvc.height, cfg.libmy_uvc.fps,
	         cfg.uvctest.camera_width, cfg.uvctest.camera_height,
	         cfg.libmy_uvc.prefer_host_fps ? 1 : 0,
	         cfg.libmy_uvc.idle_sleep_ms, cfg.libmy_uvc.log_level, cfg.uvctest.stats_enable ? 1 : 0,
	         cfg.uvctest.stats_interval_sec);
	APP_LOGI("config: pip=1 rect=%dx%d@%d,%d quality=%d\n",
	         cfg.libmy_uvc_pip.pip_w, cfg.libmy_uvc_pip.pip_h, cfg.libmy_uvc_pip.pip_x, cfg.libmy_uvc_pip.pip_y,
	         cfg.libmy_uvc_pip.pip_jpeg_quality);

	my_app::PipelineCoordinator coordinator;
	if (!coordinator.Initialize(cfg, cli)) {
		APP_LOGE("PipelineCoordinator: Initialize failed\n");
		return 1;
	}

	return coordinator.Run(g_shutdown_requested);
}
