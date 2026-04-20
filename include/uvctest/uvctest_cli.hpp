#pragma once

#include "app_config.h"

#include <string>

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
};

/** Parse argv (after program name). On Help, caller should print usage and exit 0. */
CliParseResult parse_cli(int argc, char **argv, CliState *out);

/** Apply §3.5 precedence: CLI flags override merged ini in `cfg`. */
void merge_cli_into_config(AppConfig *cfg, const CliState &cli);

/** Clamp ranges and validate codec / pip consistency. Empty `err` means ok. */
bool validate_config(AppConfig *cfg, std::string *err);

} // namespace uvctest
