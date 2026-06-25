## Context

In our current `my_uvc` pipeline, the camera reader retrieves video frames in NV12 format, copies them, scales them to RGB for YOLOv8 NPU detection, crops detected person slots using CPU or virtual-address RGA APIs, and composites them. With high resolution camera inputs like 4K, virtual memory operations lead to severe CPU overhead (due to cache flushes and page-table rebuilds via RGA MMU) and bus bottlenecks.

## Goals / Non-Goals

**Goals:**
- Zero CPU-based memory copies for video frames.
- Zero virtual address page-table mapping overhead inside RGA/NPU.
- Implement multi-channel VPSS scaling to output 4K, 640x360, and 1080p directly.
- Re-implement RGA cropping/stitching using DMA-Buf File Descriptors.

**Non-Goals:**
- Changing the YOLOv8 model architecture or post-processing logic.
- Rewriting the UVC streaming library itself (continue using libuvc bridge).

## Decisions

### Decision 1: Use 3-channel VPSS Hardware Group
- **Rationale**: VPSS can scale and output multiple channels in hardware: Channel 0 at 4K (for high-quality crops), Channel 1 at 640x360 (for YOLO), Channel 2 at 1080p (as the background/canvas base).
- **Alternatives Considered**: Using RGA to scale down the 4K frame to 640x360 for YOLO. This was rejected because VPSS scaling runs in parallel with capture and consumes zero RGA engine bandwidth.

### Decision 2: Transition RGA APIs to wrapbuffer_fd_t
- **Rationale**: RGA's `wrapbuffer_fd_t` works with DMA-Buf FDs directly, avoiding dynamic IOMMU page-table construction and CPU cache coherency overhead.
- **Alternatives Considered**: Continuing with virtual address mapping. This was rejected due to high CPU latency on 4K.

### Decision 3: Zero-Copy RKNN API for YOLO
- **Rationale**: Map the VPSS Channel 1 DMA-Buf FD directly into the NPU using `rknn_inputs_set` / zero-copy memory.
- **Alternatives Considered**: Converting YUV to RGB via RGA and then copying to NPU. This was rejected to avoid intermediate RGB copy.

## Risks / Trade-offs

- **[Risk]** DMA-Buf allocation leaks or file descriptor exhaustions.
  - **Mitigation**: Explicitly track memory ownership and close DMA-Buf FDs immediately after usage using RAII wrappers.
- **[Risk]** Memory fragmentation on high-resolution streams.
  - **Mitigation**: Pre-allocate a fixed pool of DMA-Buf buffers (CMA heap) at start-up.
