## 1. Script Modifications

- [x] 1.1 Update `scripts/start_uvc_pip.sh` to parse and resolve the target camera node from command-line argument `$2` or environment variable `CAMERA_NODE`.
- [x] 1.2 Update usage help text in `scripts/start_uvc_pip.sh` to show that the second parameter `[camera_node]` is optional.

## 2. Verification and Testing

- [x] 2.1 Test default camera node selection (`/dev/video51`) by running `./start_uvc_pip.sh v4l2` and verifying the logs.
- [x] 2.2 Test camera node selection via command-line argument by running `./start_uvc_pip.sh v4l2 /dev/video53` and verifying that it attempts to open `/dev/video53`.
- [x] 2.3 Test camera node selection via environment variable by running `CAMERA_NODE=/dev/video53 ./start_uvc_pip.sh v4l2` and verifying that it attempts to open `/dev/video53`.
- [x] 2.4 Verify that command-line argument takes precedence over environment variable by running `CAMERA_NODE=/dev/video51 ./start_uvc_pip.sh v4l2 /dev/video53` and verifying that it attempts to open `/dev/video53`.
