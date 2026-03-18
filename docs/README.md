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
  - `my_uvc_usb_config.sh -w 640 -h 480 -p 25 -n 4 --verbose`
- Start app:
  - `my_uvc -c /userdata/my_uvc.ini --size 640x480`

### Verify (host)

- Enumerate devices:
  - `v4l2-ctl --list-devices`
- Open stream:
  - `ffplay -f v4l2 -input_format h264 -video_size 640x480 /dev/videoX`

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

