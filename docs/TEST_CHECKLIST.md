# my_uvc Test Checklist

## 1) Build Check (Host)

- Configure:
  - `cmake -S . -B build-rv1126b -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rv1126b-buildroot.cmake -DCMAKE_BUILD_TYPE=Release`
- Build:
  - `cmake --build build-rv1126b -j`
- Verify target:
  - `file build-rv1126b/my_uvc` should be `aarch64`

## 2) Deploy Check (Board)

- Copy binaries/scripts/config to board.
- Ensure executable permission:
  - `chmod +x /usr/bin/my_uvc_usb_config.sh`

## 3) USB Gadget Check (Board)

- Run:
  - `my_uvc_usb_config.sh -w 640 -h 480 -p 25 --verbose`
- Verify logs:
  - `final UDC state` is non-empty
  - `Configured UVC H.264 640x480 ...` appears

## 4) Stream Check (Board + Host)

- Board:
  - `my_uvc -c /usr/bin/my_uvc.ini`
- Host:
  - `v4l2-ctl -d /dev/videoX --list-formats-ext`
  - `ffplay -f v4l2 -input_format h264 -video_size 640x480 -framerate 25 /dev/videoX`
  - Expected: format `H264`, interval includes `0.040s (25.000 fps)`

## 4.1) FPS Negotiation Quick Troubleshooting

- Symptom:
  - Host log shows `driver changed the time per frame from 1/25 to 1/5`
- Check:
  - rerun board script with explicit fps:
    - `my_uvc_usb_config.sh -w 640 -h 480 -p 25 --verbose`
  - verify host side format list again:
    - `v4l2-ctl -d /dev/videoX --list-formats-ext`
- Expected after fix:
  - host no longer falls back to 5fps
  - `ffplay` stream info reports 25 fps

## 5) Stability Check

- Keep streaming for 30-60 minutes.
- Observe:
  - no repeated disconnect/reconnect loops
  - no obvious frame freeze
  - no process crash on board

## 6) Graceful Exit Check

- Stop `my_uvc` with Ctrl+C.
- Confirm process exits cleanly and can be restarted.
