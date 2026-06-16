#include "stats_monitor.h"

#include <chrono>
#include "app_log.h"

namespace my_app {

StatsMonitor::StatsMonitor() {}

StatsMonitor::~StatsMonitor() {
	Stop();
}

void StatsMonitor::Start(std::vector<std::shared_ptr<ChannelStats>> stats_list, int interval_sec, const std::atomic<bool>& shutdown_flag) {
	if (is_running_.load()) {
		return;
	}
	is_running_.store(true);
	monitor_thread_ = std::thread(&StatsMonitor::RunLoop, this, stats_list, interval_sec, std::ref(shutdown_flag));
}

void StatsMonitor::Stop() {
	if (is_running_.load()) {
		is_running_.store(false);
		if (monitor_thread_.joinable()) {
			monitor_thread_.join();
		}
	}
}

void StatsMonitor::RunLoop(std::vector<std::shared_ptr<ChannelStats>> stats, int interval_sec, const std::atomic<bool>& shutdown_flag) {
	std::vector<unsigned long long> prev(stats.size(), 0);
	while (is_running_.load() && !shutdown_flag.load()) {
		// Sleep in small increments to respond to shutdown quickly
		for (int i = 0; i < interval_sec * 10; i++) {
			if (!is_running_.load() || shutdown_flag.load()) {
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		if (!is_running_.load() || shutdown_flag.load()) {
			break;
		}

		for (size_t i = 0; i < stats.size(); i++) {
			auto &s = stats[i];
			unsigned long long total = s->total_frames.load();
			unsigned long long delta = total - prev[i];
			prev[i] = total;
			double realtime_fps = static_cast<double>(delta) / static_cast<double>(interval_sec);
			APP_LOGI("stats ch=%d video_id=%d on=%d target_fps=%d realtime_fps=%.2f total=%llu errors=%llu\n",
			         s->channel_id, s->video_id, s->stream_on.load(), s->target_fps, realtime_fps, total,
			         s->error_count.load());
		}
	}
}

} // namespace my_app
