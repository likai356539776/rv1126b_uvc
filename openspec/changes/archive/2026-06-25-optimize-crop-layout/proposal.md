## Why

Currently, when YOLO detects a person, the cropped NV12 image of the person is composited directly into the full-screen grid slots. Because the aspect ratio of the raw bounding box (typically tall and narrow) does not match the aspect ratio of the grid slots (varying from 0.44 to 1.78 depending on layout configurations), the person's image is stretched horizontally, making them appear unnatural. Additionally, the head is positioned right at the top edge of the crop box, which looks crowded or cut off.

This change optimizes the crop bounding box by matching the grid cell's aspect ratio and shifting the center upwards to keep the head centered and natural-looking.

## What Changes

- **Aspect Ratio Matching**: Dynamically calculate the target aspect ratio of the display grid slots based on the UVC canvas size and the maximum active tiles config. Adaptively expand either the width or the height of the person crop box to match this aspect ratio before scaling, eliminating horizontal stretching.
- **Head Centering (Vertical Shift)**: Shift the vertical center of the crop box upwards by a configurable fraction (e.g. 12%) of the person's height, centering their head in the crop box and adding natural head room.
- **Tracker Interface Update**: Pass the maximum tile count from the camera pipeline config to the tracker update routine to compute the target layout cell aspect ratio.

## Capabilities

### New Capabilities
- `crop-layout`: Adjusts person crop aspect ratios and centers to match display grid properties and center the head.

### Modified Capabilities
<!-- Existing capabilities whose REQUIREMENTS are changing (not just implementation).
     Only list here if spec-level behavior changes. Each needs a delta spec file.
     Use existing spec names from openspec/specs/. Leave empty if no requirement changes. -->

## Impact

- `PersonTracker` ([person_tracker.h](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/include/person_tracker.h), [person_tracker.cc](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/src/person_tracker.cc)): Updates the `Update` interface and crop bounding box calculation logic.
- `CameraPipeline` ([camera_pipeline.cc](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/src/camera_pipeline.cc)): Passes `max_tiles` config to the tracker update function.
