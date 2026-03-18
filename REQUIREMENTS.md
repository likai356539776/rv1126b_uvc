# my_uvc 应用需求文档（v0.4）

## 1. 文档目的

本文档用于定义 `my_uvc` 项目的 v1 最小可运行需求。

## 2. 项目目标

在 RV1126B 平台上实现一个最基本可运行的 UVC Gadget 应用，整体逻辑参考 `rkipc` 的 UVC 方案，但范围收敛为单路最小实现，便于快速验证。

## 3. v1 已确认约束

- 格式优先级：仅支持 `H.264`
- 默认分辨率：`640x480`（可配置）
- 帧源路径：直接读取文件 `/userdata/200frames_count.h264`（循环送帧）
- UAC：关闭（v1 不包含音频）
- 部署方式：本地交叉编译，手动拷贝程序与脚本到设备，手动执行测试
- 当前阶段：以本需求为基线迭代实现与验证

## 4. 开发与构建环境

- 开发主机：Ubuntu 24.04 x86_64
- 编程语言：
  - C++：`gnu++17`（主）
  - C：`gnu11`（兼容/辅助）
- 编码风格：K&R
- CMake：>= 3.16（当前 3.28.3）
- 交叉编译器：Buildroot `aarch64-buildroot-linux-gnu-g++` (GCC 13.3.0)
- Buildroot Host：
  - `/home/kama/workspace/ubuntu20.04/rp_rv1126_sdk/buildroot/output/rockchip_rv1126b/host`
- Sysroot：
  - `<BUILDROOT_HOST>/aarch64-buildroot-linux-gnu/sysroot`
- Rockit SDK：
  - `/home/kama/workspace/ubuntu20.04/rv1126b_dvr/rockit/mpi/sdk`
- 目标平台：RV1126B (aarch64)

## 5. v1 功能范围（In Scope）

### 5.1 USB Gadget 配置

- 提供一个手动执行脚本（例如 `my_uvc_usb_config.sh`），完成：
  - configfs 挂载与初始化
  - `uvc.gs1` 创建与参数写入
  - UDC 绑定
- 脚本仅配置单路 UVC，默认 `H.264 + 640x480`，并支持通过参数覆盖分辨率

### 5.2 UVC 协商与推流

- 应用支持 UVC 基本协商流程（probe/commit、stream on/off）
- 主机发起拉流后，应用持续推送 H.264 帧数据
- 帧数据来源为文件 `/userdata/200frames_count.h264`
- 文件读到末尾后循环回到起始位置继续送帧

### 5.3 运行控制

- 支持前台运行日志输出
- 支持 Ctrl+C / SIGTERM 优雅退出
- 退出时释放线程、关闭 fd、清理缓存状态

## 6. v1 非目标（Out of Scope）

- 多路 UVC（`uvc.gs2` / `uvc.gs3`，v1 暂不做，后续阶段实现）
- UAC / UAC2 音频
- 复杂 UVC 扩展单元控制（XU 高级特性）
- 直接接入 VI/VENC 实时采集编码链路
- RTSP/RTMP/存储/NPU/Web 等 IPC 业务

补充说明：项目路线为“先单路调通并稳定，再扩展多路 UVC”。

## 7. 逻辑设计约束（参考 rkipc）

v1 模块拆分遵循 `rkipc` 的核心思路：

- `usb_config`：负责 gadget/configfs 配置
- `uvc_core`：负责 UVC 控制、事件处理、stream 状态管理
- `frame_source_file`：负责 H.264 文件读取与循环
- `frame_bridge`：负责把帧写入 UVC 发送缓冲
- `app_main`：负责 init/run/deinit 编排

说明：v1 的 `frame_source_file` 替代 `rkipc` 中从 `VENC` 获取码流的路径，用于先完成最小闭环验证。

## 8. 目录与产物要求

### 8.1 目录（计划）

- `my_uvc/`
- `my_uvc/src/`
- `my_uvc/include/`
- `my_uvc/scripts/`
- `my_uvc/config/`
- `my_uvc/cmake/`
- `my_uvc/third_party/uvc/`（本地拷贝 UVC 核心代码，不依赖外部目录）
- `my_uvc/docs/`

### 8.2 交付产物（v1）

- 可执行程序：`my_uvc`
- 启动脚本：`my_uvc_usb_config.sh`
- 配置文件：`my_uvc.ini`（最小配置）
- 快速构建脚本：`autobuild.sh`
- 快速部署脚本：`my_uvc_install_to_device.sh`
- 本文档：`REQUIREMENTS.md`

## 9. 手动部署与验证流程要求

### 9.1 部署方式

- 在开发机完成交叉编译
- 手动拷贝到设备（程序 + 脚本 + 配置）
- 设备端手动执行脚本与程序

### 9.2 建议验证步骤

- 步骤1：执行 USB 配置脚本（建议带详细日志）
  - 示例：`my_uvc_usb_config.sh -w 640 -h 480 -p 25 --verbose`
  - 说明：脚本默认会先执行 UDC 解绑再重绑（解绑失败按非致命处理）
  - 可选：如需跳过解绑可使用 `--no-unbind`
- 步骤2：启动 `my_uvc`
- 步骤3：主机端识别 UVC 设备并打开预览/拉流
  - 示例：`ffplay -f v4l2 -input_format h264 -video_size 640x480 -framerate 25 /dev/videoX`
- 步骤4：连续运行稳定性观察（至少 5 分钟）
- 步骤5：停止程序并检查是否正常退出

### 9.3 运行参数化（当前已实现）

`my_uvc.ini` 支持以下关键参数：

- `width` / `height` / `fps`（当前默认 `640x480@25fps`）
- `h264_path`
- `loop_file`
- `prefer_host_fps`（是否优先使用主机协商 fps）
- `sync_to_idr_on_open`（流开启时是否从 IDR 同步）
- `inject_sps_pps_on_idr`（IDR 前是否注入 SPS/PPS）
- `log_every_frames`（日志打印间隔，0 表示关闭周期日志）
- `idle_sleep_ms`（非 streaming 状态下休眠时长）

`my_uvc_usb_config.sh` 关键参数：

- `-w` / `-h`：分辨率
- `-p` / `--fps`：期望协商帧率（支持 `5/10/15/20/25/30`）
- `--verbose`：打印详细配置日志
- `--no-unbind`：跳过 UDC 解绑（默认会先解绑再重绑）

## 10. 验收标准（v1）

- AC-1：可在指定交叉工具链下成功编译
- AC-2：设备端可正常启动，不崩溃
- AC-3：主机可识别为 UVC 摄像头
- AC-4：可持续输出 H.264 视频流（来源文件循环）
- AC-5：优雅退出，无明显资源泄漏迹象

## 11. 风险与注意事项

- 文件码流可播放不代表严格符合所有 UVC 主机端解码兼容性
- 若主机端对 H.264 Annex-B / 帧边界有要求，需在实现阶段明确分帧策略
- configfs/UDC 节点路径可能因板端镜像配置不同而有差异，脚本需留可配置项

## 12. 当前实现状态（单路）

- 已实现并验证单路 UVC 出图（H.264, 640x480）
- 已验证主机端按 25fps 协商并播放（`v4l2-ctl` 可见 `0.040s (25fps)`）
- 已切换为按帧（AU）发送，并支持 IDR 同步与 SPS/PPS 注入
- 代码构建已完全在 `my_uvc` 目录内完成，不再引用 `rkipc` 源文件路径

## 13. 后续规划

- 详细测试流程见：`docs/TEST_CHECKLIST.md`
- 多路架构设计见：`docs/MULTI_UVC_DESIGN.md`

---

若以上文档内容有需要修改的地方，请直接指出条目编号或粘贴改动意见，我会继续迭代更新。
