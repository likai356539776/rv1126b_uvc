#include "streamer_pool.h"

#include <chrono>
#include <stdexcept>
#include <algorithm>
#include "app_log.h"
#include "my_uvc_pip/pip_tile_layout.hpp"

namespace my_app {

StreamerPool::StreamerPool() {}

StreamerPool::~StreamerPool() {
	Stop();
}

void StreamerPool::AddChannel(const StreamerChannelConfig& ch_cfg) {
	channels_.push_back(ch_cfg);
	auto stats = std::make_shared<ChannelStats>();
	stats->channel_id = ch_cfg.channel_id;
	stats->video_id = ch_cfg.video_id;
	stats->target_fps = ch_cfg.fps;
	stats_list_.push_back(stats);
}

void StreamerPool::Start(my_uvc_t* uvc_ctx, const CameraPipeline& camera_pipeline, const std::atomic<bool>& shutdown_flag) {
	if (is_running_.load()) {
		return;
	}
	is_running_.store(true);
	workers_.reserve(channels_.size());
	for (size_t i = 0; i < channels_.size(); i++) {
		workers_.emplace_back(&StreamerPool::ChannelWorker, this, i, uvc_ctx, std::ref(camera_pipeline), std::ref(shutdown_flag));
	}
}

void StreamerPool::Stop() {
	if (is_running_.load()) {
		is_running_.store(false);
		for (auto &t : workers_) {
			if (t.joinable()) {
				t.join();
			}
		}
		workers_.clear();
	}
}

std::vector<std::shared_ptr<ChannelStats>> StreamerPool::GetStatsList() const {
	return stats_list_;
}

void StreamerPool::ChannelWorker(size_t index, my_uvc_t* uvc_ctx, const CameraPipeline& camera_pipeline, const std::atomic<bool>& shutdown_flag) {
	const auto& ch = channels_[index];
	auto stats = stats_list_[index];

	try {
		int active_video_id = ch.video_id;
		size_t sent_frames = 0;
		bool stream_was_on = false;
		const auto frame_interval = std::chrono::microseconds(1000000 / ch.fps);
		auto last_tick = std::chrono::steady_clock::now();

		pip_helper_t *pip = nullptr;
		pip_helper_config_t pcfg{};
		pcfg.pip_enable = 1;
		pcfg.canvas_width = ch.width;
		pcfg.canvas_height = ch.height;
		pcfg.pip_x = ch.pip_x;
		pcfg.pip_y = ch.pip_y;
		pcfg.pip_w = ch.pip_w;
		pcfg.pip_h = ch.pip_h;
		pcfg.pip_jpeg_quality = ch.pip_jpeg_quality;
		pcfg.pip_overlay_path = ch.pip_overlay_path.c_str();
		pcfg.pip_overlay_stale_timeout_ms = ch.pip_overlay_stale_timeout_ms;
		pcfg.pip_tile_n_tiles = ch.pip_tile_n_tiles;
		pcfg.pip_tile_gap_px = ch.pip_tile_gap_px;
		pcfg.pip_tile_margin_px = ch.pip_tile_margin_px;
		pcfg.pip_width_stretch_factor = ch.pip_width_stretch_factor;
		pcfg.pip_adaptive_scale_w = ch.pip_adaptive_scale_w;
		pcfg.pip_adaptive_scale_h = ch.pip_adaptive_scale_h;
		pcfg.pip_border_enable = ch.pip_border_enable ? 1 : 0;
		pcfg.pip_border_radius = ch.pip_border_radius;
		pcfg.pip_border_thickness = ch.pip_border_thickness;
		pcfg.pip_border_color = ch.pip_border_color.c_str();

		pip = pip_helper_create(ch.channel_id, &pcfg);
		if (!pip) {
			APP_LOGE("pip_helper_create failed (ch=%d): %s\n", ch.channel_id, pip_helper_last_error());
			stats->stream_on.store(0);
			return;
		}
		long long last_processed_frame_idx = -1;
		const uint8_t *pip_out = nullptr;
		size_t pip_out_len = 0;

		while (is_running_.load() && !shutdown_flag.load()) {
			int latest_video_id = my_uvc_channel_video_id(ch.channel_id);
			if (latest_video_id >= 0 && latest_video_id != active_video_id) {
				APP_LOGI("channel %d remap video_id %d -> %d\n", ch.channel_id, active_video_id, latest_video_id);
				active_video_id = latest_video_id;
				stats->video_id = active_video_id;
				stream_was_on = false;
			}
			if (latest_video_id < 0) {
				if (stats->stream_on.load() != 0)
					stats->stream_on.store(0);
				stream_was_on = false;
				std::this_thread::sleep_for(std::chrono::milliseconds(ch.idle_sleep_ms));
				continue;
			}

			bool channel_stream_on = my_uvc_video_streaming(active_video_id) != 0;
			if (!channel_stream_on) {
				if (stats->stream_on.load() != 0) {
					stats->stream_on.store(0);
					APP_LOGD("channel %d stream OFF (video_id=%d)\n", ch.channel_id, active_video_id);
				}
				stream_was_on = false;
				std::this_thread::sleep_for(std::chrono::milliseconds(ch.idle_sleep_ms));
				continue;
			}

			if (!stream_was_on) {
				if (stats->stream_on.load() == 0) {
					stats->stream_on.store(1);
					APP_LOGD("channel %d stream ON (video_id=%d)\n", ch.channel_id, active_video_id);
				}
				last_tick = std::chrono::steady_clock::now();
				stream_was_on = true;
			}

			std::shared_ptr<const FrameData> current_frame = camera_pipeline.GetLatestFrame();
			if (!current_frame) {
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			bool is_duplicate = (current_frame->frame_index == last_processed_frame_idx);

			if (is_duplicate && pip_out && pip_out_len > 0) {
				if (uvc_ctx) {
					my_uvc_submit_mjpeg(uvc_ctx, ch.channel_id, pip_out, pip_out_len);
				}
				sent_frames++;
				stats->total_frames.fetch_add(1);
			} else {
				pip_helper_composite_opts_t po{};
				po.now_ms = 0;

				if (current_frame->presenter_updated) {
					po.presenter_nv12_updated = 1;
					po.presenter_nv12 = current_frame->presenter_virt_addr;
					po.presenter_fd = current_frame->presenter_fd;
					po.presenter_nv12_src_w = current_frame->presenter_w;
					po.presenter_nv12_src_h = current_frame->presenter_h;
				} else {
					po.presenter_nv12_updated = 0;
					po.presenter_fd = -1;
					po.presenter_nv12 = nullptr;
				}

				int n_active = std::min((int)current_frame->tiles.size(), ch.pip_tile_n_tiles);
				po.n_active = n_active;

				const uint8_t *tile_nv12_ptrs[my_uvc_pip::kPipTileLayoutMax];
				int tile_fds[my_uvc_pip::kPipTileLayoutMax];
				int tile_src_w_arr[my_uvc_pip::kPipTileLayoutMax];
				int tile_src_h_arr[my_uvc_pip::kPipTileLayoutMax];
				int tile_updated_arr[my_uvc_pip::kPipTileLayoutMax];

				for (int ti = 0; ti < n_active; ti++) {
					tile_nv12_ptrs[ti] = current_frame->tiles[ti].virt_addr;
					tile_fds[ti] = current_frame->tiles[ti].fd;
					tile_src_w_arr[ti] = current_frame->tiles[ti].w;
					tile_src_h_arr[ti] = current_frame->tiles[ti].h;
					tile_updated_arr[ti] = 1;
				}

				po.tile_nv12 = tile_nv12_ptrs;
				po.tile_fds = tile_fds;
				po.tile_src_w = tile_src_w_arr;
				po.tile_src_h = tile_src_h_arr;
				po.tile_nv12_updated = tile_updated_arr;

				int pc = pip_helper_composite_nv12_background(
					pip,
					-1, // Pass -1 to remove background camera layer (canvas defaults to black)
					current_frame->bg_w,
					current_frame->bg_h,
					&po,
					&pip_out,
					&pip_out_len
				);

				if (pc != 0) {
					stats->error_count.fetch_add(1);
				} else {
					static bool dumped = false;
					if (!dumped && ch.channel_id == 0) {
						dumped = true;
						FILE *fp = fopen("/userdata/dump.jpg", "wb");
						if (fp) {
							fwrite(pip_out, 1, pip_out_len, fp);
							fclose(fp);
							APP_LOGI("DEBUG: dumped first frame of ch=0 to /userdata/dump.jpg, size=%zu\n", pip_out_len);
						} else {
							APP_LOGE("DEBUG: failed to open /userdata/dump.jpg for writing\n");
						}
					}
					if (uvc_ctx) {
						my_uvc_submit_mjpeg(uvc_ctx, ch.channel_id, pip_out, pip_out_len);
					}
					sent_frames++;
					stats->total_frames.fetch_add(1);
					last_processed_frame_idx = current_frame->frame_index;
				}
			}

			auto now = std::chrono::steady_clock::now();
			auto due = last_tick + frame_interval;
			if (now < due)
				std::this_thread::sleep_for(due - now);
			last_tick = std::chrono::steady_clock::now();

			if (ch.log_every_frames > 0 && (sent_frames % static_cast<size_t>(ch.log_every_frames)) == 0) {
				APP_LOGI("ch=%d video_id=%d sent=%zu fps=%d\n", ch.channel_id, active_video_id, sent_frames, ch.fps);
			}
		}

		pip_helper_destroy(pip);
		stats->stream_on.store(0);
	} catch (const std::exception &e) {
		APP_LOGE("channel %zu worker exception: %s\n", index, e.what());
		stats->error_count.fetch_add(1);
	} catch (...) {
		APP_LOGE("channel %zu worker unknown exception\n", index);
		stats->error_count.fetch_add(1);
	}
}

} // namespace my_app
