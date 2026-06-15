#include "app_config.h"
#include "my_uvc/my_uvc.h"
#include "my_uvc_pip/pip_helper.h"
#include "my_uvc_pip/pip_tile_layout.hpp"
#include "uvctest/libmy_uvc_config_bridge.hpp"
#include "uvctest/uvctest_cli.hpp"
#include "camera_rockit_vi_vo.h"
#include "yolo_person_detector.h"

#include <array>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
std::atomic<bool> g_run(true);
std::atomic<int> g_log_level(1);
std::mutex g_log_mu;
my_uvc_t *g_my_uvc_ctx = nullptr;

enum LogLevel {
	LOG_ERROR = 0,
	LOG_INFO = 1,
	LOG_DEBUG = 2
};

void log_msg(int level, const char *fmt, ...) {
	if (level > g_log_level.load())
		return;
	std::lock_guard<std::mutex> lk(g_log_mu);
	const char *tag = (level == LOG_ERROR) ? "E" : (level == LOG_DEBUG) ? "D" : "I";
	std::fprintf(stderr, "[uvctest][%s] ", tag);
	va_list ap;
	va_start(ap, fmt);
	std::vfprintf(stderr, fmt, ap);
	va_end(ap);
	std::fprintf(stderr, "\n");
}

void on_signal(int signo) {
	(void)signo;
	g_run.store(false);
}

struct FrameData {
	long long frame_index = 0;
	std::vector<uint8_t> bg_nv12;
	int bg_w = 0;
	int bg_h = 0;

	std::vector<uint8_t> presenter_nv12;
	int presenter_w = 0;
	int presenter_h = 0;
	bool presenter_updated = false;

	struct Tile {
		std::vector<uint8_t> nv12;
		int w = 0;
		int h = 0;
	};
	std::vector<Tile> tiles;
};

std::shared_ptr<FrameData> g_latest_frame = nullptr;
std::mutex g_frame_mutex;

void get_aligned_crop_box(int src_w, int src_h, const image_rect_t &box, image_rect_t *src_box, int *crop_w, int *crop_h) {
	int w = box.right - box.left + 1;
	int h = box.bottom - box.top + 1;

	int aw = (w + 15) & ~15;
	int ah = (h + 1) & ~1;

	int cx = box.left + w / 2;
	int cy = box.top + h / 2;

	int left = cx - aw / 2;
	int top = cy - ah / 2;

	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (left + aw > src_w) left = src_w - aw;
	if (top + ah > src_h) top = src_h - ah;

	if (left < 0) { left = 0; aw = (src_w / 16) * 16; }
	if (top < 0) { top = 0; ah = (src_h / 2) * 2; }

	src_box->left = left;
	src_box->top = top;
	src_box->right = left + aw - 1;
	src_box->bottom = top + ah - 1;

	*crop_w = aw;
	*crop_h = ah;
}

void camera_thread_func(std::string yolo_model_path, std::string yolo_labels_path) {
	my_app::YoloPersonDetector yolo;
	if (yolo.Init(yolo_model_path.c_str(), yolo_labels_path.c_str()) != 0) {
		log_msg(LOG_ERROR, "camera_thread: YoloPersonDetector Init failed (model: %s, labels: %s)",
		        yolo_model_path.c_str(), yolo_labels_path.c_str());
		return;
	}

	my_app::CameraRockitRgbReader rock_reader;
	my_app::RockitCameraConfig rcfg;
	rcfg.vi_pipe_id = 0;
	rcfg.vi_dev_id = 0;
	rcfg.vi_chn_id = 0;
	rcfg.width = 1920;
	rcfg.height = 1080;
	rcfg.vo_enable = true;
	rcfg.vo_layer = 0;
	rcfg.vo_dev = 0;
	rcfg.vo_chn = 0;
#if defined(RV1126B)
	rcfg.vo_intf_type = 1;
#else
	rcfg.vo_intf_type = 0;
#endif
	rcfg.vo_disp_width = 1080;
	rcfg.vo_disp_height = 1920;
	rcfg.vo_layer_no_compress = false;
	rcfg.vo_rotation_deg = 0;
	rcfg.camera_mirror = false;

	if (rock_reader.Open(rcfg) != 0) {
		log_msg(LOG_ERROR, "camera_thread: CameraRockitRgbReader Open failed");
		yolo.Shutdown();
		return;
	}

	log_msg(LOG_INFO, "camera_thread: Camera initialized successfully. Resolution: %dx%d", rock_reader.width(), rock_reader.height());

	int vw = rock_reader.width();
	int vh = rock_reader.height();
	size_t raw_rgb = (size_t)vw * (size_t)vh * 3u;
	std::vector<uint8_t> rgb_buf(raw_rgb);

	image_buffer_t camera_rgb_img{};
	camera_rgb_img.width = vw;
	camera_rgb_img.height = vh;
	camera_rgb_img.format = IMAGE_FORMAT_RGB888;
	camera_rgb_img.size = (int)raw_rgb;
	camera_rgb_img.virt_addr = rgb_buf.data();

	image_buffer_t src_nv12_img{};
	src_nv12_img.width = vw;
	src_nv12_img.height = vh;
	src_nv12_img.format = IMAGE_FORMAT_YUV420SP_NV12;
	src_nv12_img.size = vw * vh * 3 / 2;

	long long frame_idx = 0;

	while (g_run.load()) {
		int read_r = rock_reader.ReadNextRgbInto(&camera_rgb_img, 1000);
		if (read_r != 0) {
			log_msg(LOG_ERROR, "camera_thread: ReadNextRgbInto failed, ret=%d", read_r);
			std::this_thread::sleep_for(std::chrono::milliseconds(30));
			continue;
		}

		frame_idx = rock_reader.frame_index();
		const uint8_t* nv12_data = rock_reader.GetLastNv12Data();
		if (!nv12_data) {
			log_msg(LOG_ERROR, "camera_thread: GetLastNv12Data returned null");
			continue;
		}
		src_nv12_img.virt_addr = const_cast<uint8_t*>(nv12_data);

		object_detect_result_list od_results{};
		if (yolo.DetectPersons(&camera_rgb_img, &od_results, nullptr) != 0) {
			log_msg(LOG_ERROR, "camera_thread: DetectPersons failed");
			continue;
		}

		std::vector<object_detect_result> persons;
		for (int i = 0; i < od_results.count; i++) {
			if (od_results.results[i].cls_id == 0) { // person
				persons.push_back(od_results.results[i]);
			}
		}

		auto new_frame = std::make_shared<FrameData>();
		new_frame->frame_index = frame_idx;
		new_frame->bg_w = vw;
		new_frame->bg_h = vh;
		new_frame->bg_nv12.assign(nv12_data, nv12_data + vw * vh * 3 / 2);

		int n_tiles = std::min((int)persons.size(), my_uvc_pip::kPipTileLayoutMax);
		new_frame->tiles.resize(n_tiles);

		for (int i = 0; i < n_tiles; i++) {
			image_rect_t crop_box{};
			int crop_w = 0, crop_h = 0;
			get_aligned_crop_box(vw, vh, persons[i].box, &crop_box, &crop_w, &crop_h);

			new_frame->tiles[i].w = crop_w;
			new_frame->tiles[i].h = crop_h;
			new_frame->tiles[i].nv12.resize(crop_w * crop_h * 3 / 2);

			image_buffer_t dst_nv12_img{};
			dst_nv12_img.width = crop_w;
			dst_nv12_img.height = crop_h;
			dst_nv12_img.format = IMAGE_FORMAT_YUV420SP_NV12;
			dst_nv12_img.virt_addr = new_frame->tiles[i].nv12.data();
			dst_nv12_img.size = crop_w * crop_h * 3 / 2;

			image_rect_t dst_box{0, 0, crop_w - 1, crop_h - 1};
			if (convert_image(&src_nv12_img, &dst_nv12_img, &crop_box, &dst_box, 0) != 0) {
				log_msg(LOG_ERROR, "camera_thread: crop tile %d failed", i);
			}
		}

		if (!persons.empty()) {
			image_rect_t crop_box{};
			int crop_w = 0, crop_h = 0;
			get_aligned_crop_box(vw, vh, persons[0].box, &crop_box, &crop_w, &crop_h);

			new_frame->presenter_w = crop_w;
			new_frame->presenter_h = crop_h;
			new_frame->presenter_nv12.resize(crop_w * crop_h * 3 / 2);

			image_buffer_t dst_nv12_img{};
			dst_nv12_img.width = crop_w;
			dst_nv12_img.height = crop_h;
			dst_nv12_img.format = IMAGE_FORMAT_YUV420SP_NV12;
			dst_nv12_img.virt_addr = new_frame->presenter_nv12.data();
			dst_nv12_img.size = crop_w * crop_h * 3 / 2;

			image_rect_t dst_box{0, 0, crop_w - 1, crop_h - 1};
			if (convert_image(&src_nv12_img, &dst_nv12_img, &crop_box, &dst_box, 0) == 0) {
				new_frame->presenter_updated = true;
			} else {
				log_msg(LOG_ERROR, "camera_thread: crop presenter failed");
			}
		}

		{
			std::lock_guard<std::mutex> lock(g_frame_mutex);
			g_latest_frame = new_frame;
		}
	}

	rock_reader.Close();
	rock_reader.ShutdownSubsystem();
	yolo.Shutdown();
}

struct StreamChannelContext {
	int channel_id;
	int video_id;
	int width;
	int height;
	int fps;
	int idle_sleep_ms;
	int log_every_frames;
	bool pip_enable;
	std::string pip_overlay_path;
	int pip_x;
	int pip_y;
	int pip_w;
	int pip_h;
	int pip_jpeg_quality;
	int pip_overlay_stale_timeout_ms;
	int pip_tile_n_tiles;
	int pip_tile_gap_px;
	int pip_tile_margin_px;
	struct ChannelStats *stats;
};

struct ChannelStats {
	int channel_id;
	int video_id;
	int target_fps;
	std::atomic<unsigned long long> total_frames;
	std::atomic<unsigned long long> error_count;
	std::atomic<int> stream_on;

	ChannelStats() : channel_id(-1), video_id(-1), target_fps(0), total_frames(0), error_count(0), stream_on(0) {}
};

void channel_worker(StreamChannelContext ch) {
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

	pip = pip_helper_create(ch.channel_id, &pcfg);
	if (!pip) {
		log_msg(LOG_ERROR, "pip_helper_create failed (ch=%d): %s", ch.channel_id, pip_helper_last_error());
		if (ch.stats)
			ch.stats->stream_on.store(0);
		return;
	}
	log_msg(LOG_INFO, "pip: ch=%d helper=%s (camera background mode)", ch.channel_id, pip_helper_version());

	while (g_run.load()) {
		int latest_video_id = my_uvc_channel_video_id(ch.channel_id);
		if (latest_video_id >= 0 && latest_video_id != active_video_id) {
			log_msg(LOG_INFO, "channel %d remap video_id %d -> %d", ch.channel_id, active_video_id,
			        latest_video_id);
			active_video_id = latest_video_id;
			if (ch.stats)
				ch.stats->video_id = active_video_id;
			stream_was_on = false;
		}
		if (latest_video_id < 0) {
			if (ch.stats && ch.stats->stream_on.load() != 0)
				ch.stats->stream_on.store(0);
			stream_was_on = false;
			std::this_thread::sleep_for(std::chrono::milliseconds(ch.idle_sleep_ms));
			continue;
		}

		bool channel_stream_on = my_uvc_video_streaming(active_video_id) != 0;
		if (!channel_stream_on) {
			if (ch.stats && ch.stats->stream_on.load() != 0) {
				ch.stats->stream_on.store(0);
				log_msg(LOG_DEBUG, "channel %d stream OFF (video_id=%d)", ch.channel_id, active_video_id);
			}
			stream_was_on = false;
			std::this_thread::sleep_for(std::chrono::milliseconds(ch.idle_sleep_ms));
			continue;
		}

		if (!stream_was_on) {
			if (ch.stats && ch.stats->stream_on.load() == 0) {
				ch.stats->stream_on.store(1);
				log_msg(LOG_DEBUG, "channel %d stream ON (video_id=%d)", ch.channel_id, active_video_id);
			}
			last_tick = std::chrono::steady_clock::now();
			stream_was_on = true;
		}

		std::shared_ptr<FrameData> current_frame = nullptr;
		{
			std::lock_guard<std::mutex> lock(g_frame_mutex);
			current_frame = g_latest_frame;
		}

		if (!current_frame) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			continue;
		}

		pip_helper_composite_opts_t po{};
		po.now_ms = 0;

		if (current_frame->presenter_updated) {
			po.presenter_nv12_updated = 1;
			po.presenter_nv12 = current_frame->presenter_nv12.data();
			po.presenter_nv12_src_w = current_frame->presenter_w;
			po.presenter_nv12_src_h = current_frame->presenter_h;
		} else {
			po.presenter_nv12_updated = 0;
		}

		int n_active = std::min((int)current_frame->tiles.size(), ch.pip_tile_n_tiles);
		po.n_active = n_active;

		const uint8_t *tile_nv12_ptrs[my_uvc_pip::kPipTileLayoutMax];
		int tile_src_w_arr[my_uvc_pip::kPipTileLayoutMax];
		int tile_src_h_arr[my_uvc_pip::kPipTileLayoutMax];
		int tile_updated_arr[my_uvc_pip::kPipTileLayoutMax];

		for (int ti = 0; ti < n_active; ti++) {
			tile_nv12_ptrs[ti] = current_frame->tiles[ti].nv12.data();
			tile_src_w_arr[ti] = current_frame->tiles[ti].w;
			tile_src_h_arr[ti] = current_frame->tiles[ti].h;
			tile_updated_arr[ti] = 1;
		}

		po.tile_nv12 = tile_nv12_ptrs;
		po.tile_src_w = tile_src_w_arr;
		po.tile_src_h = tile_src_h_arr;
		po.tile_nv12_updated = tile_updated_arr;

		const uint8_t *pip_out = nullptr;
		size_t pip_out_len = 0;
		int pc = pip_helper_composite_nv12_background(
			pip,
			current_frame->bg_nv12.data(),
			current_frame->bg_w,
			current_frame->bg_h,
			&po,
			&pip_out,
			&pip_out_len
		);

		if (pc != 0) {
			if (ch.stats)
				ch.stats->error_count.fetch_add(1);
		} else {
			static bool dumped = false;
			if (!dumped && ch.channel_id == 0) {
				dumped = true;
				FILE *fp = fopen("/userdata/dump.jpg", "wb");
				if (fp) {
					fwrite(pip_out, 1, pip_out_len, fp);
					fclose(fp);
					log_msg(LOG_INFO, "DEBUG: dumped first frame of ch=0 to /userdata/dump.jpg, size=%zu", pip_out_len);
				} else {
					log_msg(LOG_ERROR, "DEBUG: failed to open /userdata/dump.jpg for writing");
				}
			}
			if (g_my_uvc_ctx)
				my_uvc_submit_mjpeg(g_my_uvc_ctx, ch.channel_id, pip_out, pip_out_len);
			sent_frames++;
			if (ch.stats)
				ch.stats->total_frames.fetch_add(1);
		}

		auto now = std::chrono::steady_clock::now();
		auto due = last_tick + frame_interval;
		if (now < due)
			std::this_thread::sleep_for(due - now);
		last_tick = std::chrono::steady_clock::now();

		if (ch.log_every_frames > 0 && (sent_frames % static_cast<size_t>(ch.log_every_frames)) == 0) {
			log_msg(LOG_INFO, "ch=%d video_id=%d sent=%zu fps=%d", ch.channel_id, active_video_id,
			        sent_frames, ch.fps);
		}
	}

	pip_helper_destroy(pip);

	if (ch.stats)
		ch.stats->stream_on.store(0);
	} catch (const std::exception &e) {
		log_msg(LOG_ERROR, "channel %d worker exception: %s", ch.channel_id, e.what());
		if (ch.stats)
			ch.stats->error_count.fetch_add(1);
	} catch (...) {
		log_msg(LOG_ERROR, "channel %d worker unknown exception", ch.channel_id);
		if (ch.stats)
			ch.stats->error_count.fetch_add(1);
	}
}

void stats_worker(std::vector<std::shared_ptr<ChannelStats>> stats, int interval_sec) {
	std::vector<unsigned long long> prev(stats.size(), 0);
	while (g_run.load()) {
		std::this_thread::sleep_for(std::chrono::seconds(interval_sec));
		if (!g_run.load())
			break;
		for (size_t i = 0; i < stats.size(); i++) {
			auto &s = stats[i];
			unsigned long long total = s->total_frames.load();
			unsigned long long delta = total - prev[i];
			prev[i] = total;
			double realtime_fps = static_cast<double>(delta) / static_cast<double>(interval_sec);
			log_msg(LOG_INFO,
			        "stats ch=%d video_id=%d on=%d target_fps=%d realtime_fps=%.2f total=%llu errors=%llu",
			        s->channel_id, s->video_id, s->stream_on.load(), s->target_fps, realtime_fps, total,
			        s->error_count.load());
		}
	}
}

int open_uvc_cb(int width, int height, int fcc, int fps) {
	log_msg(LOG_INFO, "open uvc %dx%d fcc=%d fps=%d", width, height, fcc, fps);
	return 0;
}

void close_uvc_cb(void) {
	log_msg(LOG_INFO, "close uvc");
}

extern "C" {
int my_uvc_open_bridge(void *user, int w, int h, int fcc, int fps) {
	(void)user;
	return open_uvc_cb(w, h, fcc, fps);
}
void my_uvc_close_bridge(void *user) {
	(void)user;
	close_uvc_cb();
}
}

void print_usage(const char *argv0) {
	std::fprintf(stderr,
	             "Usage: %s [-c dir|file] [--codec mjpeg] [--channels n] [--width w] "
	             "[--height h] [--fps fps] "
	             "[--size WxH] "
	             "[--pip-enable 0|1] [--pip-x n] [--pip-y n] [--pip-w n] [--pip-h n] "
	             "[--pip-jpeg-quality 1-100] "
	             "[--log-every n] [--log-level 0|1|2] [--stats-enable 0|1] "
	             "[--stats-interval sec]\n"
	             "  Board/install binary name: `uvctest`.\n",
	             argv0);
}
} // namespace

int main(int argc, char **argv) {
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

	g_log_level.store(cfg.libmy_uvc.log_level);

	log_msg(LOG_INFO,
	        "config: codec=%s channels=%d width=%d height=%d fps=%d prefer_host_fps=%d "
	        "idle_sleep_ms=%d log_level=%d stats_enable=%d stats_interval_sec=%d",
	        cfg.libmy_uvc.video_codec.c_str(), cfg.libmy_uvc.channels,
	        cfg.libmy_uvc.width, cfg.libmy_uvc.height, cfg.libmy_uvc.fps,
	        cfg.libmy_uvc.prefer_host_fps ? 1 : 0,
	        cfg.libmy_uvc.idle_sleep_ms, cfg.libmy_uvc.log_level, cfg.uvctest.stats_enable ? 1 : 0,
	        cfg.uvctest.stats_interval_sec);
	log_msg(LOG_INFO, "config: pip=1 rect=%dx%d@%d,%d quality=%d",
	        cfg.libmy_uvc_pip.pip_w, cfg.libmy_uvc_pip.pip_h, cfg.libmy_uvc_pip.pip_x, cfg.libmy_uvc_pip.pip_y,
	        cfg.libmy_uvc_pip.pip_jpeg_quality);
	log_msg(LOG_DEBUG, "pip_helper: %s", pip_helper_version());

	const uint32_t flags = 0;
	my_uvc_config_t ucfg{};
	uvctest_fill_my_uvc_config(cfg, my_uvc_open_bridge, my_uvc_close_bridge, nullptr, &ucfg);

	my_uvc_t *uvc = my_uvc_create(&ucfg);
	if (!uvc) {
		log_msg(LOG_ERROR, "my_uvc_create failed");
		return 4;
	}
	g_my_uvc_ctx = uvc;

	if (my_uvc_start(uvc, flags) != 0) {
		log_msg(LOG_ERROR, "uvc_control_run failed, run usb config script first");
		g_my_uvc_ctx = nullptr;
		my_uvc_destroy(uvc);
		return 4;
	}

	std::thread camera_thread(camera_thread_func, cli.yolo_model, cli.yolo_labels);

	std::vector<StreamChannelContext> channels;
	std::vector<std::shared_ptr<ChannelStats>> stats_list;
	channels.reserve(static_cast<size_t>(cfg.libmy_uvc.channels));
	stats_list.reserve(static_cast<size_t>(cfg.libmy_uvc.channels));
	for (int i = 0; i < cfg.libmy_uvc.channels; i++) {
		StreamChannelContext ch{};
		ch.channel_id = i;
		ch.video_id = my_uvc_channel_video_id(i);
		ch.width = cfg.libmy_uvc.width;
		ch.height = cfg.libmy_uvc.height;
		ch.fps = cfg.uvctest.channel_fps[i] > 0 ? cfg.uvctest.channel_fps[i] : cfg.libmy_uvc.fps;
		ch.idle_sleep_ms = cfg.libmy_uvc.idle_sleep_ms;
		ch.pip_enable = cfg.libmy_uvc_pip.pip_enable;
		ch.pip_x = cfg.libmy_uvc_pip.pip_x;
		ch.pip_y = cfg.libmy_uvc_pip.pip_y;
		ch.pip_w = cfg.libmy_uvc_pip.pip_w;
		ch.pip_h = cfg.libmy_uvc_pip.pip_h;
		ch.pip_jpeg_quality = cfg.libmy_uvc_pip.pip_jpeg_quality;
		ch.pip_overlay_path = cfg.libmy_uvc_pip.pip_overlay_path;
		ch.pip_overlay_stale_timeout_ms = cfg.libmy_uvc_pip.pip_overlay_stale_timeout_ms;
		ch.pip_tile_n_tiles = cfg.libmy_uvc_pip.pip_tile_n_tiles;
		ch.pip_tile_gap_px = cfg.libmy_uvc_pip.pip_tile_gap_px;
		ch.pip_tile_margin_px = cfg.libmy_uvc_pip.pip_tile_margin_px;
		ch.log_every_frames = cfg.uvctest.log_every_frames;
		
		auto stats = std::make_shared<ChannelStats>();
		stats->channel_id = i;
		stats->video_id = ch.video_id;
		stats->target_fps = ch.fps;
		ch.stats = stats.get();
		
		if (ch.video_id < 0) {
			log_msg(LOG_ERROR, "no video_id for channel %d", i);
			continue;
		}
		
		log_msg(LOG_INFO, "channel %d mapped video_id=%d fps=%d (camera input)", i, ch.video_id, ch.fps);
		channels.push_back(std::move(ch));
		stats_list.push_back(std::move(stats));
	}

	if (channels.empty()) {
		log_msg(LOG_ERROR, "no valid channels to run");
		g_run.store(false);
		if (camera_thread.joinable())
			camera_thread.join();
		my_uvc_control_join(uvc, flags);
		my_uvc_formats_deinit(uvc);
		g_my_uvc_ctx = nullptr;
		my_uvc_destroy(uvc);
		return 6;
	}

	std::vector<std::thread> workers;
	std::thread stats_thread;
	workers.reserve(channels.size());
	for (auto &ch : channels)
		workers.emplace_back(channel_worker, ch);
	if (cfg.uvctest.stats_enable)
		stats_thread = std::thread(stats_worker, stats_list, cfg.uvctest.stats_interval_sec);

	for (auto &t : workers)
		t.join();
	g_run.store(false);
	if (stats_thread.joinable())
		stats_thread.join();
	if (camera_thread.joinable())
		camera_thread.join();

	my_uvc_control_join(uvc, flags);
	my_uvc_formats_deinit(uvc);
	g_my_uvc_ctx = nullptr;
	my_uvc_destroy(uvc);
	log_msg(LOG_INFO, "exit");
	return 0;
}
