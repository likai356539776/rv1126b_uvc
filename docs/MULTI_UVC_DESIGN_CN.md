# my_uvc 多路 UVC 设计说明（v2 规划）

## 目标

在当前 `my_uvc` 基础上支持多个 UVC Function（例如 `uvc.gs1`、`uvc.gs2`、`uvc.gs3`...），并保持每一路可独立配置与独立运行。

## v1 基线（历史）

- 单 UVC Function：`uvc.gs1`
- 单码流源：一个 H.264 文件
- `main.cpp` 仅维护一个推流上下文

## 当前 v1.2 状态

- 多路能力已可配置（应用侧 `channels`，USB 脚本侧 `-n/--channels`），当前上限为 16 路。
- 支持每路独立文件与独立 fps 覆盖：
  - `channelN_h264_path`
  - `channelN_fps`
- 已可用于多路枚举测试与基础独立推流验证。
- 已修复“单路开关流影响其他路”的问题，每路启停互不干扰。
- 已加入可观测性与鲁棒性控制：
  - `log_level`（error/info/debug）
  - `stats_enable` + `stats_interval_sec`（每路周期统计）
  - `startup_prime_frames`（复开流时重复首个 IDR+SPS/PPS，提升首屏成功率）

## v2 目标架构

- `UsbGadgetManager`
  - 负责 configfs 多 function 配置
  - 负责 bind/unbind 与通道到 `/dev/videoX` 的映射输出

- `ChannelConfig`（每路配置）
  - `enable`
  - `format`（初期仅 H.264）
  - `width` / `height`
  - `fps`
  - `source_path`
  - `loop_file`

- `ChannelRuntime`（每路运行态）
  - NAL/帧索引
  - SPS/PPS 缓存
  - 主机协商 fps
  - stream on/off 状态
  - 统计信息

- `StreamScheduler`
  - 可采用“每路一线程”或“单循环多路时序”两种策略
  - 保证每路独立节拍与推流稳定性

## 关键实现点

1. **USB 配置脚本**
   - 提供路数与分辨率参数；
   - 创建多个 `uvc.gsN` 并统一挂到同一 config。

2. **UVC 控制链路**
   - 继续复用本地 `third_party/uvc`；
   - 通道标识由检测到的 `/dev/videoX` 决定。

3. **帧源抽象**
   - 逐步从单一文件源过渡到 `IFrameSource` 接口：
     - `FileFrameSource`（当前实现）
     - `VencFrameSource`（后续实时编码输出）

4. **故障隔离**
   - 单路异常不应拖垮其他路；
   - 单路关闭不应触发全局推流退出。

5. **可观测性**
   - 保留每路统计（实时 fps、累计帧数、错误计数、stream 状态）；
   - 保留开关流边沿日志，方便长时压测定位。

## 里程碑（更新）

- M1：双路同源（`uvc.gs1` + `uvc.gs2`） ✅
- M2：双路独立文件 + 独立 fps ✅
- M3：复开流鲁棒性增强（startup priming + 每路 stream gate）✅
- M4：增强错误遥测（每路最近错误原因 + 时间戳）
- M5：接入实时 VENC 源（至少一路）

## 风险点

- 多路 UVC 的主机兼容性受 OS、播放器和驱动栈影响明显；
- 高路数并发时 USB 带宽/主机侧解码能力可能成为瓶颈；
- 高路数下需特别关注日志开销与线程调度开销。

