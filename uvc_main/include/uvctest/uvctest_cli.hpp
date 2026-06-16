#pragma once

#include "app_config.h"

#include <string>

/** CLI + merged ini: **`AppConfig::uvctest`** holds **[uvctest]** keys (`config/uvctest.ini`); **`libmy_uvc` / `libmy_uvc_pip`** are updated by flags that mirror those ini keys. */

namespace uvctest {

enum class CliParseResult {
	Ok,
	Help,
	BadArg,
};

struct CliState {
	AppConfig cli_cfg{};
	std::string config_path = "/userdata";
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
	bool cli_pip_tile_n_tiles = false;
	bool cli_pip_tile_test_nv12_paths = false;
	bool cli_pip_tile_test_nv12_src_w = false;
	bool cli_pip_tile_test_nv12_src_h = false;
	std::string yolo_model = "/userdata/yolov8n.rknn";
	std::string yolo_labels = "/userdata/coco_80_labels_list.txt";
	std::string camera_node = "/dev/video0";
	std::string camera_type = "rockit";
	float yolo_score_threshold = 0.60f;
	bool cli_yolo_model = false;
	bool cli_yolo_labels = false;
	bool cli_camera_node = false;
	bool cli_camera_type = false;
	bool cli_yolo_score_threshold = false;
};


/** Parse argv (after program name). On Help, caller should print usage and exit 0. */
CliParseResult parse_cli(int argc, char **argv, CliState *out);

/** Apply §3.5 precedence: CLI flags override merged ini in `cfg`. */
void merge_cli_into_config(AppConfig *cfg, const CliState &cli);

/** Clamp ranges and validate codec / pip consistency. Empty `err` means ok. */
bool validate_config(AppConfig *cfg, std::string *err);

} // namespace uvctest
