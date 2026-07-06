## 1. Structure and Strategy Table Expansion

- [x] 1.1 Add `base_dist_limit` and `base_scale_limit` parameters to the `CropStrategy` struct in `person_tracker.h`.
- [x] 1.2 Update the static strategy table `kBaseCropStrategies` in `person_tracker.cc` with custom deadzone thresholds for all 5 layout groups.

## 2. Adaptive Jitter Logic Refactoring

- [x] 2.1 Calculate relative deadzones (`dist_limit` and `scale_limit`) dynamically in `PersonTracker::Update` based on slot width and clamp with minimum pixel limits.
- [x] 2.2 Calculate relative convergence locks (`lock_dist` and `lock_scale`) based on slot width and clamp with minimum pixel limits.
- [x] 2.3 Replace the hardcoded jitter parameters in the matched slot update block with the new calculated dynamic thresholds.

## 3. Verification and Compilation

- [x] 3.1 Perform local host unit tests check to ensure configuration parsing and layouts are unaffected.
- [x] 3.2 Perform板端交叉编译 with `autobuild.sh` to ensure target binary builds clean.
