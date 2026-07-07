## Context

Currently, the startup script `start_uvc_pip.sh` hardcodes `/dev/video51` as the camera node for the V4L2 pipeline. In environments with multiple USB cameras plugged in (for example, a 4K USB Camera at `/dev/video51` and a Logitech Camera at `/dev/video53`), users cannot easily select which camera to open. 

This design doc details how to modify the startup script to support selecting the camera node via a command line argument or an environment variable.

## Goals / Non-Goals

**Goals:**
- Allow users to specify the target V4L2 device node (e.g., `/dev/video53`) when running the `start_uvc_pip.sh` script.
- Keep `/dev/video51` as the default camera node for backward compatibility.
- Enable camera node override via either command-line parameter or environment variable (`CAMERA_NODE`).

**Non-Goals:**
- Supporting concurrent running of multiple `uvctest` instances (the script's PID-tracking design and USB gadget control logic assume a single instance of `v4l2` camera type at any time).

## Decisions

### 1. Camera Node Resolution Logic in `start_uvc_pip.sh`
We will update the resolution logic for `CAMERA_NODE` inside the script's `v4l2` branch:
- First, check if the second command-line argument `$2` is specified. If so, use it.
- Second, check if the environment variable `CAMERA_NODE` is set. If so, keep/use it.
- Third, fall back to `/dev/video51`.

**Implementation sketch:**
```bash
if [ "$1" = "v4l2" ]; then
  CAMERA_TYPE="v4l2"
  if [ -n "$2" ]; then
    CAMERA_NODE="$2"
  elif [ -n "$CAMERA_NODE" ]; then
    # Keep the environment variable CAMERA_NODE
    :
  else
    CAMERA_NODE="/dev/video51"
  fi
  RESOLUTION="1920x1080"
  PIX_FMT="MJPEG"
```

### 2. Update Usage Help Text
To assist users in discovering the new feature, we will update the usage output printed when invalid arguments are provided:
```bash
echo "Usage: $0 [v4l2|rockit] [camera_node]"
```

## Risks / Trade-offs

- **[Risk] User enters an invalid or non-existent device node (e.g., `/dev/video99`).**
  - *Mitigation*: The backend `uvctest` already checks if the node can be opened and will log an error/exit if it fails. The script doesn't need to add complex path validation.
