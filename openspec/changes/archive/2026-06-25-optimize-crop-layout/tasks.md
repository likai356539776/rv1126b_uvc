## 1. Modify Tracker Interface and Signature

- [x] 1.1 Update `PersonTracker::Update` signature in [person_tracker.h](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/include/person_tracker.h) to accept `max_tiles` (default to 4).
- [x] 1.2 Update [camera_pipeline.cc](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/src/camera_pipeline.cc) to pass `cfg_.max_tiles` to `tracker_.Update`.

## 2. Implement Aspect Ratio and Upward Shift Logic

- [x] 2.1 Add grid aspect ratio helper logic in [person_tracker.cc](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/src/person_tracker.cc) based on canvas width, canvas height, and max tiles.
- [x] 2.2 Calculate shifted center `cy_crop` (12% of height shift upward) and padded/expanded dimensions `cw_crop`, `ch_crop` (15% padding base) matching target aspect ratio.
- [x] 2.3 Pass the new `cx_crop`, `cy_crop`, `cw_crop`, and `ch_crop` to `GetAlignedCropBoxCentered` in [person_tracker.cc](file:///home/kama/workspace/ubuntu20.04/uvc_pip/uvc_main/src/person_tracker.cc).

## 3. Verification and Compilation

- [x] 3.1 Build project using `./autobuild.sh` and fix any compilation issues.
- [x] 3.2 Verify person aspect ratios on the UVC video output to ensure no stretching occurs across different tile layout configurations.
