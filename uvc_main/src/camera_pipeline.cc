#include "camera_pipeline.h"

#include <chrono>
#include "app_log.h"
#include "postprocess.h"
#include "camera_rockit_vi_vo.h"
#include "camera_v4l2_reader.h"

namespace my_app {

CameraPipeline::CameraPipeline() : latest_frame_(nullptr) {}

CameraPipeline::~CameraPipeline() {
	Stop();
	if (camera_reader_) {
		camera_reader_->Close();
	}
	yolo_.Shutdown();
}

bool CameraPipeline::Initialize(const CameraPipelineConfig& cfg) {
	cfg_ = cfg;

	if (yolo_.Init(cfg_.yolo_model.c_str(), cfg_.yolo_labels.c_str()) != 0) {
		APP_LOGE("camera_pipeline: YoloPersonDetector Init failed (model: %s, labels: %s)\n",
		         cfg_.yolo_model.c_str(), cfg_.yolo_labels.c_str());
		return false;
	}

	if (cfg_.camera_type == "v4l2") {
		camera_reader_ = std::make_unique<CameraV4l2RgbReader>();
	} else {
		auto rockit_reader = std::make_unique<CameraRockitRgbReader>();
		rockit_reader->SetYoloSize(yolo_.context()->model_width, yolo_.context()->model_height);
		camera_reader_ = std::move(rockit_reader);
	}

	if (camera_reader_->Open(cfg_.width, cfg_.height, cfg_.camera_node, cfg_.fps, cfg_.camera_width, cfg_.camera_height, cfg_.vo_enable) != 0) {
		APP_LOGE("camera_pipeline: CameraReader Open failed (type: %s, node: %s)\n",
		         cfg_.camera_type.c_str(), cfg_.camera_node.c_str());
		yolo_.Shutdown();
		return false;
	}

	APP_LOGI("camera_pipeline: Camera initialized successfully. Resolution: %dx%d\n",
	         camera_reader_->width(), camera_reader_->height());
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
	int vw = camera_reader_->width();
	int vh = camera_reader_->height();
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
		if (cfg_.camera_type == "rockit") {
			ZeroCopyFrame zero_copy_frame{};
			int read_r = camera_reader_->GetZeroCopyFrame(&zero_copy_frame, 1000);
			
			if (read_r == 0) {
				// Rockit Zero-copy mode
				frame_idx = zero_copy_frame.frame_index;
				object_detect_result_list od_results{};
				if (yolo_.DetectPersonsZeroCopy(zero_copy_frame.ch1_vir, zero_copy_frame.ch1_w, zero_copy_frame.ch1_h, &od_results, nullptr) != 0) {
					APP_LOGE("camera_pipeline: DetectPersonsZeroCopy failed\n");
					camera_reader_->ReleaseZeroCopyFrame(&zero_copy_frame);
					continue;
				}

				std::vector<object_detect_result> persons;
				for (int i = 0; i < od_results.count; i++) {
					if (od_results.results[i].cls_id == 0 && od_results.results[i].prop >= cfg_.yolo_score_threshold) {
						persons.push_back(od_results.results[i]);
					}
				}

				if (frame_idx < 300 && frame_idx % 30 == 0) {
					APP_LOGI("camera_pipeline (zero-copy): frame_idx=%lld, detected %d objects, %d persons\n",
					         frame_idx, od_results.count, (int)persons.size());
				}

				int64_t now_ms = GetSteadyMs();
				tracker_.Update(persons, zero_copy_frame, now_ms, cfg_.max_tiles);
				auto new_frame = tracker_.GenerateFrameData(zero_copy_frame, camera_reader_.get(), cfg_.max_tiles);

				{
					std::lock_guard<std::mutex> lock(frame_mutex_);
					latest_frame_ = new_frame;
				}
			} else {
				// GetZeroCopyFrame failed, sleep a short time and retry.
				// Do NOT fall back to ReadNextRgbInto, which only drains ch0 and clogs the VPSS channels.
				std::this_thread::sleep_for(std::chrono::milliseconds(30));
				continue;
			}
		} else {
			// Legacy/V4L2 fallback mode using virtual addresses
			int read_r_legacy = camera_reader_->ReadNextRgbInto(&camera_rgb_img, 1000);
			if (read_r_legacy != 0) {
				if (read_r_legacy == -2 || read_r_legacy == -3) {
					std::this_thread::sleep_for(std::chrono::milliseconds(30));
					continue;
				}
				APP_LOGE("camera_pipeline: ReadNextRgbInto failed, ret=%d. Device may be disconnected.\n", read_r_legacy);
				if (cfg_.camera_type == "v4l2") {
					APP_LOGI("camera_pipeline: V4L2 USB camera disconnect detected. Closing device...\n");
					camera_reader_->Close();
					APP_LOGI("camera_pipeline: Starting reconnection loop (retrying every 2 seconds)...\n");
					while (is_running_.load() && !shutdown_flag.load()) {
						std::this_thread::sleep_for(std::chrono::seconds(2));
						if (!is_running_.load() || shutdown_flag.load()) {
							break;
						}
						APP_LOGI("camera_pipeline: Attempting to reconnect to V4L2 node %s...\n", cfg_.camera_node.c_str());
						if (camera_reader_->Open(cfg_.width, cfg_.height, cfg_.camera_node, cfg_.fps, cfg_.camera_width, cfg_.camera_height) == 0) {
							APP_LOGI("camera_pipeline: Reconnection successful! Re-initializing buffers...\n");
							vw = camera_reader_->width();
							vh = camera_reader_->height();
							raw_rgb = (size_t)vw * (size_t)vh * 3u;
							rgb_buf.resize(raw_rgb);
							camera_rgb_img.width = vw;
							camera_rgb_img.height = vh;
							camera_rgb_img.size = (int)raw_rgb;
							camera_rgb_img.virt_addr = rgb_buf.data();
							break;
						}
						APP_LOGE("camera_pipeline: Reconnect attempt failed.\n");
					}
					continue;
				} else {
					std::this_thread::sleep_for(std::chrono::milliseconds(30));
					continue;
				}
			}

			frame_idx = camera_reader_->frame_index();
			const uint8_t* nv12_data = camera_reader_->GetLastNv12Data();
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
				if (od_results.results[i].cls_id == 0 && od_results.results[i].prop >= cfg_.yolo_score_threshold) {
					persons.push_back(od_results.results[i]);
				}
			}

			if (frame_idx < 300 && frame_idx % 30 == 0) {
				long long non_zero_pixels = 0;
				size_t sample_step = 256;
				size_t sampled_count = 0;
				for (size_t i = 0; i < raw_rgb; i += sample_step) {
					if (rgb_buf[i] != 0) {
						non_zero_pixels++;
					}
					sampled_count++;
				}
				double non_zero_ratio = sampled_count > 0 ? (double)non_zero_pixels / sampled_count : 0.0;
				APP_LOGI("camera_pipeline: frame_idx=%lld, RGB non-zero ratio (sampled)=%.2f%%, detected %d objects, %d persons\n",
				         frame_idx, non_zero_ratio * 100.0, od_results.count, (int)persons.size());
			}

			// Pack fake ZeroCopyFrame for fallback mode
			ZeroCopyFrame fake_frame{};
			fake_frame.frame_index = frame_idx;
			fake_frame.ch0_fd = -1;
			fake_frame.ch1_fd = -1;
			fake_frame.ch2_fd = -1;
			fake_frame.ch0_w = vw;
			fake_frame.ch0_h = vh;
			fake_frame.ch1_w = vw;
			fake_frame.ch1_h = vh;
			fake_frame.ch2_w = vw;
			fake_frame.ch2_h = vh;
			fake_frame.opaque_frame0 = const_cast<uint8_t*>(nv12_data);
			fake_frame.opaque_frame1 = const_cast<uint8_t*>(nv12_data);
			fake_frame.opaque_frame2 = const_cast<uint8_t*>(nv12_data);

			int64_t now_ms = GetSteadyMs();
			tracker_.Update(persons, fake_frame, now_ms, cfg_.max_tiles);
			auto new_frame = tracker_.GenerateFrameData(fake_frame, nullptr, cfg_.max_tiles);

			{
				std::lock_guard<std::mutex> lock(frame_mutex_);
				latest_frame_ = new_frame;
			}
		}
	}
}

} // namespace my_app
