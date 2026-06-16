#include "camera_pipeline.h"

#include <chrono>
#include "app_log.h"
#include "postprocess.h"

namespace my_app {

CameraPipeline::CameraPipeline() : latest_frame_(nullptr) {}

CameraPipeline::~CameraPipeline() {
	Stop();
	rock_reader_.Close();
	rock_reader_.ShutdownSubsystem();
	yolo_.Shutdown();
}

bool CameraPipeline::Initialize(const CameraPipelineConfig& cfg) {
	cfg_ = cfg;

	if (yolo_.Init(cfg_.yolo_model.c_str(), cfg_.yolo_labels.c_str()) != 0) {
		APP_LOGE("camera_pipeline: YoloPersonDetector Init failed (model: %s, labels: %s)\n",
		         cfg_.yolo_model.c_str(), cfg_.yolo_labels.c_str());
		return false;
	}

	my_app::RockitCameraConfig rcfg;
	rcfg.vi_pipe_id = 0;
	rcfg.vi_dev_id = 0;
	rcfg.vi_chn_id = 0;
	rcfg.width = 1920;
	rcfg.height = 1080;
	rcfg.vo_enable = true;
	rcfg.vo_layer = 0;
	rcfg.vo_dev = 0;
	rcfg.vo_chn = 0;
#if defined(RV1126B)
	rcfg.vo_intf_type = 1;
#else
	rcfg.vo_intf_type = 0;
#endif
	rcfg.vo_disp_width = 1080;
	rcfg.vo_disp_height = 1920;
	rcfg.vo_layer_no_compress = false;
	rcfg.vo_rotation_deg = 0;
	rcfg.camera_mirror = false;

	if (rock_reader_.Open(rcfg) != 0) {
		APP_LOGE("camera_pipeline: CameraRockitRgbReader Open failed\n");
		yolo_.Shutdown();
		return false;
	}

	APP_LOGI("camera_pipeline: Camera initialized successfully. Resolution: %dx%d\n",
	         rock_reader_.width(), rock_reader_.height());
	return true;
}

void CameraPipeline::Start(const std::atomic<bool>& shutdown_flag) {
	if (is_running_.load()) {
		return;
	}
	is_running_.store(true);
	camera_thread_ = std::thread(&CameraPipeline::RunLoop, this, std::ref(shutdown_flag));
}

void CameraPipeline::Stop() {
	if (is_running_.load()) {
		is_running_.store(false);
		if (camera_thread_.joinable()) {
			camera_thread_.join();
		}
	}
}

std::shared_ptr<const FrameData> CameraPipeline::GetLatestFrame() const {
	std::lock_guard<std::mutex> lock(frame_mutex_);
	return latest_frame_;
}

int64_t CameraPipeline::GetSteadyMs() const {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()
	).count();
}

void CameraPipeline::RunLoop(const std::atomic<bool>& shutdown_flag) {
	int vw = rock_reader_.width();
	int vh = rock_reader_.height();
	size_t raw_rgb = (size_t)vw * (size_t)vh * 3u;
	std::vector<uint8_t> rgb_buf(raw_rgb);

	image_buffer_t camera_rgb_img{};
	camera_rgb_img.width = vw;
	camera_rgb_img.height = vh;
	camera_rgb_img.format = IMAGE_FORMAT_RGB888;
	camera_rgb_img.size = (int)raw_rgb;
	camera_rgb_img.virt_addr = rgb_buf.data();

	long long frame_idx = 0;

	while (is_running_.load() && !shutdown_flag.load()) {
		int read_r = rock_reader_.ReadNextRgbInto(&camera_rgb_img, 1000);
		if (read_r != 0) {
			APP_LOGE("camera_pipeline: ReadNextRgbInto failed, ret=%d\n", read_r);
			std::this_thread::sleep_for(std::chrono::milliseconds(30));
			continue;
		}

		frame_idx = rock_reader_.frame_index();
		const uint8_t* nv12_data = rock_reader_.GetLastNv12Data();
		if (!nv12_data) {
			APP_LOGE("camera_pipeline: GetLastNv12Data returned null\n");
			continue;
		}

		object_detect_result_list od_results{};
		if (yolo_.DetectPersons(&camera_rgb_img, &od_results, nullptr) != 0) {
			APP_LOGE("camera_pipeline: DetectPersons failed\n");
			continue;
		}

		std::vector<object_detect_result> persons;
		for (int i = 0; i < od_results.count; i++) {
			if (od_results.results[i].cls_id == 0 && od_results.results[i].prop >= cfg_.yolo_score_threshold) { // person with score >= threshold
				persons.push_back(od_results.results[i]);
			}
		}


		if (frame_idx % 30 == 0) {
			long long non_zero_pixels = 0;
			for (size_t i = 0; i < raw_rgb; i++) {
				if (rgb_buf[i] != 0) {
					non_zero_pixels++;
				}
			}
			double non_zero_ratio = (double)non_zero_pixels / raw_rgb;
			APP_LOGI("camera_pipeline: frame_idx=%lld, RGB non-zero ratio=%.2f%%, detected %d objects, %d persons\n",
			         frame_idx, non_zero_ratio * 100.0, od_results.count, (int)persons.size());
			for (int i = 0; i < od_results.count; i++) {
				APP_LOGI("  obj[%d]: class=%d, name=%s, score=%.2f, box=[%d,%d,%d,%d]\n",
				         i, od_results.results[i].cls_id,
				         coco_cls_to_name(od_results.results[i].cls_id),
				         od_results.results[i].prop,
				         od_results.results[i].box.left,
				         od_results.results[i].box.top,
				         od_results.results[i].box.right,
				         od_results.results[i].box.bottom);
			}
		}

		int64_t now_ms = GetSteadyMs();
		tracker_.Update(persons, nv12_data, vw, vh, now_ms);

		auto new_frame = tracker_.GenerateFrameData(frame_idx, nv12_data, vw, vh, cfg_.max_tiles);

		{
			std::lock_guard<std::mutex> lock(frame_mutex_);
			latest_frame_ = new_frame;
		}
	}
}

} // namespace my_app
