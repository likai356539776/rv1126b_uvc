#include "app_config.h"
#include "pip_mjpeg.h"

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

extern "C" {
#include "uvc_control.h"
#include "uvc-gadget.h"
#include "uvc_video.h"
}

namespace {
std::atomic<bool> g_run(true);
std::atomic<int> g_log_level(1);
std::mutex g_log_mu;

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
	std::fprintf(stderr, "[my_uvc][%s] ", tag);
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

bool load_first_jpeg_from_dir(const std::string &dir, std::vector<uint8_t> *jpeg) {
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

bool load_jpegs_from_dir(const std::string &dir, std::vector<std::vector<uint8_t>> *jpegs) {
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

bool list_jpeg_paths_from_dir(const std::string &dir, std::vector<std::filesystem::path> *paths) {
	if (!paths)
		return false;
	paths->clear();
	std::error_code ec;
	for (const auto &ent : std::filesystem::directory_iterator(std::filesystem::path(dir), ec)) {
		if (ec)
			break;
		if (!ent.is_regular_file())
			continue;
		auto ext = to_lower_ascii(ent.path().extension().string());
		if (ext == ".jpg" || ext == ".jpeg")
			paths->push_back(ent.path());
	}
	if (ec)
		return false;
	if (paths->empty())
		return false;
	std::sort(paths->begin(), paths->end(),
	          [](const std::filesystem::path &a, const std::filesystem::path &b) {
		          return a.filename().string() < b.filename().string();
	          });
	return true;
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

	bool pip_ok = false;
	std::vector<uint8_t> pip_overlay_rgb;                 // single overlay
	std::vector<std::vector<uint8_t>> pip_overlay_rgbs;   // overlay slideshow (already scaled)
	size_t pip_overlay_idx = 0;
	std::vector<uint8_t> pip_canvas_rgb;
	std::vector<uint8_t> pip_jpeg_out;
	if (ch.mjpeg_mode && ch.pip_enable) {
		std::vector<uint8_t> ov_jpg;
		if (is_dir_path(ch.pip_overlay_path)) {
			std::vector<std::filesystem::path> ov_paths;
			if (!list_jpeg_paths_from_dir(ch.pip_overlay_path, &ov_paths)) {
				log_msg(LOG_ERROR, "pip: overlay dir has no jpg: %s (ch=%d)", ch.pip_overlay_path.c_str(),
				        ch.channel_id);
			} else {
				pip_overlay_rgbs.reserve(ov_paths.size());
				for (size_t i = 0; i < ov_paths.size(); i++) {
					const auto &p = ov_paths[i];
					std::vector<uint8_t> ov_dec;
					int ojw = 0;
					int ojh = 0;
					if (!pip_mjpeg_decode_jpeg_file_rgb(p.c_str(), &ov_dec, &ojw, &ojh)) {
						log_msg(LOG_ERROR, "pip: overlay[%zu] JPEG decode failed: %s (ch=%d) reason='%s'", i, p.c_str(),
						        ch.channel_id, pip_mjpeg_last_error());
						continue;
					}
					std::vector<uint8_t> scaled;
					scaled.resize(static_cast<size_t>(ch.pip_w * ch.pip_h * 3));
					pip_mjpeg_scale_rgb_bilinear(ov_dec.data(), ojw, ojh, scaled.data(), ch.pip_w, ch.pip_h);
					pip_overlay_rgbs.push_back(std::move(scaled));
				}
				if (!pip_overlay_rgbs.empty()) {
					pip_canvas_rgb.resize(static_cast<size_t>(ch.width * ch.height * 3));
					pip_ok = true;
					log_msg(LOG_INFO, "pip: ch=%d overlay_dir=%s frames=%zu scaled=%dx%d at (%d,%d)", ch.channel_id,
					        ch.pip_overlay_path.c_str(), pip_overlay_rgbs.size(), ch.pip_w, ch.pip_h, ch.pip_x,
					        ch.pip_y);
				}
			}
		} else if (!read_file_all(ch.pip_overlay_path, &ov_jpg)) {
			log_msg(LOG_ERROR, "pip: cannot read overlay %s (ch=%d)", ch.pip_overlay_path.c_str(), ch.channel_id);
		} else {
			std::vector<uint8_t> ov_dec;
			int ojw = 0;
			int ojh = 0;
			if (!pip_mjpeg_decode_jpeg_rgb(ov_jpg.data(), ov_jpg.size(), &ov_dec, &ojw, &ojh)) {
				log_msg(LOG_ERROR, "pip: overlay JPEG decode failed: %s (ch=%d) reason='%s'",
				        ch.pip_overlay_path.c_str(), ch.channel_id, pip_mjpeg_last_error());
			} else {
				pip_overlay_rgb.resize(static_cast<size_t>(ch.pip_w * ch.pip_h * 3));
				pip_mjpeg_scale_rgb_bilinear(ov_dec.data(), ojw, ojh, pip_overlay_rgb.data(), ch.pip_w, ch.pip_h);
				pip_canvas_rgb.resize(static_cast<size_t>(ch.width * ch.height * 3));
				pip_ok = true;
				log_msg(LOG_INFO, "pip: ch=%d overlay=%s scaled=%dx%d at (%d,%d)", ch.channel_id,
				        ch.pip_overlay_path.c_str(), ch.pip_w, ch.pip_h, ch.pip_x, ch.pip_y);
			}
		}
		if (!pip_ok) {
			log_msg(LOG_ERROR, "channel %d exiting worker (pip init failed)", ch.channel_id);
			if (ch.stats)
				ch.stats->stream_on.store(0);
			return;
		}
	}

	while (g_run.load()) {
		// Re-resolve video_id by channel sequence after USB replug/re-enumeration.
		int latest_video_id = uvc_video_id_get(static_cast<unsigned int>(ch.channel_id));
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
		bool channel_stream_on = uvc_video_get_uvc_process(active_video_id);
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
			if (pip_ok) {
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
				const uint8_t *ov_ptr = pip_overlay_rgb.data();
				if (!pip_overlay_rgbs.empty()) {
					ov_ptr = pip_overlay_rgbs[pip_overlay_idx].data();
				}

				if (!pip_mjpeg_composite_jpeg(bg_ptr, bg_len, ch.width, ch.height, ov_ptr, ch.pip_w, ch.pip_h, ch.pip_x,
				                              ch.pip_y, ch.pip_jpeg_quality, &pip_jpeg_out, &pip_canvas_rgb)) {
					if (ch.stats)
						ch.stats->error_count.fetch_add(1);
				} else {
					uvc_read_camera_buffer_by_id(pip_jpeg_out.data(), -1, pip_jpeg_out.size(), nullptr, 0,
					                             active_video_id);
					sent_frames++;
					if (ch.stats)
						ch.stats->total_frames.fetch_add(1);
				}

				if (!pip_overlay_rgbs.empty()) {
					pip_overlay_idx++;
					if (pip_overlay_idx >= pip_overlay_rgbs.size())
						pip_overlay_idx = 0;
				}
			} else if (!ch.mjpeg_frames.empty()) {
				auto &img = ch.mjpeg_frames[send_frame_idx];
				void *ptr = img.data();
				uvc_read_camera_buffer_by_id(ptr, -1, img.size(), nullptr, 0, active_video_id);
				sent_frames++;
				if (ch.stats)
					ch.stats->total_frames.fetch_add(1);
			} else {
				const auto &seg = ch.mjpeg_ranges[send_frame_idx];
				void *ptr = ch.bitstream.data() + seg.first;
				uvc_read_camera_buffer_by_id(ptr, -1, seg.second, nullptr, 0, active_video_id);
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
				uvc_read_camera_buffer_by_id(ptr, -1, frame_buf.size(), nullptr, 0, active_video_id);
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

void print_usage(const char *argv0) {
	std::fprintf(stderr,
	             "Usage: %s [-c config] [--codec h264|mjpeg] [--channels n] [--file path|dir] [--width w] "
	             "[--height h] [--fps fps] "
	             "[--size WxH] "
	             "[--pip-enable 0|1] [--pip-overlay path] [--pip-x n] [--pip-y n] [--pip-w n] [--pip-h n] "
	             "[--pip-jpeg-quality 1-100] "
	             "[--log-every n] [--log-level 0|1|2] [--stats-enable 0|1] "
	             "[--stats-interval sec] [--startup-prime-frames n]\n",
	             argv0);
}
} // namespace

int main(int argc, char **argv) {
	std::signal(SIGINT, on_signal);
	std::signal(SIGTERM, on_signal);

	std::string config_path = "/userdata/my_uvc.ini";
	AppConfig cli_cfg = default_app_config();
	bool cli_file = false;
	bool cli_channels = false;
	bool cli_width = false;
	bool cli_height = false;
	bool cli_size = false;
	bool cli_fps = false;
	bool cli_log_every = false;
	bool cli_log_level = false;
	bool cli_stats_enable = false;
	bool cli_stats_interval = false;
	bool cli_startup_prime_frames = false;
	bool cli_codec = false;
	bool cli_pip_enable = false;
	bool cli_pip_overlay = false;
	bool cli_pip_x = false;
	bool cli_pip_y = false;
	bool cli_pip_w = false;
	bool cli_pip_h = false;
	bool cli_pip_quality = false;

	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "-c" && i + 1 < argc) {
			config_path = argv[++i];
		} else if (a == "--codec" && i + 1 < argc) {
			std::string c = argv[++i];
			for (auto &x : c)
				x = static_cast<char>(std::tolower(static_cast<unsigned char>(x)));
			if (c == "h264" || c == "264" || c == "avc")
				cli_cfg.video_codec = "h264";
			else if (c == "mjpeg" || c == "jpeg" || c == "jpg" || c == "mjpg")
				cli_cfg.video_codec = "mjpeg";
			else {
				std::fprintf(stderr, "[my_uvc] invalid --codec (use h264 or mjpeg)\n");
				return 1;
			}
			cli_codec = true;
		} else if (a == "--channels" && i + 1 < argc) {
			cli_cfg.channels = std::stoi(argv[++i]);
			cli_channels = true;
		} else if (a == "--file" && i + 1 < argc) {
			cli_cfg.h264_path = argv[++i];
			cli_file = true;
		} else if (a == "--width" && i + 1 < argc) {
			cli_cfg.width = std::stoi(argv[++i]);
			cli_width = true;
		} else if (a == "--height" && i + 1 < argc) {
			cli_cfg.height = std::stoi(argv[++i]);
			cli_height = true;
		} else if (a == "--size" && i + 1 < argc) {
			int w = 0;
			int h = 0;
			std::string size = argv[++i];
			if (std::sscanf(size.c_str(), "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) {
				std::fprintf(stderr, "[my_uvc] invalid --size: %s (expected WxH)\n", size.c_str());
				return 1;
			}
			cli_cfg.width = w;
			cli_cfg.height = h;
			cli_size = true;
		} else if (a == "--fps" && i + 1 < argc) {
			cli_cfg.fps = std::stoi(argv[++i]);
			cli_fps = true;
		} else if (a == "--log-every" && i + 1 < argc) {
			cli_cfg.log_every_frames = std::stoi(argv[++i]);
			cli_log_every = true;
		} else if (a == "--log-level" && i + 1 < argc) {
			cli_cfg.log_level = std::stoi(argv[++i]);
			cli_log_level = true;
		} else if (a == "--stats-enable" && i + 1 < argc) {
			cli_cfg.stats_enable = std::stoi(argv[++i]) != 0;
			cli_stats_enable = true;
		} else if (a == "--stats-interval" && i + 1 < argc) {
			cli_cfg.stats_interval_sec = std::stoi(argv[++i]);
			cli_stats_interval = true;
		} else if (a == "--startup-prime-frames" && i + 1 < argc) {
			cli_cfg.startup_prime_frames = std::stoi(argv[++i]);
			cli_startup_prime_frames = true;
		} else if (a == "--pip-enable" && i + 1 < argc) {
			cli_cfg.pip_enable = (std::stoi(argv[++i]) != 0);
			cli_pip_enable = true;
		} else if (a == "--pip-overlay" && i + 1 < argc) {
			cli_cfg.pip_overlay_path = argv[++i];
			cli_cfg.pip_enable = true;
			cli_pip_overlay = true;
		} else if (a == "--pip-x" && i + 1 < argc) {
			cli_cfg.pip_x = std::stoi(argv[++i]);
			cli_pip_x = true;
		} else if (a == "--pip-y" && i + 1 < argc) {
			cli_cfg.pip_y = std::stoi(argv[++i]);
			cli_pip_y = true;
		} else if (a == "--pip-w" && i + 1 < argc) {
			cli_cfg.pip_w = std::stoi(argv[++i]);
			cli_pip_w = true;
		} else if (a == "--pip-h" && i + 1 < argc) {
			cli_cfg.pip_h = std::stoi(argv[++i]);
			cli_pip_h = true;
		} else if (a == "--pip-jpeg-quality" && i + 1 < argc) {
			cli_cfg.pip_jpeg_quality = std::stoi(argv[++i]);
			cli_pip_quality = true;
		} else if (a == "-h" || a == "--help") {
			print_usage(argv[0]);
			return 0;
		} else {
			print_usage(argv[0]);
			return 1;
		}
	}

	AppConfig cfg = default_app_config();
	std::string err;
	if (!load_app_config(config_path, &cfg, &err))
		std::fprintf(stderr, "[my_uvc] warning: %s, fallback to defaults\n", err.c_str());

	if (cli_file) {
		cfg.h264_path = cli_cfg.h264_path;
		/* Ini channelN_h264_path overrides global; CLI --file must win for all channels. */
		for (int i = 0; i < kMaxUvcChannels; i++)
			cfg.channel_h264_path[i] = cfg.h264_path;
	}
	if (cli_channels)
		cfg.channels = cli_cfg.channels;
	if (cli_width)
		cfg.width = cli_cfg.width;
	if (cli_height)
		cfg.height = cli_cfg.height;
	if (cli_size) {
		cfg.width = cli_cfg.width;
		cfg.height = cli_cfg.height;
	}
	if (cli_fps)
		cfg.fps = cli_cfg.fps;
	if (cli_log_every)
		cfg.log_every_frames = cli_cfg.log_every_frames;
	if (cli_log_level)
		cfg.log_level = cli_cfg.log_level;
	if (cli_stats_enable)
		cfg.stats_enable = cli_cfg.stats_enable;
	if (cli_stats_interval)
		cfg.stats_interval_sec = cli_cfg.stats_interval_sec;
	if (cli_startup_prime_frames)
		cfg.startup_prime_frames = cli_cfg.startup_prime_frames;
	if (cli_codec)
		cfg.video_codec = cli_cfg.video_codec;
	if (cli_pip_overlay) {
		cfg.pip_overlay_path = cli_cfg.pip_overlay_path;
		cfg.pip_enable = true;
	}
	if (cli_pip_enable)
		cfg.pip_enable = cli_cfg.pip_enable;
	if (cli_pip_x)
		cfg.pip_x = cli_cfg.pip_x;
	if (cli_pip_y)
		cfg.pip_y = cli_cfg.pip_y;
	if (cli_pip_w)
		cfg.pip_w = cli_cfg.pip_w;
	if (cli_pip_h)
		cfg.pip_h = cli_cfg.pip_h;
	if (cli_pip_quality)
		cfg.pip_jpeg_quality = cli_cfg.pip_jpeg_quality;
	if (cfg.video_codec != "h264" && cfg.video_codec != "mjpeg") {
		std::fprintf(stderr, "[my_uvc] invalid video_codec in config (use h264 or mjpeg)\n");
		return 1;
	}
	if (cfg.channels < 1)
		cfg.channels = 1;
	if (cfg.channels > kMaxUvcChannels)
		cfg.channels = kMaxUvcChannels;
	if (cfg.log_level < 0)
		cfg.log_level = 0;
	if (cfg.log_level > 2)
		cfg.log_level = 2;
	if (cfg.stats_interval_sec < 1)
		cfg.stats_interval_sec = 1;
	if (cfg.startup_prime_frames < 0)
		cfg.startup_prime_frames = 0;
	if (cfg.startup_prime_frames > 120)
		cfg.startup_prime_frames = 120;
	if (cfg.pip_jpeg_quality < 1)
		cfg.pip_jpeg_quality = 1;
	if (cfg.pip_jpeg_quality > 100)
		cfg.pip_jpeg_quality = 100;

	if (cfg.pip_enable) {
		if (cfg.video_codec != "mjpeg") {
			std::fprintf(stderr, "[my_uvc] pip_enable requires video_codec=mjpeg\n");
			return 1;
		}
		if (cfg.pip_overlay_path.empty()) {
			std::fprintf(stderr, "[my_uvc] pip_enable requires pip_overlay_path (ini or --pip-overlay)\n");
			return 1;
		}
		if (cfg.pip_w <= 0 || cfg.pip_h <= 0) {
			std::fprintf(stderr, "[my_uvc] pip_w and pip_h must be positive\n");
			return 1;
		}
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

	/*
	 * Follow rkipc default: event-driven uvc_control mode (flags=0).
	 * This allows add/remove uevents to trigger video-id rebuild after USB replug.
	 */
	const uint32_t flags = 0;
	{
		char buf[16] = {0};
		std::snprintf(buf, sizeof(buf), "%d", cfg.channels);
		setenv("UVC_CNT", buf, 1);
	}
	uvc_formats_init(cfg.video_codec == "mjpeg" ? "MJPEG" : "H.264", cfg.width, cfg.height);
	register_uvc_open_camera(open_uvc_cb);
	register_uvc_close_camera(close_uvc_cb);

	if (uvc_control_run(flags) != 0) {
		log_msg(LOG_ERROR, "uvc_control_run failed, run usb config script first");
		uvc_formats_deinit();
		return 4;
	}

	std::vector<StreamChannelContext> channels;
	std::vector<std::shared_ptr<ChannelStats>> stats_list;
	channels.reserve(static_cast<size_t>(cfg.channels));
	stats_list.reserve(static_cast<size_t>(cfg.channels));
	for (int i = 0; i < cfg.channels; i++) {
		StreamChannelContext ch{};
		ch.channel_id = i;
		ch.video_id = uvc_video_id_get(static_cast<unsigned int>(i));
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
			uvc_control_join(flags);
			uvc_formats_deinit();
			return 5;
		}
		log_msg(LOG_INFO, "channel %d mapped video_id=%d fps=%d file=%s", i, ch.video_id, ch.fps,
		        ch.h264_path.c_str());
		channels.push_back(std::move(ch));
		stats_list.push_back(std::move(stats));
	}
	if (channels.empty()) {
		log_msg(LOG_ERROR, "no valid channels to run");
		uvc_control_join(flags);
		uvc_formats_deinit();
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

	uvc_control_join(flags);
	uvc_formats_deinit();
	log_msg(LOG_INFO, "exit");
	return 0;
}
