## Context

Currently, YOLO detection outputs arbitrary bounding boxes for tracked persons, which are cropped and resized to overlay onto fixed aspect ratio PiP layout tiles. This mismatch causes noticeable image stretching and distortion. Furthermore, far-away objects suffer from extreme pixelation (blurriness) when upscaled, while dense grid slots (e.g., 8-tile/16-tile) fail to make small targets distinct when too much background padding is used.

## Goals / Non-Goals

**Goals:**
- Eliminate stretching/distortion of target images inside PiP tiles by enforcing locked aspect ratio cropping.
- Protect visual clarity of far-away objects by dynamically clamping minimum crop size based on input resolution.
- Maximize target saliency in small tiles (8-tile/16-tile grids) using tight layout-specific margins.
- Implement this purely in C++ with solidified lookup tables for zero performance overhead.

**Non-Goals:**
- Dynamically recalculate tile layout geometries in `PersonTracker`.
- Support dynamic configuration via INI files (C++ compile-time tuning is preferred by the user).

## Decisions

### Decision 1: C++ Solidified Strategy Lookup Table
- **Choice**: Implement a static layout strategy table `kBaseCropStrategies` in `person_tracker.cc` indexable by layout slot count groupings (1-4, 5-6, 7-8, 9, 10-16).
- **Rationale**: Consolidating target tile aspect ratios, padding factors, and minimum crop limits into static structures allows constant-time lookup and eliminates runtime math overhead.

### Decision 2: Resolution-Adaptive Crop Scaling
- **Choice**: Use the incoming frame width (`vw`) from `PersonTracker::Update` to calculate a scale factor (`vw / 1920.0`), dynamically scaling the base minimum crop width `min_crop_w`.
- **Rationale**: Decouples the tracker from direct INI config dependency, ensuring the absolute minimum crop dimension matches the native camera stream density (e.g., 2.0x larger for 4K inputs, 0.67x smaller for 720p).

### Decision 3: Outbound Ratio Padding Expansion
- **Choice**: Compare the detected aspect ratio against the target display ratio. Expand the shorter dimension outwards centered on the target to achieve aspect symmetry before applying RGA alignment constraints.
- **Rationale**: Ensures the entire tracked person is always enclosed inside the crop area without cropping top of heads or shoulders, and avoids any scaling deformation when composed.

## Risks / Trade-offs

- **[Risk]**: If the output layout canvas has a non-16:9 aspect ratio, reusing ratios computed from 1920x1080 could result in mild stretching.
  - **Mitigation**: UVC profiles in this project exclusively target 16:9 resolutions (720p, 1080p, 2K, 4K). Ratios will remain mathematically consistent.
- **[Risk]**: Extremely small/distant persons will display with wider surrounding background context, potentially making them look smaller.
  - **Mitigation**: This is an intentional tradeoff to preserve clarity (preventing giant blurry pixel blocks). In dense grids (10-16 tiles), the minimum threshold is set very low (100px base) to allow maximum target magnification.
