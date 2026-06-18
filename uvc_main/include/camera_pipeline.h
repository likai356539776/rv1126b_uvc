#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include "camera_reader.h"
#include "yolo_person_detector.h"
#include "person_tracker.h"

namespace my_app {

struct CameraPipelineConfig {
	std::string yolo_model;
	std::string yolo_labels;
	int max_tiles = 0;
	float yolo_score_threshold = 0.60f;
	std::string camera_type = "rockit";
	std::string camera_node = "/dev/video0";
	int width = 1920;
	int height = 1080;
	int fps = 25;
	int camera_width = 0;
	int camera_height = 0;
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
	std::unique_ptr<CameraReader> camera_reader_;
	PersonTracker tracker_;

	std::thread camera_thread_;
	std::atomic<bool> is_running_{false};

	mutable std::mutex frame_mutex_;
	std::shared_ptr<const FrameData> latest_frame_;
};

} // namespace my_app
