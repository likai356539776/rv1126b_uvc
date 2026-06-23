## 1. Quick Wins Optimizations

- [x] 1.1 Implement sampled & startup-only RGB scan in `camera_pipeline.cc`
- [x] 1.2 Implement stale frame composite bypass in `streamer_pool.cc`

## 2. Core Performance Optimizations

- [x] 2.1 Implement CPU-based YUV crop copy helper in `person_tracker.cc` and replace RGA `convert_image` call for slot crops
- [x] 2.2 Implement thread-local persistent vector buffer cache for Yolov8 NPU input image in `yolov8.cc`

## 3. Verification & Testing

- [x] 3.1 Build project using `./autobuild.sh`
- [ ] 3.2 Verify CPU usage under active target tracking on the RV1126 board
