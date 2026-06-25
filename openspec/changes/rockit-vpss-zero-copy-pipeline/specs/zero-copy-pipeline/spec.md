## ADDED Requirements

### Requirement: Multi-channel VPSS Output
The video capture subsystem SHALL configure VPSS to output three parallel channels in hardware: Channel 0 at original high-resolution, Channel 1 scaled to NPU input resolution, and Channel 2 scaled to target canvas output resolution.

#### Scenario: VPSS Channel Initialization
- **WHEN** the camera pipeline initializes
- **THEN** VPSS is configured with Channel 0 at 4K (3840x2160), Channel 1 at 640x360, and Channel 2 at 1080p (1920x1080) in hardware NV12 format.

### Requirement: DMA-Buf Video Frame Passing
The video capture subsystem SHALL allocate physical contiguous buffers using the DRM/CMA heap and pass frames using DMA-Buf File Descriptors (FDs) across all pipeline stages.

#### Scenario: Frame Transfer via File Descriptor
- **WHEN** a new frame is grabbed from a VPSS channel
- **THEN** the frame data is accessed and passed to the next processing stage using its DMA-Buf File Descriptor.

### Requirement: RKNN NPU Zero-Copy Inference
The YOLO person detection subsystem SHALL run NPU inference directly using the DMA-Buf File Descriptor of VPSS Channel 1 via the RKNN Zero-Copy API.

#### Scenario: Inference with mapped input memory
- **WHEN** NPU inference is invoked for a video frame
- **THEN** the VPSS Channel 1 DMA-Buf FD is mapped directly into NPU input memory without copying data via the CPU.

### Requirement: RGA Hardware Cropping and Compositing
The person tracker and composition subsystems SHALL crop person bounding boxes from VPSS Channel 0 and stitch them into the UVC canvas (obtained from VPSS Channel 2) using RGA FD-based APIs.

#### Scenario: Crop and resize with wrapbuffer_fd
- **WHEN** cropping a person slot or scaling the background
- **THEN** RGA wrapbuffer_fd_t and imresize/imcrop APIs are executed on the source/destination DMA-Buf FDs in hardware.
