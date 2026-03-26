# my_uvc 测试检查清单

## 1) 构建检查（主机）

- 配置：
  - `cmake -S . -B build-rv1126b -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rv1126b-buildroot.cmake -DCMAKE_BUILD_TYPE=Release`
- 编译：
  - `cmake --build build-rv1126b -j`
- 快速构建脚本（推荐）：
  - `./autobuild.sh --release`
  - `./autobuild.sh --debug --clean`
  - `./autobuild.sh --release --jobs 8 --install`
  - 多设备示例：
    - `./autobuild.sh --release --install --adb-serial <serial>`
- 目标架构确认：
  - `file build-rv1126b/my_uvc` 应显示为 `aarch64`

## 2) 部署检查（板端）

- 将可执行文件/脚本/配置下发到板端。
- 确认可执行权限：
  - `chmod +x /usr/bin/my_uvc_usb_config.sh`
- 按产品 profile 部署（推荐）：
  - 1 路独立：
    - `./my_uvc_install_to_device.sh --config config/profiles/my_uvc_1ch_independent.ini`
  - 2 路独立：
    - `./my_uvc_install_to_device.sh --config config/profiles/my_uvc_2ch_independent.ini`
  - 4 路独立：
    - `./my_uvc_install_to_device.sh --config config/profiles/my_uvc_4ch_independent.ini`
  - 6/8/10/12/16 路独立：
    - `./my_uvc_install_to_device.sh --config config/profiles/my_uvc_6ch_independent.ini`
    - `./my_uvc_install_to_device.sh --config config/profiles/my_uvc_8ch_independent.ini`
    - `./my_uvc_install_to_device.sh --config config/profiles/my_uvc_10ch_independent.ini`
    - `./my_uvc_install_to_device.sh --config config/profiles/my_uvc_12ch_independent.ini`
    - `./my_uvc_install_to_device.sh --config config/profiles/my_uvc_16ch_independent.ini`
  - 一条命令选 profile：
    - `./scripts/select_profile.sh 1 --install`
    - `./scripts/select_profile.sh 2 --install`
    - `./scripts/select_profile.sh 4 --install`
    - `./scripts/select_profile.sh 6 --install`
    - `./scripts/select_profile.sh 8 --install`
    - `./scripts/select_profile.sh 10 --install`
    - `./scripts/select_profile.sh 12 --install`
    - `./scripts/select_profile.sh 16 --install`
  - 部署并自动启动：
    - `./scripts/select_profile.sh 4 --install --run`
  - 部署 + 自动启动 + 自定义 USB fps：
    - `./scripts/select_profile.sh 4 --install --run --fps 20`
  - 部署 + 自动启动 + 自定义分辨率：
    - `./scripts/select_profile.sh 4 --install --run --size 1280x720`

## 3) USB Gadget 检查（板端）

- 执行：
  - H.264：
    - `my_uvc_usb_config.sh -f H.264 -w 640 -h 480 -p 25 -n 1 --verbose`
  - MJPEG：
    - `my_uvc_usb_config.sh -f MJPEG -w 640 -h 480 -p 25 -n 1 --verbose`
  - 若板端存在会覆盖 gadget 的系统服务（如 `usbdevice`）：`my_uvc_usb_config.sh -w 640 -h 480 -p 25 -n 1 --verbose --stop-system-usb`
  - 做 USB 拔插压测时建议始终带 `--stop-system-usb`，可避免 gadget 激活竞态告警。
- 验证日志：
  - `final UDC state` 非空
  - 出现 `Configured UVC ... 640x480 ...`

## 4) 推流检查（板端 + 主机）

- 板端：
  - H.264：
    - `my_uvc -c /userdata/my_uvc.ini --codec h264`
  - MJPEG：
    - `my_uvc -c /userdata/my_uvc.ini --codec mjpeg --file /userdata/mjpeg_frames_dir`
  - MJPEG + 画中画（PiP）：
    - `my_uvc -c /userdata/my_uvc.ini --codec mjpeg --file /userdata/mjpeg_frames_dir --pip-enable 1 --pip-overlay /userdata/mjpeg_overlay --pip-x 20 --pip-y 20 --pip-w 160 --pip-h 120 --pip-jpeg-quality 85`
  - 或显式覆盖分辨率：
    - `my_uvc -c /userdata/my_uvc.ini --size 640x480`
- 主机：
  - `v4l2-ctl -d /dev/videoX --list-formats-ext`
  - H.264 预览：
    - `ffplay -f v4l2 -input_format h264 -video_size 640x480 -framerate 25 /dev/videoX`
  - MJPEG 预览：
    - `ffplay -f v4l2 -input_format mjpeg -video_size 640x480 -framerate 25 /dev/videoX`
  - 期望：对应格式为 `H264` 或 `MJPG`。

## 4.0.1) PiP 依赖检查（板端）

- 若 PiP 报 `Wrong JPEG library version`，通常是板端同时存在 `libjpeg.so.62` 与 `libjpeg.so.8` 且链接/运行库不一致。
- 当使用 `JPEG_LIB_VERSION=80` 头文件时，`my_uvc` 必须链接到 `libjpeg.so.8`。

## 4.1) FPS 协商快速排障

- 现象：
  - 主机日志出现 `driver changed the time per frame from 1/25 to 1/5`
- 排查：
  - 板端脚本显式指定 fps 后重跑：
    - `my_uvc_usb_config.sh -w 640 -h 480 -p 25 --verbose`
    - 若仍被系统覆盖，追加 `--stop-system-usb`
  - 主机重新查看支持格式：
    - `v4l2-ctl -d /dev/videoX --list-formats-ext`
- 期望：
  - 不再回落到 5fps
  - `ffplay` 显示 25fps

## 4.2) 多路 UVC 快速检查

- 2 路示例：
  - 板端 USB 配置：
    - `my_uvc_usb_config.sh -w 640 -h 480 -p 25 -n 2 --verbose`
    - 必要时追加 `--stop-system-usb`
  - 板端应用：
    - `my_uvc --channels 2 -c /userdata/my_uvc.ini`
  - 主机：
    - `v4l2-ctl --list-devices`
    - 分别打开两个 `video` 节点。

## 4.2.1) 4 路独立快速检查

- 板端 USB 配置：
  - `my_uvc_usb_config.sh -w 640 -h 480 -p 25 -n 4 --verbose`
  - 必要时追加 `--stop-system-usb`
- 板端应用：
  - `my_uvc -c /userdata/my_uvc.ini`
- 主机：
  - `v4l2-ctl --list-devices`
  - 分别打开 4 个 `/dev/videoX` 节点（4 个播放器或脚本）
- 推荐 profile：
  - `config/profiles/my_uvc_4ch_independent.ini`

## 4.2.2) 高路数（6/8/10/12/16）快速检查

- 使用对应 profile：
  - `config/profiles/my_uvc_6ch_independent.ini`
  - `config/profiles/my_uvc_8ch_independent.ini`
  - `config/profiles/my_uvc_10ch_independent.ini`
  - `config/profiles/my_uvc_12ch_independent.ini`
  - `config/profiles/my_uvc_16ch_independent.ini`
- 16 路示例：
  - `./scripts/select_profile.sh 16 --install --run --fps 10 --size 640x480`
- 验证点：
  - `my_uvc_usb_config.sh` 日志包含 `channels=16`
  - 应用统计日志覆盖所有配置路
  - 配置后检查 `ls /sys/kernel/config/usb_gadget/rockchip/functions` 应包含 `uvc.gs0...`（不能只有 `ffs.adb`）

- 可选每路独立源：
  - 在 `my_uvc.ini` 中设置：
    - `channel0_h264_path=/userdata/a.h264`
    - `channel0_fps=25`
    - `channel1_h264_path=/userdata/b.h264`
    - `channel1_fps=20`

## 4.3) 复开流鲁棒性检查（PPS/IDR）

- 目标：
  - 验证在所有通道关闭后，重新打开单路时可快速恢复。
- 推荐配置：
  - `sync_to_idr_on_open=1`
  - `inject_sps_pps_on_idr=1`
  - `startup_prime_frames=8`（弱主机可调到 `12~20`）
  - `log_level=2`
- 步骤：
  - 先关闭主机侧所有预览窗口；
  - 打开其中一路：
    - `ffplay -f v4l2 -input_format h264 -video_size 640x480 /dev/videoX`
  - 连续关闭/重开（至少 10 次）。
- 期望：
  - 不出现长时间 `non-existing PPS` 卡住；
  - 板端日志包含：
    - `channel <n> stream ON`
    - `startup priming finished ... repeated=<N>`

## 4.4) 推荐预设（速查）

默认建议 USB 脚本保持 `-p 25`，除非有主机端特殊限制。

| 预设 | 场景 | 关键 `my_uvc.ini` 参数 | 命令示例 |
|---|---|---|---|
| 稳定优先 | 长时压测，风险最低 | `log_level=1`, `stats_enable=1`, `stats_interval_sec=5`, `sync_to_idr_on_open=1`, `inject_sps_pps_on_idr=1`, `startup_prime_frames=8`, `idle_sleep_ms=10` | `my_uvc -c /userdata/my_uvc.ini` |
| 低时延优先 | 调试时降低日志与启动重复 | `log_level=0`, `stats_enable=0`, `startup_prime_frames=2`, `log_every_frames=0`, `idle_sleep_ms=5` | `my_uvc -c /userdata/my_uvc.ini --log-level 0 --stats-enable 0 --startup-prime-frames 2` |
| 复开流鲁棒优先 | 高频开关流、主机解码栈较弱 | `log_level=2`, `stats_enable=1`, `stats_interval_sec=2`, `sync_to_idr_on_open=1`, `inject_sps_pps_on_idr=1`, `startup_prime_frames=12~20`, `log_every_frames=60` | `my_uvc -c /userdata/my_uvc.ini --log-level 2 --stats-enable 1 --stats-interval 2 --startup-prime-frames 16` |

说明：

- 若主机反复报 `non-existing PPS`，按 `+2` 递增 `startup_prime_frames` 直到稳定。
- 若 CPU/日志压力偏高，先降低 `log_level`，再增大 `stats_interval_sec`。
- 双路及以上长稳压测建议先用“稳定优先”，并适度下调部分路 fps。

## 5) 稳定性检查

- 连续推流 30~60 分钟。
- 观察：
  - 无反复断连/重枚举；
  - 无明显花屏或长时间卡帧；
  - 板端进程不崩溃；
  - 开启统计时，日志持续输出。

## 5.1) 日志与统计检查

- 建议开启：
  - `log_level=2`
  - `stats_enable=1`
  - `stats_interval_sec=5`
- 期望周期日志（每路）：
  - `stats ch=<id> video_id=<id> on=<0|1> target_fps=<n> realtime_fps=<x.xx> total=<n> errors=<n>`
- 验证点：
  - `realtime_fps` 与配置值接近；
  - `total` 持续增加；
  - 正常场景下 `errors` 不应快速增长。

## 6) 优雅退出检查

- 使用 Ctrl+C 停止 `my_uvc`。
- 确认进程正常退出，且可再次启动。

