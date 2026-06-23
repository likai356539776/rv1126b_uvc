## Why

The UVC PiP camera application suffers from high CPU usage (>40% single-core) on the RV1126 embedded SoC. This is caused by redundant processing loops, frequent virtual memory imports/releases in RGA, excessive malloc/free overhead, and duplicate compositing on duplicate/stale camera frames. We need to implement target quick wins to reduce the CPU footprint and free up processor resources for other system components.

## What Changes

- **Sampled & Startup-only RGB Scan**: Change the full-frame RGB non-zero scan to run only for the first 300 frames (startup period) and use 256-byte sampling instead of a full sequential check.
- **Duplicate Frame Composite Bypass**: Cache the last composite MJPEG frame in the streamer channel worker and reuse it if the camera frame index hasn't updated, skipping RGA composition and MPP JPEG hardware encoding.
- **CPU-based 1:1 YUV Crop Copy**: Replace RGA-based 1:1 NV12 crops in `PersonTracker` with a CPU-based row-by-row memory copy to eliminate expensive RGA virtual address mapping/unmapping overhead.
- **YOLO Input Buffer Caching**: Replace dynamic `malloc` and `free` of the NPU input buffer (1.2MB) in the Yolov8 model inference wrapper with a persistent `thread_local` cache.

## Capabilities

### New Capabilities
- `performance`: Define target CPU overhead requirements and scenarios under active target tracking on the RV1126 board.

### Modified Capabilities
- None

## Impact

- `uvc_main/src/camera_pipeline.cc`: Optimized RGB scan.
- `uvc_main/src/streamer_pool.cc`: Optimized channel workers for frame reuse.
- `uvc_main/src/person_tracker.cc`: Optimized crop extraction for tracked targets using CPU memory copy.
- `uvc_main/vendor/yolov8/rknpu2/yolov8.cc`: Cached Yolov8 NPU input buffer.
