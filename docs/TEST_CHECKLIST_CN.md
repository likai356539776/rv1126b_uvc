# my_uvc 测试检查清单

## 1) 构建检查（主机）

- 配置：
  - `cmake -S . -B build-rv1126b -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rv1126b-buildroot.cmake -DCMAKE_BUILD_TYPE=Release`
- 编译：
  - `cmake --build build-rv1126b -j`
- 快速构建（推荐）：
  - `./autobuild.sh --release`
  - `./autobuild.sh --debug --clean`
  - `./autobuild.sh --release --jobs 8 --install`
  - 多设备：`./autobuild.sh --release --install --adb-serial <serial>`
- 验证：`file build-rv1126b/uvctest` 应显示 `aarch64`

## 2) 部署检查（板端）

- 将文件下发到板端，确认可执行权限。
- 按产品 profile 部署（推荐）：
  - `./scripts/select_profile.sh 1 --install`
  - `./scripts/select_profile.sh 2 --install`
  - `./scripts/select_profile.sh 4 --install`
  - `./scripts/select_profile.sh 8 --install`
  - `./scripts/select_profile.sh 16 --install`
- 部署并自动启动：
  - `./scripts/select_profile.sh 4 --install --run`
  - `./scripts/select_profile.sh 4 --install --run --fps 20 --size 1280x720`

## 3) USB Gadget 检查（板端）

- 执行：
  - H.264：`my_uvc_usb_config.sh -f H.264 -w 1920 -h 1080 -p 25 -n 1 --verbose`
  - MJPEG：`my_uvc_usb_config.sh -f MJPEG -w 1920 -h 1080 -p 25 -n 1 --verbose`
  - 若系统 USB 服务会覆盖 gadget：追加 `--stop-system-usb`
  - 做 USB 拔插测试时建议始终带 `--stop-system-usb`
- 验证日志：
  - `final UDC state` 非空
  - 出现 `Configured UVC ... 1920x1080 ...`

## 4) 推流检查（板端 + 主机）

- 板端：
  - H.264：`uvctest -c /userdata/my_uvc.ini --codec h264`
  - MJPEG：`uvctest -c /userdata/my_uvc.ini --codec mjpeg --file /userdata/mjpeg_frames_dir`
  - MJPEG + PiP：`uvctest -c /userdata/my_uvc.ini --codec mjpeg --file /userdata/mjpeg_frames_dir --pip-enable 1 --pip-overlay /userdata/mjpeg_overlay --pip-x 20 --pip-y 20 --pip-w 640 --pip-h 480 --pip-jpeg-quality 85`
- 主机：
  - `v4l2-ctl -d /dev/videoX --list-formats-ext`
  - H.264：`ffplay -f v4l2 -input_format h264 -video_size 1920x1080 -framerate 25 /dev/videoX`
  - MJPEG：`ffplay -f v4l2 -input_format mjpeg -video_size 1920x1080 -framerate 25 /dev/videoX`

## 4.0.1) PiP 依赖检查（板端）

- 若 PiP 报 `Wrong JPEG library version`，检查板端 `libjpeg.so.62` 与 `libjpeg.so.8` 的链接情况。
- `uvctest` 需链接 `libjpeg.so.8`。

## 4.1) FPS 协商排障

- 现象：主机日志出现 `driver changed the time per frame from 1/25 to 1/5`
- 处理：板端脚本显式指定 fps，必要时追加 `--stop-system-usb`。
- 验证：`v4l2-ctl --list-formats-ext` 显示正确帧率。

## 4.2) 多路 UVC 快速检查

- 2 路示例：
  - 板端：`my_uvc_usb_config.sh -w 1920 -h 1080 -p 25 -n 2 --verbose`
  - 板端：`uvctest --channels 2 -c /userdata/my_uvc.ini`
  - 主机：`v4l2-ctl --list-devices`，分别打开两个视频节点。

## 4.2.1) 4 路独立快速检查

- 板端 USB：`my_uvc_usb_config.sh -w 1920 -h 1080 -p 25 -n 4 --verbose`
- 板端应用：`uvctest -c /userdata/my_uvc.ini`
- 主机：分别打开 4 个 `/dev/videoX` 节点。
- 推荐 profile：`config/profiles/my_uvc_4ch_independent.ini`

## 4.2.2) 高路数（6/8/10/12/16）快速检查

- 使用对应 profile 与选择器：
  - `./scripts/select_profile.sh 16 --install --run --fps 10 --size 1920x1080`
- 验证：USB 配置日志显示正确路数，应用统计覆盖所有路。

## 4.3) 复开流鲁棒性检查（PPS/IDR）

- 推荐配置：`sync_to_idr_on_open=1`、`inject_sps_pps_on_idr=1`、`startup_prime_frames=8`
- 步骤：连续关闭/重开单路（至少 10 次）。
- 期望：不出现长时间 `non-existing PPS` 卡住。

## 4.4) USB 热拔插恢复检查

### 4.4.1) 推流中拔插

- 前提：
  - 板端 `uvctest` 运行中，主机端正在预览画面
  - USB 配置脚本使用了 `--stop-system-usb`
- 步骤：
  1. 确认主机端画面正常。
  2. 从板端拔掉 USB 线。
  3. 等待 2~3 秒。
  4. 重新插入 USB 线。
  5. 主机端关闭并重新打开摄像头应用。
- 板端日志期望：
  - 拔线后出现：`UVC: device disconnected (ENODEV), releasing buffers`（每个 video_id 一次）
  - 插入后出现：`UVC_EVENT_STREAMON` → `Buffer mapped` → `Starting video stream`
- 板端日志不应出现：
  - `Unable to allocate buffers: Device or resource busy`
  - 持续刷屏的 `VIDIOC_DQEVENT failed: No such device`
- 主机期望：重新打开摄像头后画面恢复。

### 4.4.2) 关闭摄像头后拔插

- 步骤：
  1. 正常推流，然后在主机端关闭摄像头。
  2. 拔掉 USB 线。
  3. 重新插入 USB 线。
  4. 在主机端打开摄像头。
- 期望：画面正常显示。

### 4.4.3) 多次拔插循环

- 步骤：重复拔插 5~10 次，每次验证恢复。
- 期望：每次推流都能恢复，无需重启板端进程。

## 4.5) 推荐预设（速查）


| 预设    | 场景    | 关键参数                                                      | 命令示例                                                                      |
| ----- | ----- | --------------------------------------------------------- | ------------------------------------------------------------------------- |
| 稳定优先  | 长时压测  | `log_level=1`, `stats_enable=1`, `startup_prime_frames=8` | `uvctest -c /userdata/my_uvc.ini`                                         |
| 低时延优先 | 调试    | `log_level=0`, `stats_enable=0`, `startup_prime_frames=2` | `uvctest -c /userdata/my_uvc.ini --log-level 0 --stats-enable 0`          |
| 复开流鲁棒 | 高频开关流 | `log_level=2`, `startup_prime_frames=16`                  | `uvctest -c /userdata/my_uvc.ini --log-level 2 --startup-prime-frames 16` |


说明：

- 若主机反复报 `non-existing PPS`，按 +2 递增 `startup_prime_frames`。
- USB 拔插测试建议始终使用 `--stop-system-usb`。

## 5) 稳定性检查

- 连续推流 30~60 分钟。
- 观察：
  - 无反复断连/重枚举
  - 无明显花屏或长时间卡帧
  - 板端进程不崩溃
  - 开启统计时日志持续输出

## 5.1) 日志与统计检查

- 建议开启：`log_level=2`、`stats_enable=1`、`stats_interval_sec=5`
- 期望周期日志（每路）：
  - `stats ch=<id> video_id=<id> on=<0|1> target_fps=<n> realtime_fps=<x.xx> total=<n> errors=<n>`
- 验证：`realtime_fps` 接近配置值，`total` 持续增加，`errors` 不快速增长。

## 6) 优雅退出检查

- 使用 Ctrl+C 停止 `uvctest`。
- 确认进程正常退出，且可再次启动。

