## 1. Multi-channel VPSS Implementation

- [x] 1.1 Update `CameraRockitRgbReader::ViDevInit` and `VpssInit` in [camera_rockit_vi_vo.cc](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/src/camera_rockit_vi_vo.cc) to initialize VPSS Group with three active channels: Ch0 (4K), Ch1 (640x360), and Ch2 (1080p).
- [x] 1.2 Implement physical memory allocation for these channels using DRM/CMA heap and expose DMA-Buf File Descriptors (FDs).

## 2. NPU Zero-Copy Inference

- [x] 2.1 Update `YoloPersonDetector::DetectPersons` in [yolo_person_detector.cc](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/src/yolo_person_detector.cc) and [yolov8.cc](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/vendor/yolov8/rknpu2/yolov8.cc) to accept DMA-Buf FDs.
- [x] 2.2 Configure NPU inputs using RKNN Zero-Copy API (`rknn_inputs_set`) with the mapped DMA-Buf memory of VPSS Channel 1.

## 3. RGA Zero-Copy Cropping & Compositing

- [x] 3.1 Convert coordinate scaling in [person_tracker.cc](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/src/person_tracker.cc) to project bounding boxes from YOLO resolution to 4K resolution.
- [x] 3.2 Update `PersonTracker::Update` to crop人像 from the VPSS Ch0 DMA-Buf FD using RGA `wrapbuffer_fd_t` and hardware `imcrop`/`imresize` operations.
- [x] 3.3 Re-implement background compositing in [pip_helper_mjpeg.cpp](file:///home/kama/workspace/ubuntu20.04/uvc_pip/libuvc/src/pip_helper/pip_helper_mjpeg.cpp) using RGA FD-based APIs, starting with VPSS Ch2 (1080p) as the base canvas.

## 4. Verification and Benchmark

- [x] 4.1 Compile the project using `./autobuild.sh` and fix any compilation issues.
- [x] 4.2 Measure end-to-end latency, CPU consumption, and framerate comparing the virtual-address pipeline vs. the zero-copy pipeline.

## 5. Bugfix for UVC No Image Output

- [ ] 5.1 Implement dynamic VI/VPSS capture path switching (Scheme C). If `vo_enable = false`, bypass VPSS initialization/binding completely and retrieve/release frames directly from/to VI via `RK_MPI_VI_GetChnFrame`. If `vo_enable = true`, initialize VPSS and use VPSS pull path.



