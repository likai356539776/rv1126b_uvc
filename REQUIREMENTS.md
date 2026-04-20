# my_uvc 应用需求文档（v0.6）

## 1. 文档目的

本文档用于定义 `my_uvc` 项目的功能需求与验收标准。

## 2. 项目目标

在 RV1126B 平台上实现可运行、可配置、可压测的 UVC Gadget 应用，整体逻辑参考 `rkipc` 的 UVC 方案，并在 `my_uvc` 内独立完成源码与构建闭环。

## 3. 已确认约束

- 格式优先级：支持 `H.264` 与 `MJPEG`
- 默认分辨率：`1920x1080`（可配置）
- 帧源路径：
  - H.264：直接读取文件（循环送帧）
  - MJPEG：可读取 `.jpg/.jpeg`、多 JPEG 拼接文件（按 `0xFFD8` 分帧）、或目录（按文件名排序）
- UAC：关闭（不包含音频）
- 部署方式：本地交叉编译，手动拷贝程序与脚本到设备，手动执行测试

## 4. 开发与构建环境

- 开发主机：Ubuntu 24.04 x86_64
- 编程语言：C++ `gnu++17`（主）/ C `gnu11`（辅助）
- 编码风格：K&R
- CMake：>= 3.16（当前 3.28.3）
- 交叉编译器：Buildroot `aarch64-buildroot-linux-gnu-g++` (GCC 13.3.0)
- Buildroot Host：`/home/kama/workspace/ubuntu20.04/rp_rv1126_sdk/buildroot/output/rockchip_rv1126b/host`
- Sysroot：`<BUILDROOT_HOST>/aarch64-buildroot-linux-gnu/sysroot`
- 目标平台：RV1126B (aarch64)

## 5. 功能范围

### 5.1 USB Gadget 配置

- 提供 `my_uvc_usb_config.sh` 脚本完成 configfs 挂载、`uvc.gsN` 创建与 UDC 绑定
- 支持格式/分辨率/fps/路数参数化（`-f H.264|MJPEG -w -h -p -n`）
- 支持 `--stop-system-usb` 避免系统 USB 服务覆盖

### 5.2 UVC 协商与推流

- UVC 基本协商流程（probe/commit、stream on/off）
- 按路持续推送帧数据（H.264 或 MJPEG），文件循环送帧
- 流开启时 IDR 对齐与 SPS/PPS 注入
- 开流保活 `startup_prime_frames`（提升复开流首屏成功率）
- MJPEG 实时画中画（PiP）：解码背景与叠加图、合成后再编码 JPEG

### 5.3 多路支持

- 应用侧 1~16 路，USB 脚本侧 1~16 路
- 每路独立文件（`channelN_h264_path`）与独立帧率（`channelN_fps`）
- 单路启停不影响其他路

### 5.4 USB 热拔插恢复

- netlink uevent 监听 `video4linux` 设备增删事件
- USB 断开时 UVC 工作线程立即释放缓冲区（munmap + REQBUFS(0)），但线程不退出
- USB 重连后线程在同一 fd 上接收新 STREAMON 事件，分配全新缓冲区恢复推流
- `uvc_control_thread` 在设备节点丢失时以 500ms 间隔轮询重新扫描
- `channel_worker` 支持动态 `video_id` 重映射

### 5.5 运行控制

- 日志级别控制（error/info/debug）
- 多路状态统计输出（每路实时 fps、累计帧数、错误计数）
- Ctrl+C / SIGTERM 优雅退出
- 退出时释放线程、关闭 fd、清理缓存状态

## 6. 非目标（Out of Scope）

- UAC / UAC2 音频
- 复杂 UVC 扩展单元控制（XU 高级特性）
- 直接接入 VI/VENC 实时采集编码链路
- RTSP/RTMP/存储/NPU/Web 等 IPC 业务

## 7. 目录与产物

### 7.1 目录结构

```
my_uvc/
├── src/                     # 应用源码
├── include/                 # 应用头文件
├── third_party/uvc/         # UVC 核心库（本地化）
├── scripts/                 # USB 配置与部署脚本
├── config/                  # 配置文件与产品 profile
├── cmake/                   # 交叉编译工具链
└── docs/                    # 文档
```

### 7.2 交付产物

- 可执行程序：`uvctest`
- 启动脚本：`my_uvc_usb_config.sh`
- 配置文件：`my_uvc.ini`
- 构建脚本：`autobuild.sh`
- 部署脚本：`my_uvc_install_to_device.sh`
- Profile 选择脚本：`scripts/select_profile.sh`
- 产品配置模板：`config/profiles/my_uvc_{1..16}ch_independent.ini`

### 7.3 构建命令

```bash
./autobuild.sh --release
./autobuild.sh --debug --clean
./autobuild.sh -c -d
./autobuild.sh --release --install
./scripts/select_profile.sh 4 --install --run --fps 20 --size 1280x720
```

## 8. 运行参数

### A) 应用配置 `my_uvc.ini`

| 参数名 | 默认值 | 有效范围 | CLI 覆盖 | 说明 |
|---|---:|---|---|---|
| `channels` | `1` | `1..16` | `--channels` | UVC 路数 |
| `width` | `1920` | `>0` | `--width` | 输出宽度 |
| `height` | `1080` | `>0` | `--height` | 输出高度 |
| `size` | `1920x1080` | `WxH` | `--size` | 一次性覆盖宽高 |
| `fps` | `25` | `1..120` | `--fps` | 全局默认帧率 |
| `video_codec` | `h264` | `h264\|mjpeg` | `--codec` | 负载格式 |
| `h264_path` | `/userdata/200frames_count.h264` | 非空路径 | `--file` | 媒体路径 |
| `channelN_h264_path` | 继承 | `N=0..15` | 否 | 每路文件覆盖 |
| `channelN_fps` | 继承 | `N=0..15` | 否 | 每路帧率覆盖 |
| `loop_file` | `1` | `0/1` | 否 | 文件循环 |
| `sync_to_idr_on_open` | `1` | `0/1` | 否 | IDR 对齐 |
| `inject_sps_pps_on_idr` | `1` | `0/1` | 否 | SPS/PPS 注入 |
| `startup_prime_frames` | `8` | `0..120` | `--startup-prime-frames` | 开流保活帧数 |
| `log_level` | `1` | `0/1/2` | `--log-level` | 日志级别 |
| `stats_enable` | `1` | `0/1` | `--stats-enable` | 统计输出开关 |
| `stats_interval_sec` | `5` | `1..3600` | `--stats-interval` | 统计周期(秒) |
| `pip_enable` | `0` | `0/1` | `--pip-enable` | PiP 开关 |
| `pip_overlay_path` | 空 | 路径 | `--pip-overlay` | 叠加图路径 |
| `pip_x/y/w/h` | `20/20/640/480` | 整数 | `--pip-x/y/w/h` | 小窗位置与大小 |
| `pip_jpeg_quality` | `85` | `1..100` | `--pip-jpeg-quality` | 合成 JPEG 质量 |

### B) USB 配置脚本 `my_uvc_usb_config.sh`

| 参数 | 默认值 | 说明 |
|---|---:|---|
| `-f` | `H.264` | 负载格式 (`H.264\|MJPEG`) |
| `-w` | `1920` | 宽度 |
| `-h` | `1080` | 高度 |
| `-p/--fps` | `25` | 帧率 (`5/10/15/20/25/30`) |
| `-n/--channels` | `1` | 路数 (`1..16`) |
| `--verbose` | 关闭 | 详细日志 |
| `--stop-system-usb` | 关闭 | 停止系统 USB 服务 |

### C) Profile 选择脚本 `scripts/select_profile.sh`

| 参数 | 说明 |
|---|---|
| 位置参数 | 路数 (`1/2/4/6/8/10/12/16`) |
| `--install` | 部署到板端 |
| `--run` | 自动启动 |
| `--fps` | USB 帧率 |
| `--size` | 分辨率 (`WxH`) |
| `--stop-system-usb` | 透传给 USB 脚本 |
| `--adb-serial` | 指定设备 |

## 9. 验收标准

- AC-1：可在指定交叉工具链下成功编译
- AC-2：设备端可正常启动，不崩溃
- AC-3：主机可识别为 UVC 摄像头
- AC-4：可持续输出 H.264 或 MJPEG 视频流（来源文件循环）
- AC-5：MJPEG + PiP 画中画可用（板端实时合成）
- AC-6：优雅退出，无明显资源泄漏迹象
- AC-7：多路独立开关流时，单路启停不影响其他路
- AC-8：`stats_enable=1` 时可持续输出每路统计信息
- AC-9：USB 拔线后重新插入，推流可自动恢复

## 10. 当前实现状态

- 已实现并验证单路/多路 UVC 出图（H.264 / MJPEG，默认 1920x1080，可降分辨率验证）
- 已验证主机端按 25fps 协商并播放
- 已支持每路独立文件与每路独立 fps 配置
- 已支持 IDR 同步、SPS/PPS 注入、开流保活
- 已支持 MJPEG 实时画中画（PiP）
- 已支持日志级别控制与多路统计输出
- 已支持 USB 热拔插恢复（线程原地存活 + 缓冲区即时清理 + 自动重建流）
- 代码构建完全在 `my_uvc` 目录内完成，不再引用 `rkipc` 源文件路径

## 11. 文档索引

| 文档 | 路径 |
|------|------|
| 交接文档 | `HANDOVER.md` |
| 文档入口 | `docs/README.md` |
| 测试清单 | `docs/TEST_CHECKLIST.md` / `docs/TEST_CHECKLIST_CN.md` |
| 多路设计 | `docs/MULTI_UVC_DESIGN.md` / `docs/MULTI_UVC_DESIGN_CN.md` |
