## 1. Strategy Definition and Configuration

- [x] 1.1 Define the `CropStrategy` struct in `person_tracker.h` to hold target ratios, padding, and minimum crop thresholds.
- [x] 1.2 Implement the static table `kBaseCropStrategies` and the layout lookup helper function `GetStrategyIndex` in `person_tracker.cc`.

## 2. Adaptive Cropping Logic Implementation

- [x] 2.1 Retrieve the number of active layout tiles dynamically at the start of `PersonTracker::Update`.
- [x] 2.2 Implement the aspect ratio locking algorithm to expand YOLO bounding boxes to match the target display ratio.
- [x] 2.3 Add clear-distance protection by scaling the base `min_crop_w` with the input resolution factor `vw / 1920.0`.
- [x] 2.4 Apply layout-specific padding factors to dynamically scale the locked crop box size.

## 3. Integration, Compilation, and Verification

- [x] 3.1 Verify that the smoothed crop box properly integrates with existing IIR filtering and RGA alignment logic.
- [x] 3.2 Perform local cross-compilation using `autobuild.sh` to ensure code compiles clean on host.
- [x] 3.3 Verify behavior using integration testing tools or unit tests with mock resolution profiles.
