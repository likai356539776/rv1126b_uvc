# HANDOVER

## 1. 项目基本信息

- 项目目录：`/home/kama/workspace/ubuntu20.04/uvc_sigle/my_uvc`
- 目标平台：RV1126B (aarch64)
- 主要功能：基于 USB Gadget 的 UVC 多路推流（文件源，支持 H.264 / MJPEG；MJPEG 支持实时画中画 PiP）
- 当前代码状态：可运行，已支持高路数配置、一键选择 profile、USB 热拔插恢复

## 2. 项目架构

```
my_uvc/
├── CMakeLists.txt                      # 构建入口
├── autobuild.sh                        # 一键构建脚本
├── my_uvc_install_to_device.sh         # 一键部署脚本
├── cmake/
│   └── toolchain-rv1126b-buildroot.cmake
├── config/
│   ├── my_uvc.ini                      # 主配置模板
│   └── profiles/                       # 产品 profile（1~16路）
│       └── my_uvc_{1,2,4,6,8,10,12,16}ch_independent.ini
├── docs/                               # 文档
│   ├── README.md                       # 文档总入口
│   ├── MULTI_UVC_DESIGN.md             # 多路设计（英文）
│   ├── MULTI_UVC_DESIGN_CN.md          # 多路设计（中文）
│   ├── TEST_CHECKLIST.md               # 测试清单（英文）
│   └── TEST_CHECKLIST_CN.md            # 测试清单（中文）
├── include/
│   ├── app_config.h                    # 应用配置结构定义
│   └── pip_mjpeg.h                     # PiP 画中画接口
├── scripts/
│   ├── my_uvc_usb_config.sh            # USB gadget 配置脚本
│   ├── probe_uvc_node_mapping.sh       # UVC 节点映射探测
│   └── select_profile.sh              # 产品 profile 选择/部署/启动
├── src/
│   ├── main.cpp                        # 主入口：参数解析、通道工作线程
│   ├── app_config.cpp                  # INI 配置解析
│   ├── pip_mjpeg.cpp                   # MJPEG 画中画合成（libjpeg）
│   └── uevent_stub.c                   # netlink uevent 监听（USB 热拔插检测）
└── third_party/uvc/                    # UVC Gadget 核心库（基于 rkipc 本地化）
    ├── uvc-gadget.c / .h              # V4L2 UVC 设备事件循环、缓冲区管理
    ├── uvc_control.c / .h             # UVC 控制线程、设备扫描与生命周期
    ├── uvc_video.cpp / .h             # UVC 视频线程管理、run_state 控制
    ├── uvc_encode.c / .h              # 编码格式初始化
    ├── yuv.c / .h                     # YUV 转换辅助
    └── uevent.h                       # uevent 接口声明
```

## 3. 当前已完成能力

### 3.1 基础推流
- UVC 推流（H.264 / MJPEG），配置文件路径 `/userdata/my_uvc.ini`
- 多路支持：1~16 路，应用侧与 USB 脚本侧均已支持
- 每路独立配置：`channelN_h264_path`、`channelN_fps`（N=0..15）

### 3.2 MJPEG 帧源
- 单 `.jpg/.jpeg` 文件
- 多 JPEG 拼接文件（按 `0xFFD8` 分帧）
- 目录（读取目录下所有 `.jpg/.jpeg`，按文件名排序）

### 3.3 MJPEG 实时画中画（PiP）
- `pip_enable` / `pip_overlay_path`（文件或目录轮播）
- `pip_x/pip_y/pip_w/pip_h/pip_jpeg_quality`

### 3.4 复开流鲁棒性
- `sync_to_idr_on_open` / `inject_sps_pps_on_idr` / `startup_prime_frames`

### 3.5 日志与统计
- `log_level`（0=error / 1=info / 2=debug）
- `stats_enable` + `stats_interval_sec`（每路周期统计）

### 3.6 USB 热拔插恢复
- netlink uevent 监听（`uevent_stub.c`）：检测 `video4linux` 设备增删事件
- UVC 工作线程原地存活：USB 断开时立即释放缓冲区（munmap + REQBUFS(0)），但线程不退出
- USB 重连后内核发送新 STREAMON → 线程分配全新缓冲区 → 自动恢复推流
- `uvc_control_thread` 在设备节点丢失时以 500ms 间隔轮询重新扫描
- `channel_worker` 支持动态 `video_id` 重映射

### 3.7 产品化 Profile
- 提供 1/2/4/6/8/10/12/16 路独立配置模板
- `select_profile.sh` 一键选择、部署、启动

## 4. 关键运行时线程架构

```
main()
 ├── uvc_control_thread          # 设备扫描与生命周期（事件驱动）
 │    └── uvc_gadget_pthread ×N  # 每个 video_id 一个线程（V4L2 事件 + 数据循环）
 ├── uevent_monitor_thread       # netlink 监听 USB 设备变化
 ├── channel_worker ×N           # 每路帧数据填充线程
 └── stats_worker                # 统计输出线程
```

**USB 热拔插恢复流程：**
1. USB 断开 → `uvc_gadget_pthread` 遇到 ENODEV → 释放 mmap 缓冲区 → `is_streaming=0`
2. 线程在 `select()` 中以 2s 超时等待（不退出、不空转）
3. USB 重连 → 内核在同一设备节点发送 STREAMON → `uvc_handle_streamon_event()` 分配新缓冲区
4. 推流自动恢复

## 5. 关键脚本

### 5.1 `scripts/select_profile.sh`

```bash
# 4路部署并启动
./scripts/select_profile.sh 4 --install --run --fps 20 --size 1280x720
# 16路部署并启动
./scripts/select_profile.sh 16 --install --run --fps 10 --size 640x480
```

### 5.2 `my_uvc` 常用参数

- `-c <config>` / `--channels` / `--file` / `--codec`
- `--width` / `--height` / `--size WxH` / `--fps`
- `--log-level` / `--stats-enable` / `--stats-interval`
- `--pip-enable` / `--pip-overlay` / `--pip-x/y/w/h` / `--pip-jpeg-quality`
- `--startup-prime-frames`

## 6. 关键配置文件

- 主配置模板：`config/my_uvc.ini`
- 产品 profile：`config/profiles/my_uvc_{1..16}ch_independent.ini`

## 7. 文档入口

| 文档 | 路径 |
|------|------|
| 文档总入口 | `docs/README.md` |
| 需求文档 | `REQUIREMENTS.md` |
| 多路设计（EN/CN） | `docs/MULTI_UVC_DESIGN.md` / `docs/MULTI_UVC_DESIGN_CN.md` |
| 测试清单（EN/CN） | `docs/TEST_CHECKLIST.md` / `docs/TEST_CHECKLIST_CN.md` |

## 8. 已知注意事项

- 高路数（>=10）对 USB 带宽、主机侧解码能力和调度压力较敏感，建议先降 fps 再逐步上调。
- `--fps`（USB 协商帧率）与 `my_uvc.ini` 的每路 `channelN_fps` 需要协同配置。
- 若复开流出现 `non-existing PPS`，优先上调 `startup_prime_frames`（按 +2 递增）。
- Buildroot 注意：若板端同时存在 `libjpeg.so.62` 与 `libjpeg.so.8`，PiP 需要链接 `libjpeg.so.8`。
- USB 拔插调试建议使用 `my_uvc_usb_config.sh ... --stop-system-usb`，避免系统 USB 服务覆盖。
- USB 热拔插恢复依赖设备节点在 USB 断开/重连期间保持存在（Rockchip configfs gadget 的默认行为）。
- 多路 6/7/8 在 USB2（480Mbps）下可能因 Host 带宽/驱动限制无法全部出数据，需确认 DTS 是否支持 USB3 gadget。

## 9. 新会话快速恢复模板

```text
项目路径：/home/kama/workspace/ubuntu20.04/uvc_sigle/my_uvc

请先阅读这些文件再继续：
1) HANDOVER.md
2) REQUIREMENTS.md
3) docs/README.md

我现在要做的下一步：
- （在这里写当前目标）
```
