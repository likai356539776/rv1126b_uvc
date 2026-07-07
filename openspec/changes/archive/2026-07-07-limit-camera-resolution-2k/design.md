## Context

Some USB cameras support 4K resolution (`3840x2160`), which can lead to high CPU and NPU usage during real-time target detection. To maintain stable performance and frame rates, we need to restrict the input resolution to 2K (`2560x1440`).

The backend `uvctest` application already supports a `--camera-size` parameter that overrides the camera's resolution limit. We will pass this parameter from `start_uvc_pip.sh`.

## Goals / Non-Goals

**Goals:**
- Restrict the camera sensor capture resolution to a maximum of 2K (`2560x1440`) in `v4l2` mode.
- Avoid modifying the main C++ codebase, leveraging the existing `--camera-size` option.

**Non-Goals:**
- Dynamically configuring the resolution limit per camera (we hardcode it to 2K for the `v4l2` mode startup script).
- Restricting rockit mode (which is already configured separately at 1080p).

## Decisions

### 1. Update `scripts/start_uvc_pip.sh`
We will append `--camera-size 2560x1440` to the `uvctest` arguments list:

```bash
uvctest \
  --camera-type "$CAMERA_TYPE" \
  ${CAMERA_NODE:+--camera-node $CAMERA_NODE} \
  --size "$RESOLUTION" \
  --camera-size 2560x1440 \
  --yolo-model "$YOLO_MODEL" \
  --yolo-labels "$YOLO_LABELS" \
  --channels 1 \
  --pip-tile-n-tiles 8 \
  --pip-jpeg-quality 85 > "$LOG" 2>&1 &
```

## Risks / Trade-offs

- **[Risk] The connected camera does not support 2K (`2560x1440`).**
  - *Mitigation*: The backend's `QueryMaxMjpegResolution` logic selects the highest supported resolution that is *less than or equal to* the specified limit. Therefore, if a camera only supports up to 1080p, it will successfully fall back to 1080p.
