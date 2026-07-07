## 1. Script Modifications

- [x] 1.1 Update `scripts/start_uvc_pip.sh` to pass `--camera-size 2560x1440` when launching `uvctest`.

## 2. Verification and Testing

- [x] 2.1 Verify that the 4K USB Camera `/dev/video51` is capped at `2560x1440` (2K) when starting the pipeline.
