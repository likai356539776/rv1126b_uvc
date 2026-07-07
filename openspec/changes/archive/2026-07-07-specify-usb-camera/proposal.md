## Why

Currently, the startup script `start_uvc_pip.sh` hardcodes `/dev/video51` as the camera node for the V4L2 pipeline. When multiple USB cameras are connected to the board, users cannot select which camera to open, and they cannot run multiple instances targeting different cameras. This change adds the ability to specify the target camera node, enabling multi-camera support and flexible camera selection.

## What Changes

- Add support for specifying the camera node in `start_uvc_pip.sh` via a command line argument (e.g. `start_uvc_pip.sh v4l2 /dev/video53`) or an environment variable `CAMERA_NODE`.
- Default to `/dev/video51` when no camera node is explicitly specified, maintaining backwards compatibility.
- Pass the specified camera node to the backend `uvctest` command via its existing `--camera-node` argument.

## Capabilities

### New Capabilities
- `usb-camera-selection`: Allows configuring and specifying a specific V4L2 device node (e.g. `/dev/video51`, `/dev/video53`) to open when starting the UVC V4L2 capture and processing pipeline.

### Modified Capabilities
<!-- No requirement changes to existing capabilities -->

## Impact

- **Scripts**: `scripts/start_uvc_pip.sh` will be modified to parse and pass the camera node.
- **APIs**: No external API changes; command line interface of the start script will be extended with an optional parameter.
- **Dependencies**: No new external dependencies.
