# my_uvc Test Checklist

## 1) Build Check (Host)

- Configure:
  - `cmake -S . -B build-rv1126b -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rv1126b-buildroot.cmake -DCMAKE_BUILD_TYPE=Release`
- Build:
  - `cmake --build build-rv1126b -j`
- Quick build helper (recommended):
  - `./autobuild.sh --release`
  - `./autobuild.sh --debug --clean`
  - Cross clean + Debug: `./autobuild.sh -c -d` (same as `--clean --debug`)
  - `./autobuild.sh --release --jobs 8 --install`
  - multi-device: `./autobuild.sh --release --install --adb-serial <serial>`
- Verify target:
  - `file build-rv1126b/uvctest` should be `aarch64`

## 2) Deploy Check (Board)

- Copy binaries/scripts/config to board.
- Ensure executable permission:
  - `chmod +x /usr/bin/my_uvc_usb_config.sh`
- Recommended deploy with product profile:
  - `./scripts/select_profile.sh 1 --install`
  - `./scripts/select_profile.sh 2 --install`
  - `./scripts/select_profile.sh 4 --install`
  - `./scripts/select_profile.sh 8 --install`
  - `./scripts/select_profile.sh 16 --install`
- Deploy + auto-start:
  - `./scripts/select_profile.sh 4 --install --run`
  - `./scripts/select_profile.sh 4 --install --run --fps 20 --size 1280x720`

## 3) USB Gadget Check (Board)

- Run:
  - H.264: `my_uvc_usb_config.sh -f H.264 -w 1920 -h 1080 -p 25 -n 1 --verbose`
  - MJPEG: `my_uvc_usb_config.sh -f MJPEG -w 1920 -h 1080 -p 25 -n 1 --verbose`
  - if system `usbdevice` service rewrites gadget, append `--stop-system-usb`
  - for USB hot-plug tests, always use `--stop-system-usb`
- Verify logs:
  - `final UDC state` is non-empty
  - `Configured UVC ... 1920x1080 ...` appears

## 4) Stream Check (Board + Host)

- Board:
  - H.264: `uvctest -c /userdata --codec h264`
  - MJPEG: `uvctest -c /userdata --codec mjpeg --file /userdata/mjpeg_frames_dir`
  - MJPEG + PiP: `uvctest -c /userdata --codec mjpeg --file /userdata/mjpeg_frames_dir --pip-enable 1 --pip-overlay /userdata/mjpeg_overlay --pip-x 20 --pip-y 20 --pip-w 640 --pip-h 480 --pip-jpeg-quality 85`
- Host:
  - `v4l2-ctl -d /dev/videoX --list-formats-ext`
  - H.264: `ffplay -f v4l2 -input_format h264 -video_size 1920x1080 -framerate 25 /dev/videoX`
  - MJPEG: `ffplay -f v4l2 -input_format mjpeg -video_size 1920x1080 -framerate 25 /dev/videoX`

## 4.0.1) PiP Library Sanity (Board)

- If PiP reports `Wrong JPEG library version`, check board provides both `libjpeg.so.62` and `libjpeg.so.8`.
- `uvctest` must link against `libjpeg.so.8` when `JPEG_LIB_VERSION=80` headers are used.

## 4.1) FPS Negotiation Quick Troubleshooting

- Symptom: Host log shows `driver changed the time per frame from 1/25 to 1/5`
- Fix: rerun board script with explicit fps and `--stop-system-usb` if needed.
- Verify: `v4l2-ctl -d /dev/videoX --list-formats-ext` shows correct fps.

## 4.2) Multi-UVC Quick Check

- 2-channel example:
  - Board: `my_uvc_usb_config.sh -w 1920 -h 1080 -p 25 -n 2 --verbose`
  - Board: `uvctest --channels 2 -c /userdata`
  - Host: `v4l2-ctl --list-devices`, open both video nodes.

## 4.2.1) 4-Channel Independent Quick Check

- Board USB: `my_uvc_usb_config.sh -w 1920 -h 1080 -p 25 -n 4 --verbose`
- Board app: `uvctest -c /userdata`
- Host: open 4 `/dev/videoX` nodes separately.
- Recommended profile: `config/profiles/my_uvc_4ch_independent.ini`

## 4.2.2) High-Channel (6/8/10/12/16) Quick Check

- Use matching profile and selector:
  - `./scripts/select_profile.sh 16 --install --run --fps 10 --size 1920x1080`
- Verify:
  - `my_uvc_usb_config.sh` log shows correct channel count
  - app stats output includes all configured channels

## 4.3) Stream Reopen Robustness Check (PPS/IDR)

- Recommended config:
  - `sync_to_idr_on_open=1`, `inject_sps_pps_on_idr=1`, `startup_prime_frames=8`
- Steps: close and reopen one channel repeatedly (at least 10 cycles).
- Expected: no long stall with `non-existing PPS` errors.

## 4.4) USB Hot-Plug Recovery Check

### 4.4.1) Replug During Streaming

- Prerequisites:
  - Board running `uvctest` with streaming active on host
  - USB config script run with `--stop-system-usb`
- Steps:
  1. Verify normal streaming on host (image visible).
  2. Physically unplug USB cable from board.
  3. Wait 2-3 seconds.
  4. Plug USB cable back in.
  5. On host, close and reopen the camera application.
- Board log expected:
  - `UVC: device disconnected (ENODEV), releasing buffers` (once per video_id)
  - After replug: `UVC_EVENT_STREAMON` → `Buffer mapped` → `Starting video stream`
- Board log must NOT show:
  - `Unable to allocate buffers: Device or resource busy`
  - Continuous `VIDIOC_DQEVENT failed: No such device` flood
- Host expected: camera image resumes after reopening.

### 4.4.2) Replug After Closing Camera

- Steps:
  1. Start streaming, then close host camera.
  2. Unplug USB cable.
  3. Plug USB cable back in.
  4. Open camera on host.
- Expected: normal image display.

### 4.4.3) Multiple Replug Cycles

- Steps: repeat unplug/replug 5-10 times, each time verifying recovery.
- Expected: streaming recovers each time without board process restart.

## 4.5) Recommended Presets (Quick Reference)

| Preset | Scenario | Key settings | Command |
|---|---|---|---|
| Stable-first | Long-run test | `log_level=1`, `stats_enable=1`, `startup_prime_frames=8` | `uvctest -c /userdata` |
| Low-latency | Debugging | `log_level=0`, `stats_enable=0`, `startup_prime_frames=2` | `uvctest -c /userdata --log-level 0 --stats-enable 0` |
| Reopen-robust | Frequent open/close | `log_level=2`, `startup_prime_frames=16` | `uvctest -c /userdata --log-level 2 --startup-prime-frames 16` |

Notes:
- If host shows `non-existing PPS`, increase `startup_prime_frames` by +2.
- For USB hot-plug tests, always use `--stop-system-usb`.

## 5) Stability Check

- Keep streaming for 30-60 minutes.
- Observe:
  - no repeated disconnect/reconnect loops
  - no obvious frame freeze
  - no process crash on board
  - stats logs printed periodically when enabled

## 5.1) Logging and Stats Check

- Enable: `log_level=2`, `stats_enable=1`, `stats_interval_sec=5`
- Expected periodic line per channel:
  - `stats ch=<id> video_id=<id> on=<0|1> target_fps=<n> realtime_fps=<x.xx> total=<n> errors=<n>`
- Verify: `realtime_fps` close to configured, `total` increasing, `errors` not growing rapidly.

## 6) Graceful Exit Check

- Stop `uvctest` with Ctrl+C.
- Confirm process exits cleanly and can be restarted.
