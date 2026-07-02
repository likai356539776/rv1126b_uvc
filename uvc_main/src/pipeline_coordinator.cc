#include "pipeline_coordinator.h"

#include <chrono>
#include <thread>
#include "app_log.h"
#include "uvctest/libmy_uvc_config_bridge.hpp"

extern "C" {
static int my_uvc_open_bridge(void *user, int w, int h, int fcc, int fps) {
	(void)user;
	APP_LOGI("open uvc %dx%d fcc=%d fps=%d\n", w, h, fcc, fps);
	return 0;
}

static void my_uvc_close_bridge(void *user) {
	(void)user;
	APP_LOGI("close uvc\n");
}
}

namespace my_app {

PipelineCoordinator::PipelineCoordinator() : uvc_ctx_(nullptr), initialized_(false) {}

PipelineCoordinator::~PipelineCoordinator() {
	if (uvc_ctx_) {
		my_uvc_destroy(uvc_ctx_);
	}
}

bool PipelineCoordinator::Initialize(const AppConfig& cfg, const uvctest::CliState& cli) {
	cfg_ = cfg;
	cli_ = cli;

	// Fill my_uvc configuration structure
	my_uvc_config_t ucfg{};
	uvctest_fill_my_uvc_config(cfg_, my_uvc_open_bridge, my_uvc_close_bridge, nullptr, &ucfg);

	uvc_ctx_ = my_uvc_create(&ucfg);
	if (!uvc_ctx_) {
		APP_LOGE("PipelineCoordinator: my_uvc_create failed\n");
		return false;
	}

	// Initialize Camera NPU processing pipeline
	CameraPipelineConfig cam_cfg;
	cam_cfg.yolo_model = cli_.yolo_model;
	cam_cfg.yolo_labels = cli_.yolo_labels;
	cam_cfg.max_tiles = cfg_.libmy_uvc_pip.pip_tile_n_tiles;
	cam_cfg.yolo_score_threshold = cfg_.uvctest.yolo_score_threshold;
	cam_cfg.camera_type = cfg_.uvctest.camera_type;
	cam_cfg.camera_node = cfg_.uvctest.camera_node;
	cam_cfg.width = cfg_.libmy_uvc.width;
	cam_cfg.height = cfg_.libmy_uvc.height;
	cam_cfg.fps = cfg_.libmy_uvc.fps;
	cam_cfg.camera_width = cfg_.uvctest.camera_width;
	cam_cfg.camera_height = cfg_.uvctest.camera_height;
	cam_cfg.vo_enable = cfg_.uvctest.vo_enable;
	if (!camera_pipeline_.Initialize(cam_cfg)) {
		APP_LOGE("PipelineCoordinator: camera_pipeline_.Initialize failed\n");
		my_uvc_destroy(uvc_ctx_);
		uvc_ctx_ = nullptr;
		return false;
	}

	initialized_ = true;
	return true;
}

int PipelineCoordinator::Run(const std::atomic<bool>& shutdown_flag) {
	if (!initialized_) {
		APP_LOGE("PipelineCoordinator: not initialized\n");
		return 1;
	}

	const uint32_t flags = 0;
	if (my_uvc_start(uvc_ctx_, flags) != 0) {
		APP_LOGE("PipelineCoordinator: my_uvc_start failed, run usb config script first\n");
		return 4;
	}

	// Initialize Streamer Pool Channels (must be done after UVC has started so video_ids can be resolved)
	bool has_valid_channels = false;
	for (int i = 0; i < cfg_.libmy_uvc.channels; i++) {
		StreamerChannelConfig ch{};
		ch.channel_id = i;
		ch.video_id = my_uvc_channel_video_id(i);
		ch.width = cfg_.libmy_uvc.width;
		ch.height = cfg_.libmy_uvc.height;
		ch.fps = cfg_.uvctest.channel_fps[i] > 0 ? cfg_.uvctest.channel_fps[i] : cfg_.libmy_uvc.fps;
		ch.idle_sleep_ms = cfg_.libmy_uvc.idle_sleep_ms;
		ch.pip_enable = cfg_.libmy_uvc_pip.pip_enable;
		ch.pip_x = cfg_.libmy_uvc_pip.pip_x;
		ch.pip_y = cfg_.libmy_uvc_pip.pip_y;
		ch.pip_w = cfg_.libmy_uvc_pip.pip_w;
		ch.pip_h = cfg_.libmy_uvc_pip.pip_h;
		ch.pip_jpeg_quality = cfg_.libmy_uvc_pip.pip_jpeg_quality;
		ch.pip_overlay_path = cfg_.libmy_uvc_pip.pip_overlay_path;
		ch.pip_overlay_stale_timeout_ms = cfg_.libmy_uvc_pip.pip_overlay_stale_timeout_ms;
		ch.pip_tile_n_tiles = cfg_.libmy_uvc_pip.pip_tile_n_tiles;
		ch.pip_tile_gap_px = cfg_.libmy_uvc_pip.pip_tile_gap_px;
		ch.pip_tile_margin_px = cfg_.libmy_uvc_pip.pip_tile_margin_px;
		ch.pip_width_stretch_factor = cfg_.libmy_uvc_pip.pip_width_stretch_factor;
		ch.pip_adaptive_scale_w = cfg_.libmy_uvc_pip.pip_adaptive_scale_w;
		ch.pip_adaptive_scale_h = cfg_.libmy_uvc_pip.pip_adaptive_scale_h;
		ch.pip_border_enable = cfg_.libmy_uvc_pip.pip_border_enable;
		ch.pip_border_radius = cfg_.libmy_uvc_pip.pip_border_radius;
		ch.pip_border_thickness = cfg_.libmy_uvc_pip.pip_border_thickness;
		ch.pip_border_color = cfg_.libmy_uvc_pip.pip_border_color;
		ch.log_every_frames = cfg_.uvctest.log_every_frames;

		if (ch.video_id < 0) {
			APP_LOGE("PipelineCoordinator: no video_id for channel %d\n", i);
			continue;
		}

		APP_LOGI("PipelineCoordinator: channel %d mapped video_id=%d fps=%d\n", i, ch.video_id, ch.fps);
		streamer_pool_.AddChannel(ch);
		has_valid_channels = true;
	}

	if (!has_valid_channels) {
		APP_LOGE("PipelineCoordinator: no valid channels to run\n");
		my_uvc_control_join(uvc_ctx_, flags);
		my_uvc_formats_deinit(uvc_ctx_);
		return 6;
	}

	// Start all worker pipelines
	camera_pipeline_.Start(shutdown_flag);
	streamer_pool_.Start(uvc_ctx_, camera_pipeline_, shutdown_flag);
	if (cfg_.uvctest.stats_enable) {
		stats_monitor_.Start(streamer_pool_.GetStatsList(), cfg_.uvctest.stats_interval_sec, shutdown_flag);
	}

	// Block until shutdown signal is received
	while (!shutdown_flag.load()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	APP_LOGI("PipelineCoordinator: shutdown requested, stopping all pipelines...\n");

	// Stop everything in reverse order of dependencies
	stats_monitor_.Stop();
	streamer_pool_.Stop();
	camera_pipeline_.Stop();

	my_uvc_control_join(uvc_ctx_, flags);
	my_uvc_formats_deinit(uvc_ctx_);

	return 0;
}

} // namespace my_app
