#ifndef MY_UVC_APP_CONFIG_H_
#define MY_UVC_APP_CONFIG_H_

#include <array>
#include <string>

constexpr int kMaxUvcChannels = 16;

struct AppConfig {
	int channels;
	int width;
	int height;
	int fps;
	int log_every_frames;
	int idle_sleep_ms;
	bool loop_file;
	bool prefer_host_fps;
	bool sync_to_idr_on_open;
	bool inject_sps_pps_on_idr;
	int startup_prime_frames;
	int log_level;
	bool stats_enable;
	int stats_interval_sec;
	std::string h264_path;
	std::array<int, kMaxUvcChannels> channel_fps;
	std::array<std::string, kMaxUvcChannels> channel_h264_path;
};

AppConfig default_app_config();
bool load_app_config(const std::string &path, AppConfig *cfg, std::string *err);

#endif // MY_UVC_APP_CONFIG_H_
