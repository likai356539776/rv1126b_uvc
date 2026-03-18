#include "app_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>

namespace {
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
} // namespace

AppConfig default_app_config() {
	AppConfig cfg;
	cfg.channels = 1;
	cfg.width = 640;
	cfg.height = 480;
	cfg.fps = 25;
	cfg.log_every_frames = 120;
	cfg.idle_sleep_ms = 10;
	cfg.loop_file = true;
	cfg.prefer_host_fps = true;
	cfg.sync_to_idr_on_open = true;
	cfg.inject_sps_pps_on_idr = true;
	cfg.startup_prime_frames = 8;
	cfg.log_level = 1;
	cfg.stats_enable = true;
	cfg.stats_interval_sec = 5;
	cfg.h264_path = "/userdata/200frames_count.h264";
	for (int i = 0; i < kMaxUvcChannels; i++) {
		cfg.channel_fps[i] = cfg.fps;
		cfg.channel_h264_path[i] = cfg.h264_path;
	}
	return cfg;
}

bool load_app_config(const std::string &path, AppConfig *cfg, std::string *err) {
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

	std::string section;
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
			continue;
		}

		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;

		std::string key = to_lower(trim(line.substr(0, eq)));
		std::string val = trim(line.substr(eq + 1));

		if (section != "my_uvc" && section != "uvc")
			continue;

		if (key == "channels") {
			int v = 0;
			if (!to_int(val, &v) || v < 1 || v > kMaxUvcChannels) {
				if (err)
					*err = "invalid channels at line " + std::to_string(lineno);
				return false;
			}
			cfg->channels = v;
		} else if (key == "width") {
			int v = 0;
			if (!to_int(val, &v) || v <= 0) {
				if (err)
					*err = "invalid width at line " + std::to_string(lineno);
				return false;
			}
			cfg->width = v;
		} else if (key == "height") {
			int v = 0;
			if (!to_int(val, &v) || v <= 0) {
				if (err)
					*err = "invalid height at line " + std::to_string(lineno);
				return false;
			}
			cfg->height = v;
		} else if (key == "fps") {
			int v = 0;
			if (!to_int(val, &v) || v <= 0 || v > 120) {
				if (err)
					*err = "invalid fps at line " + std::to_string(lineno);
				return false;
			}
			cfg->fps = v;
		} else if (key == "log_every_frames") {
			int v = 0;
			if (!to_int(val, &v) || v < 0) {
				if (err)
					*err = "invalid log_every_frames at line " + std::to_string(lineno);
				return false;
			}
			cfg->log_every_frames = v;
		} else if (key == "idle_sleep_ms") {
			int v = 0;
			if (!to_int(val, &v) || v < 1 || v > 2000) {
				if (err)
					*err = "invalid idle_sleep_ms at line " + std::to_string(lineno);
				return false;
			}
			cfg->idle_sleep_ms = v;
		} else if (key == "loop_file") {
			bool v = false;
			if (!to_bool(val, &v)) {
				if (err)
					*err = "invalid loop_file at line " + std::to_string(lineno);
				return false;
			}
			cfg->loop_file = v;
		} else if (key == "prefer_host_fps") {
			bool v = false;
			if (!to_bool(val, &v)) {
				if (err)
					*err = "invalid prefer_host_fps at line " + std::to_string(lineno);
				return false;
			}
			cfg->prefer_host_fps = v;
		} else if (key == "sync_to_idr_on_open") {
			bool v = false;
			if (!to_bool(val, &v)) {
				if (err)
					*err = "invalid sync_to_idr_on_open at line " + std::to_string(lineno);
				return false;
			}
			cfg->sync_to_idr_on_open = v;
		} else if (key == "inject_sps_pps_on_idr") {
			bool v = false;
			if (!to_bool(val, &v)) {
				if (err)
					*err = "invalid inject_sps_pps_on_idr at line " + std::to_string(lineno);
				return false;
			}
			cfg->inject_sps_pps_on_idr = v;
		} else if (key == "startup_prime_frames") {
			int v = 0;
			if (!to_int(val, &v) || v < 0 || v > 120) {
				if (err)
					*err = "invalid startup_prime_frames at line " + std::to_string(lineno);
				return false;
			}
			cfg->startup_prime_frames = v;
		} else if (key == "log_level") {
			int v = 0;
			if (!to_int(val, &v) || v < 0 || v > 2) {
				if (err)
					*err = "invalid log_level at line " + std::to_string(lineno);
				return false;
			}
			cfg->log_level = v;
		} else if (key == "stats_enable") {
			bool v = false;
			if (!to_bool(val, &v)) {
				if (err)
					*err = "invalid stats_enable at line " + std::to_string(lineno);
				return false;
			}
			cfg->stats_enable = v;
		} else if (key == "stats_interval_sec") {
			int v = 0;
			if (!to_int(val, &v) || v < 1 || v > 3600) {
				if (err)
					*err = "invalid stats_interval_sec at line " + std::to_string(lineno);
				return false;
			}
			cfg->stats_interval_sec = v;
		} else if (key == "h264_path") {
			if (val.empty()) {
				if (err)
					*err = "empty h264_path at line " + std::to_string(lineno);
				return false;
			}
			cfg->h264_path = val;
		} else {
			// Per-channel overrides:
			//   channel0_h264_path=/userdata/a.h264
			//   channel1_fps=25
			std::smatch m;
			if (std::regex_match(key, m, std::regex("^channel([0-9]+)_(h264_path|fps)$"))) {
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
					cfg->channel_h264_path[ch] = val;
				} else if (field == "fps") {
					int v = 0;
					if (!to_int(val, &v) || v <= 0 || v > 120) {
						if (err)
							*err = "invalid channel_fps at line " + std::to_string(lineno);
						return false;
					}
					cfg->channel_fps[ch] = v;
				}
			}
		}
	}

	for (int i = 0; i < kMaxUvcChannels; i++) {
		if (cfg->channel_h264_path[i].empty())
			cfg->channel_h264_path[i] = cfg->h264_path;
		if (cfg->channel_fps[i] <= 0)
			cfg->channel_fps[i] = cfg->fps;
	}

	return true;
}
