#include "app_config.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace {

namespace fs = std::filesystem;

std::string trim(const std::string &s) {
	size_t b = 0;
	while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
		b++;
	size_t e = s.size();
	while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
		e--;
	return s.substr(b, e - b);
}

std::string to_lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

bool to_int(const std::string &s, int *out) {
	try {
		size_t idx = 0;
		int v = std::stoi(s, &idx, 10);
		if (idx != s.size())
			return false;
		*out = v;
		return true;
	} catch (...) {
		return false;
	}
}

bool to_bool(const std::string &s, bool *out) {
	std::string v = to_lower(trim(s));
	if (v == "1" || v == "true" || v == "yes" || v == "on") {
		*out = true;
		return true;
	}
	if (v == "0" || v == "false" || v == "no" || v == "off") {
		*out = false;
		return true;
	}
	return false;
}

bool to_float(const std::string &s, float *out) {
	try {
		size_t idx = 0;
		float v = std::stof(s, &idx);
		if (idx != s.size())
			return false;
		*out = v;
		return true;
	} catch (...) {
		return false;
	}
}


bool section_is_known(const std::string &sec) {
	return sec == "my_uvc" || sec == "uvc" || sec == "libmy_uvc" || sec == "libmy_uvc_pip" ||
	       sec == "uvctest";
}

/** Legacy [my_uvc]/[uvc] accepts all keys; split sections only accept their own keys. */
bool apply_config_kv(const std::string &section, const std::string &key, const std::string &val, int lineno,
                     AppConfig *cfg, std::string *err) {
	const bool legacy = (section == "my_uvc" || section == "uvc");
	const bool allow_core = legacy || section == "libmy_uvc";
	const bool allow_pip = legacy || section == "libmy_uvc_pip";
	const bool allow_test = legacy || section == "uvctest";

	auto reject_key = [&](const char *name) -> bool {
		if (legacy)
			return true;
		if (err)
			*err = std::string("key '") + name + "' is not valid in section [" + section + "] at line " +
			       std::to_string(lineno);
		return false;
	};

	bool matched = false;

	if (key == "channels") {
		if (!allow_core)
			return reject_key("channels");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v < 1 || v > kMaxUvcChannels) {
			if (err)
				*err = "invalid channels at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc.channels = v;
	} else if (key == "width") {
		if (!allow_core)
			return reject_key("width");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v <= 0) {
			if (err)
				*err = "invalid width at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc.width = v;
	} else if (key == "height") {
		if (!allow_core)
			return reject_key("height");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v <= 0) {
			if (err)
				*err = "invalid height at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc.height = v;
	} else if (key == "fps") {
		if (!allow_core)
			return reject_key("fps");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v <= 0 || v > 120) {
			if (err)
				*err = "invalid fps at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc.fps = v;
	} else if (key == "log_every_frames") {
		if (!allow_test)
			return reject_key("log_every_frames");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v < 0) {
			if (err)
				*err = "invalid log_every_frames at line " + std::to_string(lineno);
			return false;
		}
		cfg->uvctest.log_every_frames = v;
	} else if (key == "pip_tile_test_nv12_paths") {
		if (!allow_test)
			return reject_key("pip_tile_test_nv12_paths");
		matched = true;
		cfg->uvctest.pip_tile_test_nv12_paths = val;
	} else if (key == "pip_tile_test_nv12_src_w") {
		if (!allow_test)
			return reject_key("pip_tile_test_nv12_src_w");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v < 0 || v > 8192) {
			if (err)
				*err = "invalid pip_tile_test_nv12_src_w at line " + std::to_string(lineno);
			return false;
		}
		cfg->uvctest.pip_tile_test_nv12_src_w = v;
	} else if (key == "pip_tile_test_nv12_src_h") {
		if (!allow_test)
			return reject_key("pip_tile_test_nv12_src_h");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v < 0 || v > 8192) {
			if (err)
				*err = "invalid pip_tile_test_nv12_src_h at line " + std::to_string(lineno);
			return false;
		}
		cfg->uvctest.pip_tile_test_nv12_src_h = v;
	} else if (key == "idle_sleep_ms") {
		if (!allow_core)
			return reject_key("idle_sleep_ms");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v < 1 || v > 2000) {
			if (err)
				*err = "invalid idle_sleep_ms at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc.idle_sleep_ms = v;
	} else if (key == "loop_file") {
		if (!allow_core)
			return reject_key("loop_file");
		matched = true;
		bool v = false;
		if (!to_bool(val, &v)) {
			if (err)
				*err = "invalid loop_file at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc.loop_file = v;
	} else if (key == "prefer_host_fps") {
		if (!allow_core)
			return reject_key("prefer_host_fps");
		matched = true;
		bool v = false;
		if (!to_bool(val, &v)) {
			if (err)
				*err = "invalid prefer_host_fps at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc.prefer_host_fps = v;
	} else if (key == "sync_to_idr_on_open") {
		if (!allow_core)
			return reject_key("sync_to_idr_on_open");
		matched = true;
		bool v = false;
		if (!to_bool(val, &v)) {
			if (err)
				*err = "invalid sync_to_idr_on_open at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc.sync_to_idr_on_open = v;
	} else if (key == "inject_sps_pps_on_idr") {
		if (!allow_core)
			return reject_key("inject_sps_pps_on_idr");
		matched = true;
		bool v = false;
		if (!to_bool(val, &v)) {
			if (err)
				*err = "invalid inject_sps_pps_on_idr at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc.inject_sps_pps_on_idr = v;
	} else if (key == "startup_prime_frames") {
		if (!allow_core)
			return reject_key("startup_prime_frames");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v < 0 || v > 120) {
			if (err)
				*err = "invalid startup_prime_frames at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc.startup_prime_frames = v;
	} else if (key == "log_level") {
		if (!allow_core)
			return reject_key("log_level");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v < 0 || v > 2) {
			if (err)
				*err = "invalid log_level at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc.log_level = v;
	} else if (key == "stats_enable") {
		if (!allow_test)
			return reject_key("stats_enable");
		matched = true;
		bool v = false;
		if (!to_bool(val, &v)) {
			if (err)
				*err = "invalid stats_enable at line " + std::to_string(lineno);
			return false;
		}
		cfg->uvctest.stats_enable = v;
	} else if (key == "stats_interval_sec") {
		if (!allow_test)
			return reject_key("stats_interval_sec");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v < 1 || v > 3600) {
			if (err)
				*err = "invalid stats_interval_sec at line " + std::to_string(lineno);
			return false;
		}
		cfg->uvctest.stats_interval_sec = v;
	} else if (key == "yolo_score_threshold") {
		if (!allow_test)
			return reject_key("yolo_score_threshold");
		matched = true;
		float v = 0.0f;
		if (!to_float(val, &v) || v < 0.0f || v > 100.0f) {
			if (err)
				*err = "invalid yolo_score_threshold at line " + std::to_string(lineno) + " (must be [0.0, 100.0])";
			return false;
		}
		if (v > 1.0f) {
			v /= 100.0f;
		}
		cfg->uvctest.yolo_score_threshold = v;
	} else if (key == "camera_type") {
		if (!allow_test)
			return reject_key("camera_type");
		matched = true;
		cfg->uvctest.camera_type = trim(val);
	} else if (key == "camera_node") {
		if (!allow_test)
			return reject_key("camera_node");
		matched = true;
		cfg->uvctest.camera_node = trim(val);
	} else if (key == "video_codec" || key == "codec") {

		if (!allow_core)
			return reject_key("video_codec");
		matched = true;
		std::string v = to_lower(trim(val));
		if (v == "h264" || v == "264" || v == "avc") {
			cfg->libmy_uvc.video_codec = "h264";
		} else if (v == "mjpeg" || v == "jpeg" || v == "jpg" || v == "mjpg") {
			cfg->libmy_uvc.video_codec = "mjpeg";
		} else {
			if (err)
				*err = "invalid video_codec at line " + std::to_string(lineno);
			return false;
		}
	} else if (key == "pip_enable") {
		if (!allow_pip)
			return reject_key("pip_enable");
		matched = true;
		bool v = false;
		if (!to_bool(val, &v)) {
			if (err)
				*err = "invalid pip_enable at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc_pip.pip_enable = v;
	} else if (key == "pip_overlay_path") {
		if (!allow_pip)
			return reject_key("pip_overlay_path");
		matched = true;
		if (val.empty()) {
			if (err)
				*err = "empty pip_overlay_path at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc_pip.pip_overlay_path = val;
	} else if (key == "pip_x") {
		if (!allow_pip)
			return reject_key("pip_x");
		matched = true;
		int v = 0;
		if (!to_int(val, &v)) {
			if (err)
				*err = "invalid pip_x at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc_pip.pip_x = v;
	} else if (key == "pip_y") {
		if (!allow_pip)
			return reject_key("pip_y");
		matched = true;
		int v = 0;
		if (!to_int(val, &v)) {
			if (err)
				*err = "invalid pip_y at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc_pip.pip_y = v;
	} else if (key == "pip_w") {
		if (!allow_pip)
			return reject_key("pip_w");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v <= 0) {
			if (err)
				*err = "invalid pip_w at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc_pip.pip_w = v;
	} else if (key == "pip_h") {
		if (!allow_pip)
			return reject_key("pip_h");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v <= 0) {
			if (err)
				*err = "invalid pip_h at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc_pip.pip_h = v;
	} else if (key == "pip_jpeg_quality" || key == "pip_quality") {
		if (!allow_pip)
			return reject_key("pip_jpeg_quality");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v < 1 || v > 100) {
			if (err)
				*err = "invalid pip_jpeg_quality at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc_pip.pip_jpeg_quality = v;
	} else if (key == "pip_overlay_stale_timeout_ms") {
		if (!allow_pip)
			return reject_key("pip_overlay_stale_timeout_ms");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v < 0) {
			if (err)
				*err = "invalid pip_overlay_stale_timeout_ms at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc_pip.pip_overlay_stale_timeout_ms = v;
	} else if (key == "pip_tile_n_tiles") {
		if (!allow_pip)
			return reject_key("pip_tile_n_tiles");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v < 0 || v > kMaxUvcChannels) {
			if (err)
				*err = "invalid pip_tile_n_tiles at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc_pip.pip_tile_n_tiles = v;
	} else if (key == "pip_tile_gap_px") {
		if (!allow_pip)
			return reject_key("pip_tile_gap_px");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v < 0) {
			if (err)
				*err = "invalid pip_tile_gap_px at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc_pip.pip_tile_gap_px = v;
	} else if (key == "pip_tile_margin_px") {
		if (!allow_pip)
			return reject_key("pip_tile_margin_px");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v < 0) {
			if (err)
				*err = "invalid pip_tile_margin_px at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc_pip.pip_tile_margin_px = v;
	} else if (key == "pip_width_stretch_factor") {
		if (!allow_pip)
			return reject_key("pip_width_stretch_factor");
		matched = true;
		float v = 0.0f;
		if (!to_float(val, &v) || v <= 0.0f) {
			if (err)
				*err = "invalid pip_width_stretch_factor at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc_pip.pip_width_stretch_factor = v;
	} else if (key == "pip_border_enable") {
		if (!allow_pip)
			return reject_key("pip_border_enable");
		matched = true;
		bool v = false;
		if (!to_bool(val, &v)) {
			if (err)
				*err = "invalid pip_border_enable at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc_pip.pip_border_enable = v;
	} else if (key == "pip_border_radius") {
		if (!allow_pip)
			return reject_key("pip_border_radius");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v < 0) {
			if (err)
				*err = "invalid pip_border_radius at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc_pip.pip_border_radius = v;
	} else if (key == "pip_border_thickness") {
		if (!allow_pip)
			return reject_key("pip_border_thickness");
		matched = true;
		int v = 0;
		if (!to_int(val, &v) || v < 0) {
			if (err)
				*err = "invalid pip_border_thickness at line " + std::to_string(lineno);
			return false;
		}
		cfg->libmy_uvc_pip.pip_border_thickness = v;
	} else if (key == "pip_border_color") {
		if (!allow_pip)
			return reject_key("pip_border_color");
		matched = true;
		cfg->libmy_uvc_pip.pip_border_color = val;
	} else if (key == "h264_path") {
		if (!allow_test)
			return reject_key("h264_path");
		matched = true;
		if (val.empty()) {
			if (err)
				*err = "empty h264_path at line " + std::to_string(lineno);
			return false;
		}
		cfg->uvctest.h264_path = val;
	} else {
		std::smatch m;
		if (std::regex_match(key, m, std::regex("^channel([0-9]+)_(h264_path|fps)$"))) {
			if (!allow_test && !legacy) {
				if (err)
					*err = "channelN_* keys are only valid in [uvctest] or legacy [my_uvc] at line " +
					       std::to_string(lineno);
				return false;
			}
			matched = true;
			int ch = -1;
			if (!to_int(m[1].str(), &ch) || ch < 0 || ch >= kMaxUvcChannels) {
				if (err)
					*err = "invalid channel index at line " + std::to_string(lineno);
				return false;
			}
			std::string field = m[2].str();
			if (field == "h264_path") {
				if (val.empty()) {
					if (err)
						*err = "empty channel_h264_path at line " + std::to_string(lineno);
					return false;
				}
				cfg->uvctest.channel_h264_path[ch] = val;
			} else if (field == "fps") {
				int v = 0;
				if (!to_int(val, &v) || v <= 0 || v > 120) {
					if (err)
						*err = "invalid channel_fps at line " + std::to_string(lineno);
					return false;
				}
				cfg->uvctest.channel_fps[ch] = v;
			}
		}
	}

	if (!matched) {
		if (legacy)
			return true;
		if (err)
			*err = "unknown key '" + key + "' in section [" + section + "] at line " + std::to_string(lineno);
		return false;
	}
	return true;
}

bool load_app_config_stream_impl(std::istream &in, const std::string *only_section, AppConfig *cfg,
                                 std::string *err, const std::string &path_label)
{
	std::string section;
	bool known_section = false;
	std::string line;
	int lineno = 0;
	while (std::getline(in, line)) {
		lineno++;
		size_t cpos = line.find(';');
		if (cpos != std::string::npos)
			line = line.substr(0, cpos);
		cpos = line.find('#');
		if (cpos != std::string::npos)
			line = line.substr(0, cpos);

		line = trim(line);
		if (line.empty())
			continue;

		if (line.front() == '[' && line.back() == ']') {
			section = to_lower(trim(line.substr(1, line.size() - 2)));
			known_section = section_is_known(section);
			continue;
		}

		if (!known_section)
			continue;
		if (only_section && section != *only_section)
			continue;

		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;

		std::string key = to_lower(trim(line.substr(0, eq)));
		std::string val = trim(line.substr(eq + 1));

		if (!apply_config_kv(section, key, val, lineno, cfg, err)) {
			if (err)
				*err = path_label + ": " + *err;
			return false;
		}
	}
	return true;
}

bool load_app_config_stream(std::istream &in, AppConfig *cfg, std::string *err, const std::string &path_label)
{
	return load_app_config_stream_impl(in, nullptr, cfg, err, path_label);
}

bool load_app_config_file_merge(const std::string &path, AppConfig *cfg, std::string *err) {
	std::ifstream in(path);
	if (!in.is_open()) {
		if (err)
			*err = "cannot open config: " + path;
		return false;
	}
	return load_app_config_stream(in, cfg, err, path);
}

bool load_app_config_stream_section_only(std::istream &in, const std::string &want_section, AppConfig *cfg,
                                         std::string *err, const std::string &path_label)
{
	const std::string want = to_lower(trim(want_section));
	if (!section_is_known(want)) {
		if (err)
			*err = "unknown section name: " + want_section;
		return false;
	}
	return load_app_config_stream_impl(in, &want, cfg, err, path_label);
}

void finalize_channel_defaults(AppConfig *cfg) {
	for (int i = 0; i < kMaxUvcChannels; i++) {
		if (cfg->uvctest.channel_h264_path[i].empty())
			cfg->uvctest.channel_h264_path[i] = cfg->uvctest.h264_path;
		if (cfg->uvctest.channel_fps[i] <= 0)
			cfg->uvctest.channel_fps[i] = cfg->libmy_uvc.fps;
	}
}

bool load_app_config_section_from_file_body(const std::string &path, const std::string &section, AppConfig *cfg,
                                            std::string *err)
{
	if (!cfg) {
		if (err)
			*err = "cfg is null";
		return false;
	}
	std::ifstream in(path);
	if (!in.is_open()) {
		if (err)
			*err = "cannot open config: " + path;
		return false;
	}
	return load_app_config_stream_section_only(in, section, cfg, err, path);
}

} // namespace

bool load_app_config_section_from_file(const std::string &path, const std::string &section, AppConfig *cfg,
                                     std::string *err)
{
	return load_app_config_section_from_file_body(path, section, cfg, err);
}

AppConfig default_app_config() {
	AppConfig cfg;
	cfg.libmy_uvc.channels = 1;
	cfg.libmy_uvc.width = 1920;
	cfg.libmy_uvc.height = 1080;
	cfg.libmy_uvc.fps = 25;
	cfg.uvctest.log_every_frames = 120;
	cfg.libmy_uvc.idle_sleep_ms = 10;
	cfg.libmy_uvc.loop_file = true;
	cfg.libmy_uvc.prefer_host_fps = true;
	cfg.libmy_uvc.sync_to_idr_on_open = true;
	cfg.libmy_uvc.inject_sps_pps_on_idr = true;
	cfg.libmy_uvc.startup_prime_frames = 8;
	cfg.libmy_uvc.log_level = 1;
	cfg.uvctest.stats_enable = true;
	cfg.uvctest.stats_interval_sec = 5;
	cfg.libmy_uvc.video_codec = "h264";
	cfg.uvctest.h264_path = "/userdata/200frames_count.h264";
	cfg.uvctest.yolo_score_threshold = 0.60f;
	cfg.uvctest.camera_type = "rockit";
	cfg.uvctest.camera_node = "/dev/video0";
	cfg.uvctest.pip_tile_test_nv12_src_w = 0;

	cfg.uvctest.pip_tile_test_nv12_src_h = 0;
	cfg.libmy_uvc_pip.pip_enable = false;
	cfg.libmy_uvc_pip.pip_overlay_path.clear();
	cfg.libmy_uvc_pip.pip_x = 320;
	cfg.libmy_uvc_pip.pip_y = 720;
	cfg.libmy_uvc_pip.pip_w = 1280;
	cfg.libmy_uvc_pip.pip_h = 360;
	cfg.libmy_uvc_pip.pip_jpeg_quality = 85;
	cfg.libmy_uvc_pip.pip_overlay_stale_timeout_ms = 5000;
	cfg.libmy_uvc_pip.pip_tile_n_tiles = 0;
	cfg.libmy_uvc_pip.pip_tile_gap_px = 0;
	cfg.libmy_uvc_pip.pip_tile_margin_px = 0;
	cfg.libmy_uvc_pip.pip_width_stretch_factor = 1.3f;
	cfg.libmy_uvc_pip.pip_border_enable = false;
	cfg.libmy_uvc_pip.pip_border_radius = 16;
	cfg.libmy_uvc_pip.pip_border_thickness = 2;
	cfg.libmy_uvc_pip.pip_border_color = "#FFFFFF";
	for (int i = 0; i < kMaxUvcChannels; i++) {
		cfg.uvctest.channel_fps[i] = cfg.libmy_uvc.fps;
		cfg.uvctest.channel_h264_path[i] = cfg.uvctest.h264_path;
	}
	return cfg;
}

bool load_app_config(const std::string &path, AppConfig *cfg, std::string *err) {
	if (!cfg) {
		if (err)
			*err = "cfg is null";
		return false;
	}

	*cfg = default_app_config();

	std::error_code ec;
	if (fs::is_directory(path, ec)) {
		static const char *kSplit[] = {"libmy_uvc.ini", "libmy_uvc_pip.ini", "uvctest.ini"};
		int loaded = 0;
		for (const char *name : kSplit) {
			fs::path p = fs::path(path) / name;
			if (!fs::is_regular_file(p, ec))
				continue;
			if (!load_app_config_file_merge(p.string(), cfg, err))
				return false;
			loaded++;
		}
		if (loaded == 0) {
			if (err)
				*err = "config directory has none of libmy_uvc.ini, libmy_uvc_pip.ini, uvctest.ini: " + path;
			return false;
		}
		finalize_channel_defaults(cfg);
		return true;
	}

	if (!load_app_config_file_merge(path, cfg, err))
		return false;
	finalize_channel_defaults(cfg);
	return true;
}
