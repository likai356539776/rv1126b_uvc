## Context

The UVC PiP camera application runs on the Rockchip RV1126 SoC, which has a low-power quad-core ARM Cortex-A7 CPU. In multi-person environments, tracking and cropping multiple targets causes high CPU utilization (>40% single-core). The primary bottlenecks are:
1. Frequent RGA virtual memory mapping and unmapping for 1:1 target crops.
2. Dynamic memory allocation (`malloc`/`free`) of NPU input buffers on every frame.
3. Redundant compositing and hardware encoding on duplicate or stale frames in streamer workers.

## Goals / Non-Goals

**Goals:**
- Substantially reduce CPU utilization in both idle and active target-tracking scenarios.
- Eliminate frame rate jitter and periodic CPU spikes.
- Skip redundant image scaling and hardware encoding when the camera feed does not update.
- Eliminate per-frame memory allocation overhead on the NPU thread.

**Non-Goals:**
- Full-scale refactoring of the memory manager to native DRM buffers (requires extensive camera/Rockit SDK modifications).
- Altering the core YOLO inference logic or model weights.

## Decisions

### Decision 1: CPU-based 1:1 NV12 Crop Copy
- **Choice**: Implement a CPU-based row-by-row `std::memcpy` loop for cropping tracked targets in [PersonTracker::Update](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/src/person_tracker.cc#L201).
- **Rationale**: The source bounding box and destination crop slot sizes are identical (1:1 crop). Since no scaling is required, a simple CPU-based sub-rectangle copy takes <0.1ms and completely avoids RGA's virtual address mapping/unmapping system call overhead.
- **Alternative considered**: Allocating dedicated DRM buffers for each crop slot. Rejected due to high codebase complexity and buffer pool synchronization overhead.

### Decision 2: Stale Frame Composite Bypass
- **Choice**: In [StreamerPool::ChannelWorker](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/src/streamer_pool.cc#L131-L208), track `last_processed_frame_idx` and directly resubmit the last encoded MJPEG packet if `current_frame->frame_index == last_processed_frame_idx`.
- **Rationale**: If the camera has not produced a new frame, compositing the layout and running the MPP JPEG hardware encoder is completely redundant. Reusing the previous output satisfies UVC endpoint streaming requirements while reducing CPU and NPU/MPP load to near 0 during static scenes.
- **Alternative considered**: Reducing the UVC thread polling rate. Rejected because UVC endpoints must continuously receive packets at the configured target framerate to avoid host-side streaming errors.

### Decision 3: Thread-local YOLO Input Buffer Cache
- **Choice**: In Yolov8 model inference, replace dynamic `malloc` and `free` of the 1.2MB model input image with a `thread_local` persistent `std::vector<uint8_t>` cache.
- **Rationale**: Keeps the NPU input buffer allocated across frames, avoiding expensive kernel page table updates, page faults, and buffer zeroing on every frame.
- **Alternative considered**: Allocating NPU memory buffer once in detector class context. Thread-local vector is simpler to integrate into the existing C-style yolov8 wrapper and is completely thread-safe.

### Decision 4: Subsampled and Startup-only RGB Scan
- **Choice**: Limit the debug non-zero pixels scan in [CameraPipeline::RunLoop](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/src/camera_pipeline.cc#L152-L175) to `frame_idx < 300` (first 10 seconds of UVC startup) and sample every 256th byte instead of performing a full sequential buffer scan.
- **Rationale**: The log is only useful as a startup diagnostic to check if the camera output is stable. Sampling 2.4k pixels instead of scanning 6.22M pixels reduces computational overhead to near 0, and stopping the scan after 10 seconds completely eliminates running overhead.

## Risks / Trade-offs

- **[Risk]**: CPU usage might spike if there are too many target crops.
  - **Mitigation**: The maximum crop slots (`pip_tile_n_tiles`) is limited to 16. A 1:1 NV12 crop size is small (typically <256x256). Doing 16 crops on CPU via `memcpy` takes <0.5ms total, which is significantly faster and lighter than 16 RGA MMU mappings.
- **[Risk]**: Host PC receives duplicate MJPEG frames if the camera stalls.
  - **Mitigation**: This is correct UVC behavior; the stream remains active on the host showing the last captured frame instead of stalling the USB interface.
