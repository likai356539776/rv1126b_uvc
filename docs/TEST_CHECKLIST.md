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
- Recommended deploy with product profile:
  - 1-channel independent:
    - `./my_uvc_install_to_device.sh --config config/profiles/my_uvc_1ch_independent.ini`
  - 2-channel independent:
    - `./my_uvc_install_to_device.sh --config config/profiles/my_uvc_2ch_independent.ini`
  - 4-channel independent:
    - `./my_uvc_install_to_device.sh --config config/profiles/my_uvc_4ch_independent.ini`
  - 6/8/10/12/16-channel independent:
    - `./my_uvc_install_to_device.sh --config config/profiles/my_uvc_6ch_independent.ini`
    - `./my_uvc_install_to_device.sh --config config/profiles/my_uvc_8ch_independent.ini`
    - `./my_uvc_install_to_device.sh --config config/profiles/my_uvc_10ch_independent.ini`
    - `./my_uvc_install_to_device.sh --config config/profiles/my_uvc_12ch_independent.ini`
    - `./my_uvc_install_to_device.sh --config config/profiles/my_uvc_16ch_independent.ini`
  - one-command selector:
    - `./scripts/select_profile.sh 1 --install`
    - `./scripts/select_profile.sh 2 --install`
    - `./scripts/select_profile.sh 4 --install`
    - `./scripts/select_profile.sh 6 --install`
    - `./scripts/select_profile.sh 8 --install`
    - `./scripts/select_profile.sh 10 --install`
    - `./scripts/select_profile.sh 12 --install`
    - `./scripts/select_profile.sh 16 --install`
  - deploy + auto-start on board:
    - `./scripts/select_profile.sh 4 --install --run`
  - when USB rebind may drop adb, use serial-safe mode:
    - `./scripts/select_profile.sh 6 --install --run --run-mode serial-safe`
  - deploy + auto-start + custom usb fps:
    - `./scripts/select_profile.sh 4 --install --run --fps 20`
  - deploy + auto-start + custom size:
    - `./scripts/select_profile.sh 4 --install --run --size 1280x720`

## 3) USB Gadget Check (Board)

- Run:
  - H.264:
    - `my_uvc_usb_config.sh -f H.264 -w 640 -h 480 -p 25 -n 1 --verbose`
  - MJPEG:
    - `my_uvc_usb_config.sh -f MJPEG -w 640 -h 480 -p 25 -n 1 --verbose`
  - if board has `usbdevice` service that rewrites gadget: `my_uvc_usb_config.sh -w 640 -h 480 -p 25 -n 1 --verbose --stop-system-usb`
  - for cable plug/unplug stress tests, keep `--stop-system-usb` enabled to avoid gadget activate race warnings.
- Verify logs:
  - `final UDC state` is non-empty
  - `Configured UVC ... 640x480 ...` appears

## 4) Stream Check (Board + Host)

- Board:
  - H.264:
    - `my_uvc -c /userdata/my_uvc.ini --codec h264`
  - MJPEG:
    - `my_uvc -c /userdata/my_uvc.ini --codec mjpeg --file /userdata/mjpeg_frames_dir`
  - MJPEG + PiP:
    - `my_uvc -c /userdata/my_uvc.ini --codec mjpeg --file /userdata/mjpeg_frames_dir --pip-enable 1 --pip-overlay /userdata/mjpeg_overlay --pip-x 20 --pip-y 20 --pip-w 160 --pip-h 120 --pip-jpeg-quality 85`
  - or with explicit size override:
    - `my_uvc -c /userdata/my_uvc.ini --size 640x480`
- Host:
  - `v4l2-ctl -d /dev/videoX --list-formats-ext`
  - H.264 preview:
    - `ffplay -f v4l2 -input_format h264 -video_size 640x480 -framerate 25 /dev/videoX`
  - MJPEG preview:
    - `ffplay -f v4l2 -input_format mjpeg -video_size 640x480 -framerate 25 /dev/videoX`
  - Expected: format includes `H264` or `MJPG` accordingly.

## 4.0.1) PiP Library Sanity (Board)

- If PiP reports `Wrong JPEG library version`, check board rootfs provides both `libjpeg.so.62` and `libjpeg.so.8`.
- `my_uvc` must link against `libjpeg.so.8` when `JPEG_LIB_VERSION=80` headers are used.

## 4.1) FPS Negotiation Quick Troubleshooting

- Symptom:
  - Host log shows `driver changed the time per frame from 1/25 to 1/5`
- Check:
  - rerun board script with explicit fps:
    - `my_uvc_usb_config.sh -w 640 -h 480 -p 25 --verbose`
    - if gadget is overwritten by system service, append `--stop-system-usb`
  - verify host side format list again:
    - `v4l2-ctl -d /dev/videoX --list-formats-ext`
- Expected after fix:
  - host no longer falls back to 5fps
  - `ffplay` stream info reports 25 fps

## 4.2) Multi-UVC Quick Check

- Example 2-channel setup:
  - Board USB config:
  - `my_uvc_usb_config.sh -w 640 -h 480 -p 25 -n 2 --verbose`
  - if needed: `... --stop-system-usb`
  - Board app:
    - `my_uvc --channels 2 -c /userdata/my_uvc.ini`
  - Host:
    - `v4l2-ctl --list-devices`
    - open both video nodes with two players.

## 4.2.1) 4-Channel Independent Quick Check

- Board USB config:
  - `my_uvc_usb_config.sh -w 640 -h 480 -p 25 -n 4 --verbose`
  - if needed: `... --stop-system-usb`
- Board app:
  - `my_uvc -c /userdata/my_uvc.ini`
- Host:
  - `v4l2-ctl --list-devices`
  - open 4 `/dev/videoX` nodes separately (4 players or scripts)
- Recommended profile:
  - `config/profiles/my_uvc_4ch_independent.ini`

## 4.2.2) High-Channel (6/8/10/12/16) Quick Check

- Use matching profile:
  - `config/profiles/my_uvc_6ch_independent.ini`
  - `config/profiles/my_uvc_8ch_independent.ini`
  - `config/profiles/my_uvc_10ch_independent.ini`
  - `config/profiles/my_uvc_12ch_independent.ini`
  - `config/profiles/my_uvc_16ch_independent.ini`
- Selector example (16 channels):
  - `./scripts/select_profile.sh 16 --install --run --fps 10 --size 640x480`
- Verify:
  - `my_uvc_usb_config.sh` log shows `channels=16`
  - app stats output includes all configured channels
  - after config, `ls /sys/kernel/config/usb_gadget/rockchip/functions` should include `uvc.gs0...` (not only `ffs.adb`)

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
