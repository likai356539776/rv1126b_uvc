#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include "camera_rockit_vi_vo.h"
#include "yolo_person_detector.h"
#include "person_tracker.h"

namespace my_app {

struct CameraPipelineConfig {
	std::string yolo_model;
	std::string yolo_labels;
	int max_tiles = 0;
};

class CameraPipeline {
public:
	CameraPipeline();
	~CameraPipeline();

	CameraPipeline(const CameraPipeline&) = delete;
	CameraPipeline& operator=(const CameraPipeline&) = delete;

	bool Initialize(const CameraPipelineConfig& cfg);
	void Start(const std::atomic<bool>& shutdown_flag);
	void Stop();

	std::shared_ptr<const FrameData> GetLatestFrame() const;

private:
	void RunLoop(const std::atomic<bool>& shutdown_flag);
	int64_t GetSteadyMs() const;

	CameraPipelineConfig cfg_;
	YoloPersonDetector yolo_;
	CameraRockitRgbReader rock_reader_;
	PersonTracker tracker_;

	std::thread camera_thread_;
	std::atomic<bool> is_running_{false};

	mutable std::mutex frame_mutex_;
	std::shared_ptr<const FrameData> latest_frame_;
};

} // namespace my_app
