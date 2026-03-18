#include "app_config.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include "uvc_control.h"
#include "uvc-gadget.h"
#include "uvc_video.h"
}

namespace {
std::atomic<bool> g_run(true);
std::atomic<bool> g_stream_on(false);
std::atomic<bool> g_need_sync(true);
std::atomic<int> g_host_fps(0);

void on_signal(int signo) {
	(void)signo;
	g_run.store(false);
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
	int width;
	int height;
	int fps;
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

int open_uvc_cb(int width, int height, int fcc, int fps) {
	std::fprintf(stderr, "[my_uvc] open uvc %dx%d fcc=%d fps=%d\n", width, height, fcc, fps);
	if (fps > 0)
		g_host_fps.store(fps);
	g_need_sync.store(true);
	g_stream_on.store(true);
	return 0;
}

void close_uvc_cb(void) {
	std::fprintf(stderr, "[my_uvc] close uvc\n");
	g_stream_on.store(false);
}

void print_usage(const char *argv0) {
	std::fprintf(stderr,
	             "Usage: %s [-c config] [--file path] [--width w] [--height h] [--fps fps] "
	             "[--log-every n]\n",
	             argv0);
}
} // namespace

int main(int argc, char **argv) {
	std::signal(SIGINT, on_signal);
	std::signal(SIGTERM, on_signal);

	std::string config_path = "config/my_uvc.ini";
	AppConfig cli_cfg = default_app_config();
	bool cli_file = false;
	bool cli_width = false;
	bool cli_height = false;
	bool cli_fps = false;
	bool cli_log_every = false;

	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "-c" && i + 1 < argc) {
			config_path = argv[++i];
		} else if (a == "--file" && i + 1 < argc) {
			cli_cfg.h264_path = argv[++i];
			cli_file = true;
		} else if (a == "--width" && i + 1 < argc) {
			cli_cfg.width = std::stoi(argv[++i]);
			cli_width = true;
		} else if (a == "--height" && i + 1 < argc) {
			cli_cfg.height = std::stoi(argv[++i]);
			cli_height = true;
		} else if (a == "--fps" && i + 1 < argc) {
			cli_cfg.fps = std::stoi(argv[++i]);
			cli_fps = true;
		} else if (a == "--log-every" && i + 1 < argc) {
			cli_cfg.log_every_frames = std::stoi(argv[++i]);
			cli_log_every = true;
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

	if (cli_file)
		cfg.h264_path = cli_cfg.h264_path;
	if (cli_width)
		cfg.width = cli_cfg.width;
	if (cli_height)
		cfg.height = cli_cfg.height;
	if (cli_fps)
		cfg.fps = cli_cfg.fps;
	if (cli_log_every)
		cfg.log_every_frames = cli_cfg.log_every_frames;

	// v1 runs a single channel; keep explicit context to ease future multi-UVC extension.
	StreamChannelContext ch0 = {0, cfg.width, cfg.height, cfg.fps};

	std::fprintf(stderr,
	             "[my_uvc] config: file=%s width=%d height=%d fps=%d loop=%d "
	             "prefer_host_fps=%d sync_to_idr_on_open=%d inject_sps_pps_on_idr=%d "
	             "log_every_frames=%d idle_sleep_ms=%d\n",
	             cfg.h264_path.c_str(), ch0.width, ch0.height, ch0.fps, cfg.loop_file ? 1 : 0,
	             cfg.prefer_host_fps ? 1 : 0, cfg.sync_to_idr_on_open ? 1 : 0,
	             cfg.inject_sps_pps_on_idr ? 1 : 0, cfg.log_every_frames, cfg.idle_sleep_ms);

	std::vector<uint8_t> bitstream;
	if (!read_file_all(cfg.h264_path, &bitstream)) {
		std::fprintf(stderr, "[my_uvc] error: cannot read %s\n", cfg.h264_path.c_str());
		return 2;
	}

	std::vector<NalRange> nals = split_annexb_nals(bitstream);
	if (nals.empty()) {
		std::fprintf(stderr, "[my_uvc] error: no Annex-B NAL found in %s\n", cfg.h264_path.c_str());
		return 3;
	}
	std::vector<FrameRange> frames = build_frames_from_nals(nals);
	if (frames.empty()) {
		std::fprintf(stderr, "[my_uvc] error: no VCL frame found in %s\n", cfg.h264_path.c_str());
		return 3;
	}

	size_t sps_idx = static_cast<size_t>(-1);
	size_t pps_idx = static_cast<size_t>(-1);
	size_t first_idr_frame_idx = static_cast<size_t>(-1);
	for (size_t i = 0; i < nals.size(); i++) {
		if (nals[i].type == 7 && sps_idx == static_cast<size_t>(-1))
			sps_idx = i;
		if (nals[i].type == 8 && pps_idx == static_cast<size_t>(-1))
			pps_idx = i;
	}
	for (size_t i = 0; i < frames.size(); i++) {
		if (frames[i].idr) {
			first_idr_frame_idx = i;
			break;
		}
	}
	if (first_idr_frame_idx == static_cast<size_t>(-1)) {
		std::fprintf(stderr, "[my_uvc] warning: no IDR found, decode may be unstable\n");
		first_idr_frame_idx = 0;
	}
	if (sps_idx == static_cast<size_t>(-1) || pps_idx == static_cast<size_t>(-1))
		std::fprintf(stderr, "[my_uvc] warning: missing SPS/PPS in source file\n");

	const uint32_t flags = UVC_CONTROL_CHECK_STRAIGHT;
	uvc_formats_init("H.264", ch0.width, ch0.height);
	register_uvc_open_camera(open_uvc_cb);
	register_uvc_close_camera(close_uvc_cb);

	if (uvc_control_run(flags) != 0) {
		std::fprintf(stderr,
		             "[my_uvc] error: uvc_control_run failed, run usb config script first\n");
		uvc_formats_deinit();
		return 4;
	}

	const auto default_frame_interval = std::chrono::microseconds(1000000 / ch0.fps);
	size_t frame_idx = 0;
	size_t sent_frames = 0;
	auto last_tick = std::chrono::steady_clock::now();
	bool stream_was_on = false;
	std::vector<uint8_t> frame_buf;

	while (g_run.load()) {
		if (!g_stream_on.load()) {
			stream_was_on = false;
			std::this_thread::sleep_for(std::chrono::milliseconds(cfg.idle_sleep_ms));
			continue;
		}

		if (!stream_was_on) {
			if (cfg.sync_to_idr_on_open)
				g_need_sync.store(true);
			stream_was_on = true;
		}

		if (g_need_sync.exchange(false)) {
			frame_idx = first_idr_frame_idx;
		}

		const FrameRange &f = frames[frame_idx];
		frame_buf.clear();
		if (cfg.inject_sps_pps_on_idr && f.idr && sps_idx != static_cast<size_t>(-1) &&
		    pps_idx != static_cast<size_t>(-1)) {
			const NalRange &sps = nals[sps_idx];
			const NalRange &pps = nals[pps_idx];
			append_nal(&frame_buf, bitstream, sps);
			append_nal(&frame_buf, bitstream, pps);
		}
		for (size_t i = f.begin_nal; i < f.end_nal; i++)
			append_nal(&frame_buf, bitstream, nals[i]);

		if (!frame_buf.empty()) {
			void *ptr = const_cast<uint8_t *>(frame_buf.data());
			uvc_read_camera_buffer(ptr, -1, frame_buf.size(), nullptr, 0);
			sent_frames++;
		}

		int host_fps = g_host_fps.load();
		auto frame_interval = default_frame_interval;
		if (cfg.prefer_host_fps && host_fps > 0)
			frame_interval = std::chrono::microseconds(1000000 / host_fps);
		auto now = std::chrono::steady_clock::now();
		auto due = last_tick + frame_interval;
		if (now < due)
			std::this_thread::sleep_for(due - now);
		last_tick = std::chrono::steady_clock::now();

		frame_idx++;
		if (frame_idx >= frames.size()) {
			if (cfg.loop_file) {
				frame_idx = 0;
			} else {
				break;
			}
		}

		if (cfg.log_every_frames > 0 && (sent_frames % static_cast<size_t>(cfg.log_every_frames)) == 0) {
			std::fprintf(stderr, "[my_uvc] sent frames=%zu frame_idx=%zu host_fps=%d\n",
			             sent_frames, frame_idx, host_fps);
		}
	}

	uvc_control_join(flags);
	uvc_formats_deinit();
	std::fprintf(stderr, "[my_uvc] exit\n");
	return 0;
}
