## Why

When high-resolution USB cameras (such as 4K cameras) are connected, the UVC pipeline might capture at their maximum resolution, which increases processing latency and resource consumption (CPU/NPU) during YOLO object detection. Restricting the maximum camera input resolution to 2K (e.g. 2560x1440) ensures optimal performance and stability.

## What Changes

- Add `--camera-size 2560x1440` to the `uvctest` execution arguments in `start_uvc_pip.sh` under `v4l2` mode.
- This instructs the backend `uvctest` to cap the camera capture resolution to 2K (2560x1440) even if the connected camera supports 4K (3840x2160).

## Capabilities

### New Capabilities
- `camera-resolution-limiting`: Restricts the camera sensor capture resolution to a specified maximum width and height to optimize resource usage.

### Modified Capabilities
<!-- No modified capabilities -->

## Impact

- **Scripts**: `scripts/start_uvc_pip.sh` will pass `--camera-size 2560x1440` to `uvctest`.
- **System**: Capped maximum capture resolution for 4K USB cameras, saving CPU/NPU processing bandwidth.
