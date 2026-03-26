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
	/** "h264" | "mjpeg" — must match my_uvc_usb_config.sh -f (H.264 / MJPEG). */
	std::string video_codec;
	std::string h264_path;
	/** MJPEG only: real-time picture-in-picture (requires libjpeg at link time). */
	bool pip_enable;
	std::string pip_overlay_path;
	int pip_x;
	int pip_y;
	int pip_w;
	int pip_h;
	int pip_jpeg_quality;
	std::array<int, kMaxUvcChannels> channel_fps;
	std::array<std::string, kMaxUvcChannels> channel_h264_path;
};

AppConfig default_app_config();
bool load_app_config(const std::string &path, AppConfig *cfg, std::string *err);

#endif // MY_UVC_APP_CONFIG_H_
