# my_uvc Docs Index

本文档是 `my_uvc/docs` 目录的统一入口，提供中英文文档索引与常用命令速查。

## 1) Document Map

| 文档 | EN | CN |
|------|----|----|
| 测试清单 | `TEST_CHECKLIST.md` | `TEST_CHECKLIST_CN.md` |
| 多路设计 | `MULTI_UVC_DESIGN.md` | `MULTI_UVC_DESIGN_CN.md` |
| 需求文档 | `../REQUIREMENTS.md` | — |
| 交接文档 | `../HANDOVER.md` | — |

## 2) Quick Start

### Build (host)

```bash
# Release
./autobuild.sh --release
# Debug
./autobuild.sh --debug
```

### Deploy profile (host → board)

```bash
# 4路独立部署
./scripts/select_profile.sh 4 --install
# 16路部署并自动启动
./scripts/select_profile.sh 16 --install --run --fps 10 --size 1920x1080
```

`select_profile.sh` 在「Suggested board commands」中仅给出 **`uvctest`** 作为应用入口（见 `HANDOVER.md` 安装布局）。

### Run manually (board)

```bash
# 1. USB gadget 配置
#    H.264:
my_uvc_usb_config.sh -f H.264 -w 1920 -h 1080 -p 25 -n 2 --verbose
#    MJPEG:
my_uvc_usb_config.sh -f MJPEG -w 1920 -h 1080 -p 25 -n 2 --verbose
#    如系统 USB 服务会覆盖 gadget，追加 --stop-system-usb

# 2. 启动应用（-c 指向配置目录或单文件，默认 /userdata）
#    主程序名为 uvctest。
#    H.264:
uvctest -c /userdata --codec h264 --size 1920x1080
#    MJPEG:
uvctest -c /userdata --codec mjpeg --file /userdata/mjpeg_frames_dir --size 1920x1080
#    MJPEG + PiP:
uvctest -c /userdata --codec mjpeg --file /userdata/mjpeg_frames_dir \
  --pip-enable 1 --pip-overlay /userdata/mjpeg_overlay \
  --pip-x 20 --pip-y 20 --pip-w 640 --pip-h 480 --pip-jpeg-quality 85
```

### Verify (host)

```bash
# 枚举设备
v4l2-ctl --list-devices
# H.264 预览
ffplay -f v4l2 -input_format h264 -video_size 1920x1080 /dev/videoX
# MJPEG 预览
ffplay -f v4l2 -input_format mjpeg -video_size 1920x1080 /dev/videoX
```

## 3) MJPEG USB Command Matrix (1~8 channels)

默认分辨率已改为 **1920×1080**；下列矩阵仍为 **640×480 / MJPEG / 25fps** 下的带宽调参参考（`--mjpeg-max-frame-size` 与帧缓冲需随分辨率与路数重算）。1080p 多路请优先用小路数或降 fps 实测。

```bash
# 1路（优先画质）
my_uvc_usb_config.sh -f MJPEG -w 640 -h 480 -p 25 -n 1 --stop-system-usb
# 备选：--mjpeg-max-frame-size 153600

# 2路
my_uvc_usb_config.sh -f MJPEG -w 640 -h 480 -p 25 -n 2 --stop-system-usb
# 备选：--mjpeg-max-frame-size 102400

# 3路
my_uvc_usb_config.sh -f MJPEG -w 640 -h 480 -p 25 -n 3 --stop-system-usb
# 备选：--mjpeg-max-frame-size 81920

# 4路
my_uvc_usb_config.sh -f MJPEG -w 640 -h 480 -p 25 -n 4 --stop-system-usb
# 备选：--mjpeg-max-frame-size 76800

# 5路
my_uvc_usb_config.sh -f MJPEG -w 640 -h 480 -p 25 -n 5 --stop-system-usb
# 备选：--mjpeg-max-frame-size 65536

# 6路
my_uvc_usb_config.sh -f MJPEG -w 640 -h 480 -p 25 -n 6 --stop-system-usb
# 推荐固定值起点：--mjpeg-max-frame-size 61440
# 若仍不稳：--mjpeg-max-frame-size 57344

# 7路
my_uvc_usb_config.sh -f MJPEG -w 640 -h 480 -p 25 -n 7 --stop-system-usb
# 推荐固定值起点：--mjpeg-max-frame-size 57344
# 若仍不稳：--mjpeg-max-frame-size 53248

# 8路
my_uvc_usb_config.sh -f MJPEG -w 640 -h 480 -p 25 -n 8 --stop-system-usb
# 推荐固定值起点：--mjpeg-max-frame-size 53248
# 若仍不稳：--mjpeg-max-frame-size 49152
```

建议每次切换路数都执行：
1) `killall uvctest`
2) 执行对应 `my_uvc_usb_config.sh` 命令
3) 拔插 USB 线，触发主机重新枚举
4) 再启动 `uvctest`

## 4) USB Hot-Plug Recovery

USB 热拔插恢复是内置能力，无需额外操作：

1. 正常推流时拔掉 USB 线
2. 板端日志出现 `UVC: device disconnected (ENODEV), releasing buffers`
3. 重新插入 USB 线
4. 板端日志出现 `UVC_EVENT_STREAMON` → `Buffer mapped` → `Starting video stream`
5. 主机端重新打开摄像头即可恢复

若需做 USB 拔插压测，建议：
- 始终使用 `--stop-system-usb` 避免系统 USB 服务干扰
- 设置 `MY_UVC_UEVENT_DUMP_ALL=1` 环境变量可查看完整 uevent 日志

## 5) Profile Files

配置模板位于 `../config/profiles/`：

| Profile | 路数 |
|---------|------|
| `my_uvc_1ch_independent.ini` | 1 |
| `my_uvc_2ch_independent.ini` | 2 |
| `my_uvc_4ch_independent.ini` | 4 |
| `my_uvc_6ch_independent.ini` | 6 |
| `my_uvc_8ch_independent.ini` | 8 |
| `my_uvc_10ch_independent.ini` | 10 |
| `my_uvc_12ch_independent.ini` | 12 |
| `my_uvc_16ch_independent.ini` | 16 |

## 6) Embedding `libmy_uvc` (C API)

- 头文件：`include/my_uvc/my_uvc.h`（安装到 `.../include/my_uvc/`）。
- 若应用**不**使用 `uvctest`，只链接 `libmy_uvc.so` 并自行 `my_uvc_create`：可任选  
  (a) 在代码里填 `my_uvc_config_t`，或  
  (b) 调用 **`my_uvc_load_ini_section_only(path, "libmy_uvc", …)`** 从**单个文件**中只加载 `[libmy_uvc]`（或 legacy `[my_uvc]`）并填入 `my_uvc_config_t`（与 `uvctest` 使用的字段一致）。  
  目录合并（`-c /userdata` 多文件）仍由 **`uvctest`** / `load_app_config` 路径处理；库内 C API 对应「单文件、单区段」便利接口。详见 `HANDOVER.md` §4 与 `config/README_CONFIG.md`。

## 7) Notes

- 板端配置：默认 `-c /userdata`（目录），依次合并 `libmy_uvc.ini`、`libmy_uvc_pip.ini`、`uvctest.ini`；仍可使用单文件 `-c /userdata/my_uvc.ini`。说明见 `config/README_CONFIG.md`
- 应用侧最大路数：16
- USB 脚本侧最大路数：16
- Buildroot 注意：若板端同时存在 `libjpeg.so.62` 与 `libjpeg.so.8`，`uvctest` / `libmy_uvc.so` 必须链接 `libjpeg.so.8`
- USB 热拔插恢复依赖 Rockchip configfs gadget 设备节点在 USB 断开时保持存在的默认行为
