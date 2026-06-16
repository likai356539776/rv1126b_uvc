#pragma once

#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include "streamer_pool.h"

namespace my_app {

class StatsMonitor {
public:
	StatsMonitor();
	~StatsMonitor();

	StatsMonitor(const StatsMonitor&) = delete;
	StatsMonitor& operator=(const StatsMonitor&) = delete;

	void Start(std::vector<std::shared_ptr<ChannelStats>> stats_list, int interval_sec, const std::atomic<bool>& shutdown_flag);
	void Stop();

private:
	void RunLoop(std::vector<std::shared_ptr<ChannelStats>> stats, int interval_sec, const std::atomic<bool>& shutdown_flag);

	std::thread monitor_thread_;
	std::atomic<bool> is_running_{false};
};

} // namespace my_app
