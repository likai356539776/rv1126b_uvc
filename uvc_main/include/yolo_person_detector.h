#pragma once

#include <cstring>

#include "yolov8.h"
#include "image_utils.h"

namespace my_app {

class YoloPersonDetector {
 public:
  YoloPersonDetector() { memset(&ctx_, 0, sizeof(ctx_)); }

  YoloPersonDetector(const YoloPersonDetector&) = delete;
  YoloPersonDetector& operator=(const YoloPersonDetector&) = delete;

  int Init(const char* model_path, const char* labels_path = nullptr);
  void Shutdown();

  int DetectPersons(image_buffer_t* img, object_detect_result_list* od_results, double* out_infer_ms);

  rknn_app_context_t* context() { return &ctx_; }

 private:
  rknn_app_context_t ctx_;
};

}  // namespace my_app
