## Why

Currently, the person tracker uses fixed, hardcoded pixel thresholds (12.0px for displacement and 16.0px for size) to define the jitter deadzone. Under high-resolution inputs (such as 4K), minor fluctuations easily break this deadzone, causing unnecessary jitters, while under low-resolution inputs (such as 720p) or for far-away targets, the fixed pixel thresholds represent a large percentage of the target's body size, making the tracking response sluggish and laggy. 

This change aims to implement a relative target-size adaptive deadzone strategy, making the jitter thresholds scale dynamically with the target's current width, thereby providing a consistent physical jitter filtering experience across all distance ranges and resolution profiles.

## What Changes

- **Relative Aspect Deadzone Calculation**: Replace hardcoded pixel deadzones with dynamic formulas based on the tracked target's real-time width (`w_slot`).
- **Physical Min Clamp (Pixel Protection)**: Enforce minimum pixel boundaries (4.0px for center displacement and 6.0px for scale size) to prevent zero-value breakdowns for extremely distant targets.
- **Adaptive Convergence and Locking**: Align the motion convergence limits (`lock_dist` and `lock_scale`) to scale proportionally with target size, cutting off the filtering long-tail cleanly and preventing "jelly effect" crawling.
- **Solidified Strategy Table Adaptation**: Incorporate default strategy tuning (e.g. relaxed deadzones for large slots, tight/sensitive deadzones for dense grids) to accommodate layout constraints.

## Capabilities

### New Capabilities
- `yolo-adaptive-deadzone`: Implements relative target-size adaptive deadzones with safety clamping and layout-specific sensitivity adjustments for robust and consistent target jitter filtering.

### Modified Capabilities

## Impact

- `uvc_main/src/person_tracker.cc` and `uvc_main/include/person_tracker.h`: Refactoring of the `CropStrategy` struct, initialization parameters, and matched slot deadzone checking logic.
