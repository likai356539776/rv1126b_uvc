# my_uvc Test Checklist

## 1) Build Check (Host)

- Configure:
  - `cmake -S . -B build-rv1126b -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rv1126b-buildroot.cmake -DCMAKE_BUILD_TYPE=Release`
- Build:
  - `cmake --build build-rv1126b -j`
- Quick build helper (recommended):
  - `./autobuild.sh --release`
  - `./autobuild.sh --debug --clean`
  - `./autobuild.sh --release --jobs 8 --install`
  - multi-device example:
    - `./autobuild.sh --release --install --adb-serial <serial>`
- Verify target:
  - `file build-rv1126b/my_uvc` should be `aarch64`

## 2) Deploy Check (Board)

- Copy binaries/scripts/config to board.
- Ensure executable permission:
  - `chmod +x /usr/bin/my_uvc_usb_config.sh`

## 3) USB Gadget Check (Board)

- Run:
  - `my_uvc_usb_config.sh -w 640 -h 480 -p 25 -n 1 --verbose`
- Verify logs:
  - `final UDC state` is non-empty
  - `Configured UVC H.264 640x480 ...` appears

## 4) Stream Check (Board + Host)

- Board:
  - `my_uvc -c /userdata/my_uvc.ini`
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

## 4.2) Multi-UVC Quick Check

- Example 2-channel setup:
  - Board USB config:
    - `my_uvc_usb_config.sh -w 640 -h 480 -p 25 -n 2 --verbose`
  - Board app:
    - `my_uvc --channels 2 -c /userdata/my_uvc.ini`
  - Host:
    - `v4l2-ctl --list-devices`
    - open both video nodes with two players.

- Optional per-channel independent source:
  - set in `my_uvc.ini`:
    - `channel0_h264_path=/userdata/a.h264`
    - `channel0_fps=25`
    - `channel1_h264_path=/userdata/b.h264`
    - `channel1_fps=20`

## 4.3) Stream Reopen Robustness Check (PPS/IDR)

- Goal:
  - Verify quick recovery when both channels were closed and one channel is reopened.
- Recommended config in `my_uvc.ini`:
  - `sync_to_idr_on_open=1`
  - `inject_sps_pps_on_idr=1`
  - `startup_prime_frames=8` (increase to `12~20` on weaker hosts)
  - `log_level=2`
- Steps:
  - Ensure all host viewers are closed.
  - Open one channel only:
    - `ffplay -f v4l2 -input_format h264 -video_size 640x480 /dev/videoX`
  - Close and reopen repeatedly (at least 10 cycles).
- Expected:
  - No long stall with repeated `non-existing PPS` errors.
  - Board log includes:
    - `channel <n> stream ON`
    - `startup priming finished ... repeated=<N>`

## 4.4) Recommended Presets (Quick Reference)

Use the following presets for fast onsite switching. Keep USB script at `-p 25` unless there is a special host limitation.

| Preset | Scenario | Key `my_uvc.ini` settings | Command example |
|---|---|---|---|
| Stable-first | Long-run pressure test, lowest risk | `log_level=1`, `stats_enable=1`, `stats_interval_sec=5`, `sync_to_idr_on_open=1`, `inject_sps_pps_on_idr=1`, `startup_prime_frames=8`, `idle_sleep_ms=10` | `my_uvc -c /userdata/my_uvc.ini` |
| Low-latency-first | Debugging with lower log noise and less startup repeat | `log_level=0`, `stats_enable=0`, `startup_prime_frames=2`, `log_every_frames=0`, `idle_sleep_ms=5` | `my_uvc -c /userdata/my_uvc.ini --log-level 0 --stats-enable 0 --startup-prime-frames 2` |
| Reopen-robust-first | Frequent open/close, weak host decoder stack | `log_level=2`, `stats_enable=1`, `stats_interval_sec=2`, `sync_to_idr_on_open=1`, `inject_sps_pps_on_idr=1`, `startup_prime_frames=12~20`, `log_every_frames=60` | `my_uvc -c /userdata/my_uvc.ini --log-level 2 --stats-enable 1 --stats-interval 2 --startup-prime-frames 16` |

Notes:

- If host repeatedly shows `non-existing PPS`, increase `startup_prime_frames` by steps of `+2` until stable.
- If CPU/log overhead is high, reduce `log_level` first, then increase `stats_interval_sec`.
- For dual-channel burn-in, prefer `Stable-first` and keep per-channel fps conservative (for example `25 + 20`).

## 5) Stability Check

- Keep streaming for 30-60 minutes.
- Observe:
  - no repeated disconnect/reconnect loops
  - no obvious frame freeze
  - no process crash on board
  - stats logs are printed periodically when enabled

## 5.1) Logging and Stats Check

- Enable:
  - `log_level=2`
  - `stats_enable=1`
  - `stats_interval_sec=5`
- Expected periodic line per channel:
  - `stats ch=<id> video_id=<id> on=<0|1> target_fps=<n> realtime_fps=<x.xx> total=<n> errors=<n>`
- Verify:
  - `realtime_fps` is close to configured fps during steady streaming
  - `total` increases continuously
  - `errors` does not grow rapidly in normal case

## 6) Graceful Exit Check

- Stop `my_uvc` with Ctrl+C.
- Confirm process exits cleanly and can be restarted.
