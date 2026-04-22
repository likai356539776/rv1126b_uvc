# my_uvc 测试检查清单

## 1) 构建检查（主机）

- 配置：
  - `cmake -S . -B build-rv1126b -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rv1126b-buildroot.cmake -DCMAKE_BUILD_TYPE=Release`
- 编译：
  - `cmake --build build-rv1126b -j`
- 快速构建（推荐）：
  - `./autobuild.sh --release`
  - `./autobuild.sh --debug --clean`
  - 交叉编译清理后 Debug：`./autobuild.sh -c -d`（同 `--clean --debug`）
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
  - H.264：`uvctest -c /userdata --codec h264`
  - MJPEG：`uvctest -c /userdata --codec mjpeg --file /userdata/mjpeg_frames_dir`
  - MJPEG + PiP：`uvctest -c /userdata --codec mjpeg --file /userdata/mjpeg_frames_dir --pip-enable 1 --pip-overlay /userdata/mjpeg_overlay --pip-x 20 --pip-y 20 --pip-w 640 --pip-h 480 --pip-jpeg-quality 85`
- 主机：
  - `v4l2-ctl -d /dev/videoX --list-formats-ext`
  - H.264：`ffplay -f v4l2 -input_format h264 -video_size 1920x1080 -framerate 25 /dev/videoX`
  - MJPEG：`ffplay -f v4l2 -input_format mjpeg -video_size 1920x1080 -framerate 25 /dev/videoX`

## 4.0.1) PiP 依赖检查（板端）

- 若 PiP 报 `Wrong JPEG library version`，检查板端 `libjpeg.so.62` 与 `libjpeg.so.8` 的链接情况。
- `uvctest` 需链接 `libjpeg.so.8`。

## 4.0.2) PiP 断流与网格 n_active（板端 / 宿主机闸口）

- **宿主机 P3-I4**：`tests/integration/check_pip_helper_no_product_fopen.sh --strict`（**`scripts/run_p3_host_smoke.sh`** 与 **`scripts/run_p5_host_smoke.sh`** 均会调用）；目标为 `src/pip_helper` 与 `src/pip_mjpeg.cpp` 不出现 `fopen`。
- **板端 P3-I5**：`tests/integration/board_pip_stale_and_n_active_smoke.sh check` 查看步骤摘要；需要短时跑进程时用 `smoke`。**`smoke` 默认 `CONFIG=/userdata`（目录）**，`uvctest` 合并 **`libmy_uvc.ini` → `libmy_uvc_pip.ini` → `uvctest.ini`**。单文件 profile（如 **`select_profile --install`**）在板上多为 **`/userdata/profile.ini`**，可 `export CONFIG=/userdata/profile.ini`。若 ini 已配置 `[uvctest] pip_tile_test_nv12_paths` 与 `[libmy_uvc_pip] pip_tile_n_tiles`，可设 `EXPECT_N_ACTIVE` 要求日志含 `pip grid NV12 test n_active=<n>`（示例：`EXPECT_N_ACTIVE=2 board_pip_stale_and_n_active_smoke.sh smoke`，期望出现 **`ch=0 pip grid NV12 test n_active=2`** 且末行 **`board_pip_stale_and_n_active_smoke: ok`**；**2026-04-21** rv1126b-buildroot 已按此通过，见 **`tests/integration/record_release_regression.md`**）。**`my_uvc_install_to_device.sh` 不推送该脚本**，板端须手动同步仓库 `tests/integration` 内同名文件至 `/usr/bin`（或指定路径）并 `chmod +x`；成功时末行 `board_pip_stale_and_n_active_smoke: ok`，且不应再出现 `log: unbound variable`（旧版 `local log` + `EXIT trap` 已修复）。`my_uvc_usb_config.sh … --stop-system-usb` 后内核偶现 `dwc3 … was not queued to ep0out`，与 gadget 断开相关，一般可忽略。串口日志中 `width=19205` / `quality=5` 多为窄终端折行，以 `pip_hw` / UVC 行 `1920x1080` 为准；短时 `timeout` 内主机未开流时 `stats … on=0` 可接受。结果记入 **`tests/integration/record_release_regression.md`**（P3-I5 行）。
- **断流超时**：`[libmy_uvc_pip] pip_overlay_stale_timeout_ms` 与 `pip_helper_create` 字段一致（`-1` 缺省 5000 ms，`0` 仅冻结不上屏背图，`>0` 自定义）。`uvctest` 单文件主讲人 overlay 路径默认每帧刷新时间戳；要在板上观察「无新帧 → 冻结/超时」需集成方使用 `pip_helper_composite_mjpeg_ex` 的 `presenter_nv12_updated` / `tile_nv12_updated`。**网格 per-slot 断流**：`tile_nv12_updated` 非 NULL 时，`tile_nv12_updated[i]==0` 可省略 `tile_nv12[i]`，库内按槽位缓存与超时策略处理。
- **n_active**：`n_active = min(非空 NV12 路径段数, pip_tile_n_tiles)`；路径数小于槽位数时余格为背图（人工预览确认）。

## 4.1) FPS 协商排障

- 现象：主机日志出现 `driver changed the time per frame from 1/25 to 1/5`
- 处理：板端脚本显式指定 fps，必要时追加 `--stop-system-usb`。
- 验证：`v4l2-ctl --list-formats-ext` 显示正确帧率。

## 4.2) 多路 UVC 快速检查

- 2 路示例：
  - 板端：`my_uvc_usb_config.sh -w 1920 -h 1080 -p 25 -n 2 --verbose`
  - 板端：`uvctest --channels 2 -c /userdata`
  - 主机：`v4l2-ctl --list-devices`，分别打开两个视频节点。

## 4.2.1) 4 路独立快速检查

- 板端 USB：`my_uvc_usb_config.sh -w 1920 -h 1080 -p 25 -n 4 --verbose`
- 板端应用：`uvctest -c /userdata`
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
| 稳定优先  | 长时压测  | `log_level=1`, `stats_enable=1`, `startup_prime_frames=8` | `uvctest -c /userdata`                                         |
| 低时延优先 | 调试    | `log_level=0`, `stats_enable=0`, `startup_prime_frames=2` | `uvctest -c /userdata --log-level 0 --stats-enable 0`          |
| 复开流鲁棒 | 高频开关流 | `log_level=2`, `startup_prime_frames=16`                  | `uvctest -c /userdata --log-level 2 --startup-prime-frames 16` |


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

