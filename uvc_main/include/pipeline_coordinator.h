#pragma once

#include <atomic>
#include <memory>
#include "app_config.h"
#include "uvctest/uvctest_cli.hpp"
#include "my_uvc/my_uvc.h"
#include "camera_pipeline.h"
#include "streamer_pool.h"
#include "stats_monitor.h"

namespace my_app {

class PipelineCoordinator {
public:
	PipelineCoordinator();
	~PipelineCoordinator();

	PipelineCoordinator(const PipelineCoordinator&) = delete;
	PipelineCoordinator& operator=(const PipelineCoordinator&) = delete;

	bool Initialize(const AppConfig& cfg, const uvctest::CliState& cli);
	int Run(const std::atomic<bool>& shutdown_flag);

private:
	AppConfig cfg_;
	uvctest::CliState cli_;

	my_uvc_t* uvc_ctx_;
	CameraPipeline camera_pipeline_;
	StreamerPool streamer_pool_;
	StatsMonitor stats_monitor_;

	bool initialized_;
};

} // namespace my_app
