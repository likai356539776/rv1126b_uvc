## Why

On the Rockchip RV1126 platform, processing high-resolution video inputs (such as 4K) using CPU-allocated virtual memory buffers creates critical performance bottlenecks. This is caused by CPU cache invalidation/flushing overheads and dynamic virtual-to-physical MMU page mapping within the RGA driver during image scaling, cropping, and color conversion. Upgrading to a hardware-driven multi-channel VPSS and full-path zero-copy pipeline (via DMA-Buf FDs) is necessary to achieve high-performance, low-latency, and high-frame-rate UVC video output.

## What Changes

- **Multi-channel VPSS Hardware Configuration**: Configure VPSS to output three parallel channels in hardware: Channel 0 for high-resolution person cropping source, Channel 1 for low-resolution YOLO NPU detection source, and Channel 2 for background/overview UVC canvas resolution.
- **DMA-Buf Memory Management**: Allocate CMA/DRM physical memory buffers to hold the video frames, passing them between stages using DMA-Buf File Descriptors (FDs).
- **RKNN Zero-Copy NPU Inference**: Integrate the RKNN Zero-Copy API to feed the low-resolution VPSS Channel 1 DMA-Buf FD directly to the NPU, eliminating CPU image pre-processing and memory copies.
- **RGA Hardware Cropping & Compositing**: Re-implement NV12 person cropping and canvas compositing using RGA FD-based APIs (`wrapbuffer_fd_t` and `imcrop`/`imresize`/`imblit`), replacing all CPU memory copies (`std::memcpy`) and virtual address-based conversions.
- **MPP Zero-Copy Encoding**: Feed the final composite canvas DMA-Buf FD directly into the MPP hardware MJPEG/H.264 encoder.

## Capabilities

### New Capabilities
- `zero-copy-pipeline`: Provides multi-channel VPSS configuration, DMA-buf FD memory allocation, NPU zero-copy inference, and RGA FD-based cropping/composition.

### Modified Capabilities

## Impact

- `CameraRockitRgbReader` ([camera_rockit_vi_vo.h](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/include/camera_rockit_vi_vo.h), [camera_rockit_vi_vo.cc](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/src/camera_rockit_vi_vo.cc)): Configures VPSS for multiple channels and exposes DMA-Buf FDs instead of copying to virtual buffer pointers.
- `YoloPersonDetector` ([yolov8.h](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/vendor/yolov8/yolov8.h), [yolov8.cc](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/vendor/yolov8/rknpu2/yolov8.cc)): Adapts model inference to accept RKNN zero-copy DMA-Buf FDs.
- `PersonTracker` ([person_tracker.h](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/include/person_tracker.h), [person_tracker.cc](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/src/person_tracker.cc)): Updates slot updates and cropping logic to use RGA FD-based operations.
- `pip_helper` ([pip_helper.h](file:///home/kama/workspace/ubuntu20.04/uvc_pip/libuvc/include/my_uvc_pip/pip_helper.h), [pip_helper_mjpeg.cpp](file:///home/kama/workspace/ubuntu20.04/uvc_pip/libuvc/src/pip_helper/pip_helper_mjpeg.cpp)): Adapts background compositing and tile scaling to receive DMA-buf FDs.
