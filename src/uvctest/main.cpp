#include "app_config.h"
#include "my_uvc/my_uvc.h"
#include "my_uvc_pip/pip_helper.h"
#include "uvctest/libmy_uvc_config_bridge.hpp"
#include "uvctest/uvctest_cli.hpp"

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
#include <fstream>
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

bool read_file_all(const std::string &path, std::vector<uint8_t> *out);

std::vector<std::pair<size_t, size_t>> split_jpeg_by_soi(const std::vector<uint8_t> &buf) {
	std::vector<std::pair<size_t, size_t>> out;
	std::vector<size_t> starts;
	for (size_t i = 0; i + 1 < buf.size(); ++i) {
		if (buf[i] == 0xFF && buf[i + 1] == 0xD8)
			starts.push_back(i);
	}
	for (size_t k = 0; k < starts.size(); ++k) {
		size_t off = starts[k];
		size_t end = (k + 1 < starts.size()) ? starts[k + 1] : buf.size();
		if (end > off)
			out.emplace_back(off, end - off);
	}
	return out;
}

bool is_dir_path(const std::string &path) {
	std::error_code ec;
	return std::filesystem::is_directory(std::filesystem::path(path), ec) && !ec;
}

std::string to_lower_ascii(std::string s) {
	for (auto &c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

[[maybe_unused]] bool load_first_jpeg_from_dir(const std::string &dir, std::vector<uint8_t> *jpeg) {
	if (!jpeg)
		return false;
	jpeg->clear();

	std::error_code ec;
	std::vector<std::filesystem::path> files;
	for (const auto &ent : std::filesystem::directory_iterator(std::filesystem::path(dir), ec)) {
		if (ec)
			break;
		if (!ent.is_regular_file())
			continue;
		auto ext = to_lower_ascii(ent.path().extension().string());
		if (ext == ".jpg" || ext == ".jpeg")
			files.push_back(ent.path());
	}
	if (ec || files.empty())
		return false;

	std::sort(files.begin(), files.end(),
	          [](const std::filesystem::path &a, const std::filesystem::path &b) {
		          return a.filename().string() < b.filename().string();
	          });

	std::vector<uint8_t> img;
	if (!read_file_all(files.front().string(), &img))
		return false;
	if (img.size() < 2 || img[0] != 0xFF || img[1] != 0xD8)
		return false;
	*jpeg = std::move(img);
	return true;
}

[[maybe_unused]] bool load_jpegs_from_dir(const std::string &dir, std::vector<std::vector<uint8_t>> *jpegs) {
	if (!jpegs)
		return false;
	jpegs->clear();

	std::error_code ec;
	std::vector<std::filesystem::path> files;
	for (const auto &ent : std::filesystem::directory_iterator(std::filesystem::path(dir), ec)) {
		if (ec)
			break;
		if (!ent.is_regular_file())
			continue;
		auto ext = to_lower_ascii(ent.path().extension().string());
		if (ext == ".jpg" || ext == ".jpeg")
			files.push_back(ent.path());
	}
	if (ec)
		return false;
	if (files.empty())
		return false;

	std::sort(files.begin(), files.end(),
	          [](const std::filesystem::path &a, const std::filesystem::path &b) {
		          return a.filename().string() < b.filename().string();
	          });

	jpegs->reserve(files.size());
	for (const auto &p : files) {
		std::vector<uint8_t> img;
		if (!read_file_all(p.string(), &img))
			return false;
		if (img.size() < 2 || img[0] != 0xFF || img[1] != 0xD8)
			return false;
		jpegs->push_back(std::move(img));
	}
	return !jpegs->empty();
}

bool load_mjpeg_frames_from_dir(const std::string &dir, std::vector<std::vector<uint8_t>> *frames) {
	if (!frames)
		return false;
	frames->clear();

	std::error_code ec;
	std::vector<std::filesystem::path> files;
	for (const auto &ent : std::filesystem::directory_iterator(std::filesystem::path(dir), ec)) {
		if (ec)
			break;
		if (!ent.is_regular_file())
			continue;
		auto ext = to_lower_ascii(ent.path().extension().string());
		if (ext == ".jpg" || ext == ".jpeg")
			files.push_back(ent.path());
	}
	if (ec)
		return false;

	std::sort(files.begin(), files.end(),
	          [](const std::filesystem::path &a, const std::filesystem::path &b) {
		          return a.filename().string() < b.filename().string();
	          });

	frames->reserve(files.size());
	for (const auto &p : files) {
		std::vector<uint8_t> img;
		if (!read_file_all(p.string(), &img))
			return false;
		if (img.size() < 2 || img[0] != 0xFF || img[1] != 0xD8)
			return false; // must start with JPEG SOI
		frames->push_back(std::move(img));
	}
	return !frames->empty();
}

bool read_file_all(const std::string &path, std::vector<uint8_t> *out) {
	std::ifstream in(path, std::ios::binary | std::ios::ate);
	if (!in.is_open())
		return false;
	std::streamsize size = in.tellg();
	if (size <= 0)
		return false;
	in.seekg(0, std::ios::beg);
	out->resize(static_cast<size_t>(size));
	return static_cast<bool>(in.read(reinterpret_cast<char *>(out->data()), size));
}

bool is_start_code_3(const std::vector<uint8_t> &b, size_t i) {
	return i + 2 < b.size() && b[i] == 0x00 && b[i + 1] == 0x00 && b[i + 2] == 0x01;
}

bool is_start_code_4(const std::vector<uint8_t> &b, size_t i) {
	return i + 3 < b.size() && b[i] == 0x00 && b[i + 1] == 0x00 && b[i + 2] == 0x00 &&
	       b[i + 3] == 0x01;
}

struct NalRange {
	size_t off;
	size_t len;
	uint8_t type;
};

struct FrameRange {
	size_t begin_nal;
	size_t end_nal;
	bool idr;
};

struct StreamChannelContext {
	int channel_id;
	int video_id;
	int width;
	int height;
	int fps;
	bool loop_file;
	bool sync_to_idr_on_open;
	bool inject_sps_pps_on_idr;
	int startup_prime_frames;
	int log_every_frames;
	int idle_sleep_ms;
	bool mjpeg_mode;
	std::string h264_path;
	std::vector<uint8_t> bitstream;
	/** MJPEG: SOI-split segments (offset, length) into bitstream. */
	std::vector<std::pair<size_t, size_t>> mjpeg_ranges;
	/** MJPEG: directory mode, one JPEG file per frame. */
	std::vector<std::vector<uint8_t>> mjpeg_frames;
	bool pip_enable;
	std::string pip_overlay_path;
	int pip_x;
	int pip_y;
	int pip_w;
	int pip_h;
	int pip_jpeg_quality;
	std::vector<NalRange> nals;
	std::vector<FrameRange> frames;
	size_t sps_idx;
	size_t pps_idx;
	size_t first_idr_frame_idx;
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

std::vector<NalRange> split_annexb_nals(const std::vector<uint8_t> &buf) {
	std::vector<NalRange> out;
	size_t i = 0;
	while (i + 3 < buf.size()) {
		size_t sc_len = 0;
		if (is_start_code_4(buf, i))
			sc_len = 4;
		else if (is_start_code_3(buf, i))
			sc_len = 3;
		else {
			i++;
			continue;
		}

		size_t nal_start = i + sc_len;
		size_t j = nal_start;
		while (j + 3 < buf.size()) {
			if (is_start_code_4(buf, j) || is_start_code_3(buf, j))
				break;
			j++;
		}
		if (j + 3 >= buf.size())
			j = buf.size();
		if (j > nal_start) {
			NalRange n;
			n.off = i;
			n.len = j - i;
			n.type = buf[nal_start] & 0x1F;
			out.push_back(n);
		}
		i = j;
	}
	return out;
}

bool is_vcl_nal(uint8_t type) {
	return type >= 1 && type <= 5;
}

std::vector<FrameRange> build_frames_from_nals(const std::vector<NalRange> &nals) {
	std::vector<FrameRange> frames;
	size_t cur_begin = static_cast<size_t>(-1);
	bool cur_idr = false;

	for (size_t i = 0; i < nals.size(); i++) {
		bool vcl = is_vcl_nal(nals[i].type);
		if (vcl) {
			if (cur_begin != static_cast<size_t>(-1)) {
				FrameRange f;
				f.begin_nal = cur_begin;
				f.end_nal = i;
				f.idr = cur_idr;
				frames.push_back(f);
			}
			cur_begin = i;
			cur_idr = (nals[i].type == 5);
		}
	}

	if (cur_begin != static_cast<size_t>(-1)) {
		FrameRange f;
		f.begin_nal = cur_begin;
		f.end_nal = nals.size();
		f.idr = cur_idr;
		frames.push_back(f);
	}

	return frames;
}

void append_nal(std::vector<uint8_t> *out, const std::vector<uint8_t> &bitstream, const NalRange &n) {
	size_t old = out->size();
	out->resize(old + n.len);
	std::memcpy(out->data() + old, bitstream.data() + n.off, n.len);
}

bool load_channel_stream(StreamChannelContext *ch) {
	if (ch->mjpeg_mode) {
		ch->sps_idx = static_cast<size_t>(-1);
		ch->pps_idx = static_cast<size_t>(-1);
		ch->first_idr_frame_idx = 0;
		ch->nals.clear();
		ch->frames.clear();
		ch->mjpeg_ranges.clear();
		ch->mjpeg_frames.clear();

		if (is_dir_path(ch->h264_path)) {
			ch->bitstream.clear();
			return load_mjpeg_frames_from_dir(ch->h264_path, &ch->mjpeg_frames);
		}

		if (!read_file_all(ch->h264_path, &ch->bitstream))
			return false;
		ch->mjpeg_ranges = split_jpeg_by_soi(ch->bitstream);
		return !ch->mjpeg_ranges.empty();
	}

	ch->mjpeg_ranges.clear();
	ch->mjpeg_frames.clear();
	ch->sps_idx = static_cast<size_t>(-1);
	ch->pps_idx = static_cast<size_t>(-1);
	ch->first_idr_frame_idx = static_cast<size_t>(-1);
	if (!read_file_all(ch->h264_path, &ch->bitstream))
		return false;
	ch->nals = split_annexb_nals(ch->bitstream);
	if (ch->nals.empty())
		return false;
	ch->frames = build_frames_from_nals(ch->nals);
	if (ch->frames.empty())
		return false;
	for (size_t i = 0; i < ch->nals.size(); i++) {
		if (ch->nals[i].type == 7 && ch->sps_idx == static_cast<size_t>(-1))
			ch->sps_idx = i;
		if (ch->nals[i].type == 8 && ch->pps_idx == static_cast<size_t>(-1))
			ch->pps_idx = i;
	}
	for (size_t i = 0; i < ch->frames.size(); i++) {
		if (ch->frames[i].idr) {
			ch->first_idr_frame_idx = i;
			break;
		}
	}
	if (ch->first_idr_frame_idx == static_cast<size_t>(-1))
		ch->first_idr_frame_idx = 0;
	return true;
}

void channel_worker(StreamChannelContext ch) {
	try {
	std::vector<uint8_t> frame_buf;
	int active_video_id = ch.video_id;
	size_t frame_idx = ch.mjpeg_mode ? 0 : ch.first_idr_frame_idx;
	size_t sent_frames = 0;
	bool stream_was_on = false;
	int startup_prime_left = 0;
	const auto frame_interval = std::chrono::microseconds(1000000 / ch.fps);
	auto last_tick = std::chrono::steady_clock::now();
	const size_t nframes = ch.mjpeg_mode ? (!ch.mjpeg_frames.empty() ? ch.mjpeg_frames.size() : ch.mjpeg_ranges.size())
	                                     : ch.frames.size();

	pip_helper_t *pip = nullptr;
	if (ch.mjpeg_mode && ch.pip_enable) {
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
		pip = pip_helper_create(ch.channel_id, &pcfg);
		if (!pip) {
			log_msg(LOG_ERROR, "pip_helper_create failed (ch=%d): %s", ch.channel_id, pip_helper_last_error());
			if (ch.stats)
				ch.stats->stream_on.store(0);
			return;
		}
		log_msg(LOG_INFO, "pip: ch=%d helper=%s overlay=%s", ch.channel_id, pip_helper_version(),
		        ch.pip_overlay_path.c_str());
	}

	while (g_run.load()) {
		// Re-resolve video_id by channel sequence after USB replug/re-enumeration.
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

		// Per-channel stream gate: true only after this channel gets COMMIT/STREAMON.
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
			if (ch.sync_to_idr_on_open)
				frame_idx = ch.mjpeg_mode ? 0 : ch.first_idr_frame_idx;
			startup_prime_left = ch.startup_prime_frames;
			last_tick = std::chrono::steady_clock::now();
			stream_was_on = true;
		}

		size_t send_frame_idx = frame_idx;
		if (startup_prime_left > 0)
			send_frame_idx = ch.mjpeg_mode ? 0 : ch.first_idr_frame_idx;

		if (ch.mjpeg_mode) {
			if (pip) {
				const uint8_t *bg_ptr = nullptr;
				size_t bg_len = 0;
				if (!ch.mjpeg_frames.empty()) {
					auto &img = ch.mjpeg_frames[send_frame_idx];
					bg_ptr = img.data();
					bg_len = img.size();
				} else {
					const auto &seg = ch.mjpeg_ranges[send_frame_idx];
					bg_ptr = ch.bitstream.data() + seg.first;
					bg_len = seg.second;
				}
				const uint8_t *pip_out = nullptr;
				size_t pip_out_len = 0;
				if (pip_helper_composite_mjpeg(pip, bg_ptr, bg_len, &pip_out, &pip_out_len) != 0) {
					if (ch.stats)
						ch.stats->error_count.fetch_add(1);
				} else {
					if (g_my_uvc_ctx)
						my_uvc_submit_mjpeg(g_my_uvc_ctx, ch.channel_id, pip_out, pip_out_len);
					sent_frames++;
					if (ch.stats)
						ch.stats->total_frames.fetch_add(1);
				}
			} else if (!ch.mjpeg_frames.empty()) {
				auto &img = ch.mjpeg_frames[send_frame_idx];
				void *ptr = img.data();
				if (g_my_uvc_ctx)
					my_uvc_submit_mjpeg(g_my_uvc_ctx, ch.channel_id, ptr, img.size());
				sent_frames++;
				if (ch.stats)
					ch.stats->total_frames.fetch_add(1);
			} else {
				const auto &seg = ch.mjpeg_ranges[send_frame_idx];
				void *ptr = ch.bitstream.data() + seg.first;
				if (g_my_uvc_ctx)
					my_uvc_submit_mjpeg(g_my_uvc_ctx, ch.channel_id, ptr, seg.second);
				sent_frames++;
				if (ch.stats)
					ch.stats->total_frames.fetch_add(1);
			}
		} else {
			const FrameRange &f = ch.frames[send_frame_idx];
			frame_buf.clear();
			if (ch.inject_sps_pps_on_idr && f.idr && ch.sps_idx != static_cast<size_t>(-1) &&
			    ch.pps_idx != static_cast<size_t>(-1)) {
				append_nal(&frame_buf, ch.bitstream, ch.nals[ch.sps_idx]);
				append_nal(&frame_buf, ch.bitstream, ch.nals[ch.pps_idx]);
			}
			for (size_t i = f.begin_nal; i < f.end_nal; i++)
				append_nal(&frame_buf, ch.bitstream, ch.nals[i]);

			if (!frame_buf.empty()) {
				void *ptr = const_cast<uint8_t *>(frame_buf.data());
				if (g_my_uvc_ctx)
					my_uvc_submit_h264(g_my_uvc_ctx, ch.channel_id, ptr, frame_buf.size());
				sent_frames++;
				if (ch.stats)
					ch.stats->total_frames.fetch_add(1);
			} else if (ch.stats) {
				ch.stats->error_count.fetch_add(1);
			}
		}

		auto now = std::chrono::steady_clock::now();
		auto due = last_tick + frame_interval;
		if (now < due)
			std::this_thread::sleep_for(due - now);
		last_tick = std::chrono::steady_clock::now();

		if (startup_prime_left > 0) {
			startup_prime_left--;
			if (startup_prime_left == 0) {
				frame_idx = ch.mjpeg_mode ? 1 : (ch.first_idr_frame_idx + 1);
				if (frame_idx >= nframes) {
					if (ch.loop_file)
						frame_idx = 0;
					else
						break;
				}
				log_msg(LOG_DEBUG,
				        "channel %d startup priming finished (video_id=%d, repeated=%d)",
				        ch.channel_id, active_video_id, ch.startup_prime_frames);
			}
		} else {
			frame_idx++;
			if (frame_idx >= nframes) {
				if (ch.loop_file)
					frame_idx = 0;
				else
					break;
			}
		}

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
	             "Usage: %s [-c dir|file] [--codec h264|mjpeg] [--channels n] [--file path|dir] [--width w] "
	             "[--height h] [--fps fps] "
	             "[--size WxH] "
	             "[--pip-enable 0|1] [--pip-overlay path] [--pip-x n] [--pip-y n] [--pip-w n] [--pip-h n] "
	             "[--pip-jpeg-quality 1-100] "
	             "[--log-every n] [--log-level 0|1|2] [--stats-enable 0|1] "
	             "[--stats-interval sec] [--startup-prime-frames n]\n"
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

	std::string verr;
	if (!uvctest::validate_config(&cfg, &verr)) {
		std::fprintf(stderr, "%s\n", verr.c_str());
		return 1;
	}

	g_log_level.store(cfg.log_level);

	log_msg(LOG_INFO,
	        "config: codec=%s file=%s channels=%d width=%d height=%d fps=%d loop=%d prefer_host_fps=%d "
	        "sync_to_idr_on_open=%d inject_sps_pps_on_idr=%d log_every_frames=%d idle_sleep_ms=%d "
	        "startup_prime_frames=%d log_level=%d stats_enable=%d stats_interval_sec=%d",
	        cfg.video_codec.c_str(), cfg.h264_path.c_str(), cfg.channels, cfg.width, cfg.height, cfg.fps,
	        cfg.loop_file ? 1 : 0, cfg.prefer_host_fps ? 1 : 0, cfg.sync_to_idr_on_open ? 1 : 0,
	        cfg.inject_sps_pps_on_idr ? 1 : 0, cfg.log_every_frames, cfg.idle_sleep_ms,
	        cfg.startup_prime_frames, cfg.log_level, cfg.stats_enable ? 1 : 0, cfg.stats_interval_sec);
	if (cfg.pip_enable)
		log_msg(LOG_INFO, "config: pip=1 overlay=%s rect=%dx%d@%d,%d quality=%d", cfg.pip_overlay_path.c_str(),
		        cfg.pip_w, cfg.pip_h, cfg.pip_x, cfg.pip_y, cfg.pip_jpeg_quality);
	log_msg(LOG_DEBUG, "pip_helper: %s", pip_helper_version());

	/*
	 * Follow rkipc default: event-driven uvc_control mode (flags=0).
	 * This allows add/remove uevents to trigger video-id rebuild after USB replug.
	 */
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

	std::vector<StreamChannelContext> channels;
	std::vector<std::shared_ptr<ChannelStats>> stats_list;
	channels.reserve(static_cast<size_t>(cfg.channels));
	stats_list.reserve(static_cast<size_t>(cfg.channels));
	for (int i = 0; i < cfg.channels; i++) {
		StreamChannelContext ch{};
		ch.channel_id = i;
		ch.video_id = my_uvc_channel_video_id(i);
		ch.width = cfg.width;
		ch.height = cfg.height;
		ch.fps = cfg.channel_fps[i] > 0 ? cfg.channel_fps[i] : cfg.fps;
		ch.loop_file = cfg.loop_file;
		ch.sync_to_idr_on_open = cfg.sync_to_idr_on_open;
		ch.inject_sps_pps_on_idr = cfg.inject_sps_pps_on_idr;
		ch.startup_prime_frames = cfg.startup_prime_frames;
		ch.log_every_frames = cfg.log_every_frames;
		ch.idle_sleep_ms = cfg.idle_sleep_ms;
		ch.mjpeg_mode = (cfg.video_codec == "mjpeg");
		ch.pip_enable = cfg.pip_enable;
		ch.pip_overlay_path = cfg.pip_overlay_path;
		ch.pip_x = cfg.pip_x;
		ch.pip_y = cfg.pip_y;
		ch.pip_w = cfg.pip_w;
		ch.pip_h = cfg.pip_h;
		ch.pip_jpeg_quality = cfg.pip_jpeg_quality;
		ch.h264_path = cfg.channel_h264_path[i].empty() ? cfg.h264_path : cfg.channel_h264_path[i];
		auto stats = std::make_shared<ChannelStats>();
		stats->channel_id = i;
		stats->video_id = ch.video_id;
		stats->target_fps = ch.fps;
		ch.stats = stats.get();
		if (ch.video_id < 0) {
			log_msg(LOG_ERROR, "no video_id for channel %d", i);
			continue;
		}
		if (!load_channel_stream(&ch)) {
			log_msg(LOG_ERROR, "load stream failed for channel %d (%s)", i, ch.h264_path.c_str());
			my_uvc_control_join(uvc, flags);
			my_uvc_formats_deinit(uvc);
			g_my_uvc_ctx = nullptr;
			my_uvc_destroy(uvc);
			return 5;
		}
		log_msg(LOG_INFO, "channel %d mapped video_id=%d fps=%d file=%s", i, ch.video_id, ch.fps,
		        ch.h264_path.c_str());
		channels.push_back(std::move(ch));
		stats_list.push_back(std::move(stats));
	}
	if (channels.empty()) {
		log_msg(LOG_ERROR, "no valid channels to run");
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
	if (cfg.stats_enable)
		stats_thread = std::thread(stats_worker, stats_list, cfg.stats_interval_sec);

	for (auto &t : workers)
		t.join();
	g_run.store(false);
	if (stats_thread.joinable())
		stats_thread.join();

	my_uvc_control_join(uvc, flags);
	my_uvc_formats_deinit(uvc);
	g_my_uvc_ctx = nullptr;
	my_uvc_destroy(uvc);
	log_msg(LOG_INFO, "exit");
	return 0;
}
