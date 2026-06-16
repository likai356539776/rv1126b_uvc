#pragma once

#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include "my_uvc/my_uvc.h"
#include "my_uvc_pip/pip_helper.h"
#include "camera_pipeline.h"

namespace my_app {

struct ChannelStats {
	int channel_id;
	int video_id;
	int target_fps;
	std::atomic<unsigned long long> total_frames{0};
	std::atomic<unsigned long long> error_count{0};
	std::atomic<int> stream_on{0};

	ChannelStats() : channel_id(-1), video_id(-1), target_fps(0) {}
	
	// Delete copy/move to allow std::vector<std::shared_ptr<ChannelStats>>
	ChannelStats(const ChannelStats&) = delete;
	ChannelStats& operator=(const ChannelStats&) = delete;
};

struct StreamerChannelConfig {
	int channel_id = -1;
	int video_id = -1;
	int width = 0;
	int height = 0;
	int fps = 30;
	int idle_sleep_ms = 10;
	int log_every_frames = 120;
	bool pip_enable = false;
	std::string pip_overlay_path;
	int pip_x = 20;
	int pip_y = 20;
	int pip_w = 640;
	int pip_h = 480;
	int pip_jpeg_quality = 85;
	int pip_overlay_stale_timeout_ms = 5000;
	int pip_tile_n_tiles = 0;
	int pip_tile_gap_px = 0;
	int pip_tile_margin_px = 0;
	float pip_width_stretch_factor = 1.3f;
	bool pip_border_enable = true;
	int pip_border_radius = 4;
	int pip_border_thickness = 2;
	std::string pip_border_color = "#FFFFFF";
};

class StreamerPool {
public:
	StreamerPool();
	~StreamerPool();

	StreamerPool(const StreamerPool&) = delete;
	StreamerPool& operator=(const StreamerPool&) = delete;

	void AddChannel(const StreamerChannelConfig& ch_cfg);
	void Start(my_uvc_t* uvc_ctx, const CameraPipeline& camera_pipeline, const std::atomic<bool>& shutdown_flag);
	void Stop();

	std::vector<std::shared_ptr<ChannelStats>> GetStatsList() const;

private:
	void ChannelWorker(size_t index, my_uvc_t* uvc_ctx, const CameraPipeline& camera_pipeline, const std::atomic<bool>& shutdown_flag);

	std::vector<StreamerChannelConfig> channels_;
	std::vector<std::shared_ptr<ChannelStats>> stats_list_;
	std::vector<std::thread> workers_;
	std::atomic<bool> is_running_{false};
};

} // namespace my_app
