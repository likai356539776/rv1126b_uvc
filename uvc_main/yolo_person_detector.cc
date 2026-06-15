#include "yolo_person_detector.h"

#include <chrono>

#include "app_log.h"
#include "my_app_timing.h"
#include "postprocess.h"
#include "time_util.hh"

namespace my_app {

int YoloPersonDetector::Init(const char* model_path, const char* labels_path) {
  if (init_post_process(labels_path) != 0) {
    APP_LOGE("init_post_process fail\n");
    return -1;
  }
  int ret = init_yolov8_model(model_path, &ctx_);
  if (ret != 0) {
    APP_LOGE("init_yolov8_model fail! ret=%d\n", ret);
    deinit_post_process();
    memset(&ctx_, 0, sizeof(ctx_));
    return -1;
  }
  return 0;
}

void YoloPersonDetector::Shutdown() {
  release_yolov8_model(&ctx_);
  memset(&ctx_, 0, sizeof(ctx_));
  deinit_post_process();
}

int YoloPersonDetector::DetectPersons(image_buffer_t* img, object_detect_result_list* od_results, double* out_infer_ms) {
  if (out_infer_ms) {
#if MY_APP_TIMING_ENABLED
    auto t0 = std::chrono::steady_clock::now();
    int ret = inference_yolov8_model(&ctx_, img, od_results);
    *out_infer_ms = mono_elapsed_ms(t0);
    return ret;
#else
    *out_infer_ms = -1.0;
#endif
  }
  return inference_yolov8_model(&ctx_, img, od_results);
}

}  // namespace my_app
