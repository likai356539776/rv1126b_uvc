## Why

Currently, when the YOLO person detector identifies a person, the system crops the bounding box exactly as detected and resizes it to fit the PiP layout tile. Since the bounding boxes of tracked persons have arbitrary aspect ratios (e.g., highly tall or wide) while the target PiP slots have fixed grid aspect ratios, the scaled person images appear severely stretched and distorted in the PiP output. Furthermore, when target persons are far away, their detection boxes are extremely small, leading to severe mosaic/blurriness when upscaled to fill large tiles. Conversely, in dense grid layouts (e.g., 8-tile or 16-tile configurations), the physical size of each tile is very small; if the crop margin is too loose, the tracked persons appear as tiny dots, making them difficult to recognize.

This change aims to implement a resolution-adaptive, aspect-ratio-locked, strategy-driven cropping mechanism with dynamic padding and clear-distance protection to improve the visual quality of the PiP output.

## What Changes

- **Aspect Ratio Locking**: Calculate the target tile aspect ratio based on the active tile count and output resolution, then lock the crop box aspect ratio to match this target ratio (by expanding the shorter dimension centered on the target).
- **Static Strategy-based Lookup**: Solidify a C++ static strategy map defining fixed aspect ratios, padding margins, and minimum crop width thresholds for different grid slots (1-4 tiles, 5-6 tiles, 7-8 tiles, 9 tiles, and 10-16 tiles).
- **Clear-Distance Protection**: Dynamically scale the minimum crop width threshold using the camera input size (`vw`) to prevent far-away persons (small boxes) from being over-magnified and blurred in large tiles.
- **Tight Layout Margin**: Automatically tighten the padding margins and lower the minimum crop limits in dense grid layouts (e.g., 8-tile/16-tile layouts) to maximize target representation in small slots.

## Capabilities

### New Capabilities
- `yolo-crop-aspect-ratio`: This capability implements resolution-adaptive, aspect-ratio-locked cropping of YOLO targets with distance-based clarity protection and dynamic padding tailored for different grid layout configurations.

### Modified Capabilities

## Impact

- `uvc_main/src/person_tracker.cc` and `uvc_main/include/person_tracker.h`: Implementation of the static strategy table, adaptive aspect ratio calculations, minimum size thresholds, dynamic padding, and tracking logic adjustments.
- `uvc_main/src/camera_pipeline.cc`: Passes layout-related active slot configurations or uses the existing parameter bindings seamlessly.
- Config files: No parsing changes needed, as we utilize existing `--size` and `--camera-size` parameter structures, but documentation/behavior of parameters will be impacted.
