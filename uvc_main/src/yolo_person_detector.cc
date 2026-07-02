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

int YoloPersonDetector::DetectPersonsZeroCopy(void* virt_addr, int w, int h, object_detect_result_list* od_results, double* out_infer_ms) {
  letterbox_t letter_box{};
  double scale = std::min((double)ctx_.model_width / w, (double)ctx_.model_height / h);
  letter_box.scale = scale;
  letter_box.x_pad = (ctx_.model_width - w * scale) / 2.0;
  letter_box.y_pad = (ctx_.model_height - h * scale) / 2.0;

  int ret = 0;
  if (out_infer_ms) {
#if MY_APP_TIMING_ENABLED
    auto t0 = std::chrono::steady_clock::now();
    ret = inference_yolov8_model_zerocopy(&ctx_, virt_addr, &letter_box, od_results);
    *out_infer_ms = mono_elapsed_ms(t0);
#else
    *out_infer_ms = -1.0;
    ret = inference_yolov8_model_zerocopy(&ctx_, virt_addr, &letter_box, od_results);
#endif
  } else {
    ret = inference_yolov8_model_zerocopy(&ctx_, virt_addr, &letter_box, od_results);
  }
  return ret;
}

}  // namespace my_app
