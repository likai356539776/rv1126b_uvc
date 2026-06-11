#include "app_config.h"
#include "my_uvc/my_uvc.h"
#include "my_uvc_pip/pip_helper.h"
#include "my_uvc_pip/pip_tile_layout.hpp"
#include "uvctest/libmy_uvc_config_bridge.hpp"
#include "uvctest/uvctest_cli.hpp"

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
	int pip_overlay_stale_timeout_ms;
	int pip_tile_n_tiles;
	int pip_tile_gap_px;
	int pip_tile_margin_px;
	/** [uvctest] pip_tile_test_nv12_paths — 逗号分隔 NV12 裸文件，与槽位顺序一致。 */
	std::string pip_tile_test_nv12_paths;
	int pip_tile_test_nv12_src_w;
	int pip_tile_test_nv12_src_h;
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

static std::vector<std::string> split_comma_paths(const std::string &s)
{
	std::vector<std::string> out;
	size_t start = 0;
	while (start < s.size()) {
		const size_t comma = s.find(',', start);
		std::string part =
		    (comma == std::string::npos) ? s.substr(start) : s.substr(start, comma - start);
		while (!part.empty() && (part.front() == ' ' || part.front() == '\t'))
			part.erase(part.begin());
		while (!part.empty() && (part.back() == ' ' || part.back() == '\t'))
			part.pop_back();
		if (!part.empty())
			out.push_back(std::move(part));
		if (comma == std::string::npos)
			break;
		start = comma + 1;
	}
	return out;
}

static size_t pip_tile_nv12_byte_size(const my_uvc_pip::PipTileRect &r)
{
	int ow = 0;
	int oh = 0;
	if (!my_uvc_pip::pip_tile_rect_nv12_plane_wh(r, &ow, &oh))
		return 0;
	return static_cast<size_t>(ow) * static_cast<size_t>(oh) * 3 / 2;
}

/**
 * 按槽顺序加载 NV12 测试文件；n_active = min(路径数, tile_n_tiles)。
 * 每个槽位支持单帧或多帧（自动循环）。失败时返回 false。
 */
static bool uvctest_load_pip_tile_nv12_test(const StreamChannelContext &ch, int tile_n_tiles,
                                            std::vector<std::vector<std::vector<uint8_t>>> *buffers,
                                            int *n_active_out)
{
	buffers->clear();
	*n_active_out = 0;
	if (tile_n_tiles <= 0 || ch.pip_tile_test_nv12_paths.empty())
		return true;

	const std::vector<std::string> paths = split_comma_paths(ch.pip_tile_test_nv12_paths);
	if (paths.empty()) {
		log_msg(LOG_ERROR, "ch=%d pip_tile_test_nv12_paths has no non-empty path segments", ch.channel_id);
		return false;
	}

	my_uvc_pip::PipTileLayoutSpec spec{};
	spec.canvas_w = ch.width;
	spec.canvas_h = ch.height;
	spec.n_tiles = tile_n_tiles;
	spec.gap_px = ch.pip_tile_gap_px;
	spec.margin_px = ch.pip_tile_margin_px;
	std::array<my_uvc_pip::PipTileRect, my_uvc_pip::kPipTileLayoutMax> rects{};
	if (my_uvc_pip::pip_tile_layout_bottom_third(spec, &rects) != tile_n_tiles) {
		log_msg(LOG_ERROR, "ch=%d pip tile layout failed for NV12 test load", ch.channel_id);
		return false;
	}

	if ((ch.pip_tile_test_nv12_src_w > 0) != (ch.pip_tile_test_nv12_src_h > 0)) {
		log_msg(LOG_ERROR, "ch=%d pip_tile_test_nv12_src_w/h must both be 0 or both >0", ch.channel_id);
		return false;
	}

	const int n_use = std::min(tile_n_tiles, static_cast<int>(paths.size()));
	for (int i = 0; i < n_use; i++) {
		size_t expected_frame_size = 0;
		if (ch.pip_tile_test_nv12_src_w > 0 && ch.pip_tile_test_nv12_src_h > 0) {
			const int sw = (ch.pip_tile_test_nv12_src_w + 1) & ~1;
			const int sh = (ch.pip_tile_test_nv12_src_h + 1) & ~1;
			expected_frame_size = static_cast<size_t>(sw) * static_cast<size_t>(sh) * 3 / 2;
		} else {
			expected_frame_size = pip_tile_nv12_byte_size(rects[static_cast<size_t>(i)]);
		}
		if (expected_frame_size == 0) {
			log_msg(LOG_ERROR, "ch=%d pip tile %d degenerate rect", ch.channel_id, i);
			return false;
		}

		std::vector<uint8_t> raw_all;
		if (!read_file_all(paths[static_cast<size_t>(i)], &raw_all)) {
			log_msg(LOG_ERROR, "ch=%d read NV12 failed tile=%d path=%s", ch.channel_id, i,
			        paths[static_cast<size_t>(i)].c_str());
			return false;
		}

		if (raw_all.size() == 0 || (raw_all.size() % expected_frame_size) != 0) {
			log_msg(LOG_ERROR, "ch=%d NV12 size mismatch tile=%d path=%s (got %zu, not a multiple of %zu)",
			        ch.channel_id, i, paths[static_cast<size_t>(i)].c_str(), raw_all.size(),
			        expected_frame_size);
			return false;
		}

		size_t n_frames = raw_all.size() / expected_frame_size;
		std::vector<std::vector<uint8_t>> tile_frames;
		tile_frames.reserve(n_frames);
		for (size_t f = 0; f < n_frames; f++) {
			auto it = raw_all.begin() + static_cast<std::vector<uint8_t>::difference_type>(f * expected_frame_size);
			tile_frames.emplace_back(it, it + static_cast<std::vector<uint8_t>::difference_type>(expected_frame_size));
		}
		buffers->push_back(std::move(tile_frames));
	}
	*n_active_out = static_cast<int>(buffers->size());
	return true;
}

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
		log_msg(LOG_INFO, "pip: ch=%d helper=%s overlay=%s", ch.channel_id, pip_helper_version(),
		        ch.pip_overlay_path.c_str());
	}

	std::vector<std::vector<std::vector<uint8_t>>> pip_tile_nv12_store;
	int pip_tile_composite_n_active = 0;
	if (pip && ch.pip_tile_n_tiles > 0 && !ch.pip_tile_test_nv12_paths.empty()) {
		if (!uvctest_load_pip_tile_nv12_test(ch, ch.pip_tile_n_tiles, &pip_tile_nv12_store,
		                                     &pip_tile_composite_n_active)) {
			pip_helper_destroy(pip);
			if (ch.stats)
				ch.stats->stream_on.store(0);
			return;
		}
		if (pip_tile_composite_n_active > 0)
			log_msg(LOG_INFO, "ch=%d pip grid NV12 test n_active=%d", ch.channel_id, pip_tile_composite_n_active);
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
				int pc = 0;
				if (pip_tile_composite_n_active > 0) {
					pip_helper_composite_opts_t po{};
					po.n_active = pip_tile_composite_n_active;

					const uint8_t *current_tiles[my_uvc_pip::kPipTileLayoutMax];
					for (int ti = 0; ti < pip_tile_composite_n_active; ti++) {
						const auto &tile_frames = pip_tile_nv12_store[static_cast<size_t>(ti)];
						size_t cur_f = send_frame_idx % tile_frames.size();
						current_tiles[ti] = tile_frames[cur_f].data();
					}
					po.tile_nv12 = current_tiles;

					int tile_sw_arr[my_uvc_pip::kPipTileLayoutMax];
					int tile_sh_arr[my_uvc_pip::kPipTileLayoutMax];
					if (ch.pip_tile_test_nv12_src_w > 0 && ch.pip_tile_test_nv12_src_h > 0) {
						for (int ti = 0; ti < pip_tile_composite_n_active; ti++) {
							tile_sw_arr[ti] = ch.pip_tile_test_nv12_src_w;
							tile_sh_arr[ti] = ch.pip_tile_test_nv12_src_h;
						}
						po.tile_src_w = tile_sw_arr;
						po.tile_src_h = tile_sh_arr;
					}
					pc = pip_helper_composite_mjpeg_ex(pip, bg_ptr, bg_len, &po, &pip_out, &pip_out_len);
				} else {
					pc = pip_helper_composite_mjpeg(pip, bg_ptr, bg_len, &pip_out, &pip_out_len);
				}
				if (pc != 0) {
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

	g_log_level.store(cfg.libmy_uvc.log_level);

	log_msg(LOG_INFO,
	        "config: codec=%s file=%s channels=%d width=%d height=%d fps=%d loop=%d prefer_host_fps=%d "
	        "sync_to_idr_on_open=%d inject_sps_pps_on_idr=%d log_every_frames=%d idle_sleep_ms=%d "
	        "startup_prime_frames=%d log_level=%d stats_enable=%d stats_interval_sec=%d",
	        cfg.libmy_uvc.video_codec.c_str(), cfg.uvctest.h264_path.c_str(), cfg.libmy_uvc.channels,
	        cfg.libmy_uvc.width, cfg.libmy_uvc.height, cfg.libmy_uvc.fps,
	        cfg.libmy_uvc.loop_file ? 1 : 0, cfg.libmy_uvc.prefer_host_fps ? 1 : 0,
	        cfg.libmy_uvc.sync_to_idr_on_open ? 1 : 0,
	        cfg.libmy_uvc.inject_sps_pps_on_idr ? 1 : 0, cfg.uvctest.log_every_frames, cfg.libmy_uvc.idle_sleep_ms,
	        cfg.libmy_uvc.startup_prime_frames, cfg.libmy_uvc.log_level, cfg.uvctest.stats_enable ? 1 : 0,
	        cfg.uvctest.stats_interval_sec);
	if (cfg.libmy_uvc_pip.pip_enable)
		log_msg(LOG_INFO, "config: pip=1 overlay=%s rect=%dx%d@%d,%d quality=%d",
		        cfg.libmy_uvc_pip.pip_overlay_path.c_str(),
		        cfg.libmy_uvc_pip.pip_w, cfg.libmy_uvc_pip.pip_h, cfg.libmy_uvc_pip.pip_x, cfg.libmy_uvc_pip.pip_y,
		        cfg.libmy_uvc_pip.pip_jpeg_quality);
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
	channels.reserve(static_cast<size_t>(cfg.libmy_uvc.channels));
	stats_list.reserve(static_cast<size_t>(cfg.libmy_uvc.channels));
	for (int i = 0; i < cfg.libmy_uvc.channels; i++) {
		StreamChannelContext ch{};
		ch.channel_id = i;
		ch.video_id = my_uvc_channel_video_id(i);
		ch.width = cfg.libmy_uvc.width;
		ch.height = cfg.libmy_uvc.height;
		ch.fps = cfg.uvctest.channel_fps[i] > 0 ? cfg.uvctest.channel_fps[i] : cfg.libmy_uvc.fps;
		ch.loop_file = cfg.libmy_uvc.loop_file;
		ch.sync_to_idr_on_open = cfg.libmy_uvc.sync_to_idr_on_open;
		ch.inject_sps_pps_on_idr = cfg.libmy_uvc.inject_sps_pps_on_idr;
		ch.startup_prime_frames = cfg.libmy_uvc.startup_prime_frames;
		ch.log_every_frames = cfg.uvctest.log_every_frames;
		ch.idle_sleep_ms = cfg.libmy_uvc.idle_sleep_ms;
		ch.mjpeg_mode = (cfg.libmy_uvc.video_codec == "mjpeg");
		ch.pip_enable = cfg.libmy_uvc_pip.pip_enable;
		ch.pip_overlay_path = cfg.libmy_uvc_pip.pip_overlay_path;
		ch.pip_x = cfg.libmy_uvc_pip.pip_x;
		ch.pip_y = cfg.libmy_uvc_pip.pip_y;
		ch.pip_w = cfg.libmy_uvc_pip.pip_w;
		ch.pip_h = cfg.libmy_uvc_pip.pip_h;
		ch.pip_jpeg_quality = cfg.libmy_uvc_pip.pip_jpeg_quality;
		ch.pip_overlay_stale_timeout_ms = cfg.libmy_uvc_pip.pip_overlay_stale_timeout_ms;
		ch.pip_tile_n_tiles = cfg.libmy_uvc_pip.pip_tile_n_tiles;
		ch.pip_tile_gap_px = cfg.libmy_uvc_pip.pip_tile_gap_px;
		ch.pip_tile_margin_px = cfg.libmy_uvc_pip.pip_tile_margin_px;
		ch.pip_tile_test_nv12_paths = cfg.uvctest.pip_tile_test_nv12_paths;
		ch.pip_tile_test_nv12_src_w = cfg.uvctest.pip_tile_test_nv12_src_w;
		ch.pip_tile_test_nv12_src_h = cfg.uvctest.pip_tile_test_nv12_src_h;
		ch.h264_path =
		    cfg.uvctest.channel_h264_path[i].empty() ? cfg.uvctest.h264_path : cfg.uvctest.channel_h264_path[i];
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
	if (cfg.uvctest.stats_enable)
		stats_thread = std::thread(stats_worker, stats_list, cfg.uvctest.stats_interval_sec);

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
