## Context

Currently, the person tracking pipeline crops the detected bounding box of a person and passes it directly to the compositing layer. The compositing layer then scales the cropped NV12 image using RK hardware/software scaler to match the display grid tile size. Because grid tiles have different aspect ratios (dependent on layout rows and columns) from the typical human bounding box, this direct scaling causes noticeable stretching. Furthermore, the head is typically positioned exactly at the top edge of the crop box, leaving no headroom.

## Goals / Non-Goals

**Goals:**
- Eliminate stretching of tracked person images in the grid tiles.
- Center the person's head vertically with natural headroom.
- Retain existing RGA NV12 layout alignment requirements.

**Non-Goals:**
- Modify the grid layout coordinates or rendering implementation in the `pip_helper` library.
- Implement complex head/face-specific NPU detection (continue using YOLO bounding box).

## Decisions

### Decision 1: Calculate Target Aspect Ratio in Tracker
We will calculate the target display aspect ratio inside `PersonTracker::Update` using the canvas dimensions (`vw`, `vh`) and `max_tiles`.
- **Rationale**: Keeps the cropping aspect ratio consistent with the maximum configured display slot ratio.
- **Alternatives Considered**: Calculating the ratio dynamically based on `n_active` at runtime. This was rejected because it would cause jarring size jumps in the crop boxes whenever people enter or exit the camera view.

### Decision 2: Crop Box Expansion
For each tracked slot, we will multiply the raw bounding box size by a padding factor (1.15) to get a base size, then adjust the width or height to match the target grid aspect ratio:
- If the aspect ratio of the base box is narrower than the grid cell, we expand the crop width.
- If it is wider, we expand the crop height.
- **Rationale**: Ensures the entire person bounding box is included inside the crop box without any portion of the body being clipped.

### Decision 3: Upward Vertical Center Shift
We will shift the crop box center upwards by 12% of the detected person height.
- **Rationale**: Places the person's head more centrally in the upper half of the crop box, mimicking standard photographic framing (mid-shots/portraits) and providing headroom.

## Risks / Trade-offs

- **[Risk]** The upward shift causes the crop box to exceed the top of the video frame.
  - **Mitigation**: The existing coordinate bounds clamping in `GetAlignedCropBoxCentered` will automatically clamp `top` to `0` and adjust the position, keeping the crop box inside the frame.
- **[Risk]** Scaling height for very wide bounding boxes (e.g. multiple people or horizontal objects) could result in extremely large crop heights.
  - **Mitigation**: Height/width are clamped to the canvas dimensions `vh` and `vw` to prevent out-of-bounds crops.
