# my_uvc Docs Index

本文档是 `my_uvc/docs` 目录的统一入口，提供中英文文档索引与常用命令速查。

## 1) Document Map

- Test checklist (EN): `TEST_CHECKLIST.md`
- Test checklist (中文): `TEST_CHECKLIST_CN.md`
- Multi-UVC design (EN): `MULTI_UVC_DESIGN.md`
- Multi-UVC design (中文): `MULTI_UVC_DESIGN_CN.md`
- Requirements (project root): `../REQUIREMENTS.md`

## 2) Quick Start

### Build (host)

- Release build:
  - `./autobuild.sh --release`
- Debug build:
  - `./autobuild.sh --debug`

### Deploy profile (host)

- 4-channel independent:
  - `./scripts/select_profile.sh 4 --install`
- 16-channel independent + auto-run:
  - `./scripts/select_profile.sh 16 --install --run --fps 10 --size 640x480`

### Run manually (board)

- USB gadget setup:
  - H.264:
    - `my_uvc_usb_config.sh -f H.264 -w 640 -h 480 -p 25 -n 4 --verbose`
  - MJPEG:
    - `my_uvc_usb_config.sh -f MJPEG -w 640 -h 480 -p 25 -n 1 --verbose`
  - if system `usbdevice` service rewrites gadget, append `--stop-system-usb`
- Start app:
  - H.264:
    - `my_uvc -c /userdata/my_uvc.ini --codec h264 --size 640x480`
  - MJPEG (file/dir source):
    - `my_uvc -c /userdata/my_uvc.ini --codec mjpeg --file /userdata/mjpeg_frames_dir --size 640x480`
  - MJPEG + PiP (real-time overlay):
    - `my_uvc -c /userdata/my_uvc.ini --codec mjpeg --file /userdata/mjpeg_frames_dir --size 640x480 --pip-enable 1 --pip-overlay /userdata/mjpeg_overlay --pip-x 20 --pip-y 20 --pip-w 160 --pip-h 120 --pip-jpeg-quality 85`

### Verify (host)

- Enumerate devices:
  - `v4l2-ctl --list-devices`
- Open stream:
  - H.264: `ffplay -f v4l2 -input_format h264 -video_size 640x480 /dev/videoX`
  - MJPEG: `ffplay -f v4l2 -input_format mjpeg -video_size 640x480 /dev/videoX`

## 3) Profile Files

Config profiles are in `../config/profiles/`:

- `my_uvc_1ch_independent.ini`
- `my_uvc_2ch_independent.ini`
- `my_uvc_4ch_independent.ini`
- `my_uvc_6ch_independent.ini`
- `my_uvc_8ch_independent.ini`
- `my_uvc_10ch_independent.ini`
- `my_uvc_12ch_independent.ini`
- `my_uvc_16ch_independent.ini`

## 4) Notes

- Board runtime config path is unified as `/userdata/my_uvc.ini`.
- App-side max channels: `16`.
- USB script-side max channels: `16`.
- Buildroot note: if board rootfs provides both `libjpeg.so.62` and `libjpeg.so.8`, `my_uvc` must link against `libjpeg.so.8` to avoid `Wrong JPEG library version` during PiP.

