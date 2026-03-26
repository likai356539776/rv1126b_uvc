# HANDOVER

## 1. 项目基本信息

- 项目目录：`/home/kama/workspace/ubuntu20.04/uvc_sigle/my_uvc`
- 目标平台：RV1126B (aarch64)
- 主要功能：基于 USB Gadget 的 H.264 UVC 多路推流（文件源）
- 当前代码状态：可运行，已支持高路数配置与一键选择 profile

## 2. 当前已完成能力（关键）

- 支持 H.264 UVC 推流，默认配置文件路径为：
  - `/userdata/my_uvc.ini`
- 支持多路范围：
  - 应用侧：`1..16`
  - USB 配置脚本侧：`1..16`
- 支持每路独立配置：
  - `channelN_h264_path`（N=0..15）
  - `channelN_fps`（N=0..15）
- 支持复开流鲁棒性参数：
  - `sync_to_idr_on_open`
  - `inject_sps_pps_on_idr`
  - `startup_prime_frames`
- 支持日志与统计：
  - `log_level`
  - `stats_enable`
  - `stats_interval_sec`
- 提供产品化 profile（独立路）：
  - `1/2/4/6/8/10/12/16` 路

## 3. 关键脚本与参数

### 3.1 `scripts/select_profile.sh`

- 支持 profile：
  - `1|2|4|6|8|10|12|16`
- 关键参数：
  - `--install`：部署 profile
  - `--run`：板端自动执行 USB 配置并后台启动 `my_uvc`
  - `--fps`：USB 配置脚本 fps（`5/10/15/20/25/30`）
  - `--size`：USB+应用分辨率（`WxH`）
  - `--adb-serial`：指定设备
  - `--remote-config`：板端配置路径（默认 `/userdata/my_uvc.ini`）

示例：

```bash
./scripts/select_profile.sh 4 --install --run --fps 20 --size 1280x720
./scripts/select_profile.sh 16 --install --run --fps 10 --size 640x480
```

### 3.2 `my_uvc` 可执行参数（常用）

- `-c <config>`
- `--channels`
- `--file`
- `--width` / `--height`
- `--size WxH`（等价于同时设置 width/height）
- `--fps`
- `--log-level`
- `--stats-enable`
- `--stats-interval`
- `--startup-prime-frames`

## 4. 关键配置文件

- 主配置模板：
  - `config/my_uvc.ini`
- 产品 profile：
  - `config/profiles/my_uvc_1ch_independent.ini`
  - `config/profiles/my_uvc_2ch_independent.ini`
  - `config/profiles/my_uvc_4ch_independent.ini`
  - `config/profiles/my_uvc_6ch_independent.ini`
  - `config/profiles/my_uvc_8ch_independent.ini`
  - `config/profiles/my_uvc_10ch_independent.ini`
  - `config/profiles/my_uvc_12ch_independent.ini`
  - `config/profiles/my_uvc_16ch_independent.ini`

## 5. 文档入口

- 文档总入口：
  - `docs/README.md`
- 测试清单：
  - `docs/TEST_CHECKLIST.md`
  - `docs/TEST_CHECKLIST_CN.md`
- 多路设计：
  - `docs/MULTI_UVC_DESIGN.md`
  - `docs/MULTI_UVC_DESIGN_CN.md`
- 需求文档：
  - `REQUIREMENTS.md`

## 6. 已知注意事项

- 高路数（>=10）对 USB 带宽、主机侧解码能力和调度压力较敏感，建议先降 fps 再逐步上调。
- `--fps`（USB 协商帧率）与 `my_uvc.ini` 的每路 `channelN_fps` 需要协同配置，避免“协商值与推流节拍不一致”。
- 若复开流出现 `non-existing PPS`，优先上调 `startup_prime_frames`（建议按 +2 递增）。
- **多路 6/7/8 在 PC 上无数据**：板端与 `f_uvc`/应用侧已验证 8 路均在推流；问题集中在 **Host USB2（480M）+ 多路等时 UVC 的资源分配/驱动行为**。DTS 是否仅 HS、能否改 USB3 需单独确认；勿再堆叠用户态“通道映射”实验代码。

## 7. 新会话快速恢复模板（复制可用）

```text
项目路径：/home/kama/workspace/ubuntu20.04/uvc_sigle/my_uvc

请先阅读这些文件再继续：
1) HANDOVER.md
2) REQUIREMENTS.md
3) docs/README.md
4) docs/TEST_CHECKLIST.md
5) docs/MULTI_UVC_DESIGN.md
6) scripts/select_profile.sh

我现在要做的下一步：
- （在这里写当前目标，例如：调试16路稳定性、优化某一路掉帧问题）
```

## 8. 最近会话参考

- 可参考历史会话：[UVC 多路开发交接](d414471b-c134-4b83-93d0-e0a99b9edfed)

## 9. 会话压缩记录（供下次恢复上下文，2025-03）

### 9.1 现象（已复现）

- **板端**：8 路 `my_uvc` 统计正常（`stream ON`、帧数增长、`errors=0`）；内核 `uvc_function_bind` / `set_alt(…,1)` 对 8 路均出现。
- **Host（Ubuntu）**：`v4l2-ctl --stream-to` 对 **偶数 video 节点** 对应关系大致为 ch1→`/dev/video2`，ch2→`video4` … ch5→`video10` 有数据；**ch6→`video12`、ch7→`video14`、ch8→`video16` 文件恒为 0 字节**（或长时间无进度需 Ctrl+C）。
- **路数规律**：**4 路全好**；**6 路仅第 6 路无数据**；**8 路为第 6、7、8 路无数据**。
- **Host `dmesg`**：抓取 `uvc|video|usb` 时多为空，未见典型 “bandwidth” 报错。
- **`lsusb -t`**：`my_uvc` 挂在 **Bus 001、480M（USB2 High-Speed）**；物理口为蓝色 USB3 口 ≠ 当前链路速率（需看 SoC/DTS 是否仅 gadget HS）。
- **降负载**：仅把 USB 脚本 **fps 改为 10**（640×480、8 路）后，**6/7/8 仍无数据** → 单纯“码率减半”不足以解释，更倾向 **Host 对单设备多路等时流的数量/实现限制** 或 **控制器分配策略**。

### 9.2 板端“启动失败”误判（已澄清）

- 日志 `load stream failed for channel 0 (/userdata/ch0.h264)`：**ini 中 H.264 路径文件不存在**；补齐或改路径后进程可常驻。
- `nohup` 默认写 **`nohup.out`**；建议固定：`nohup … >/userdata/my_uvc.log 2>&1 &`（注意 `2>&1` 勿写成 `2>1`）。

### 9.3 内核调试补丁（SDK 路径，非本仓库）

- 位置：`/home/kama/workspace/ubuntu20.04/rp_rv1126_sdk/kernel-6.1/`
- 已加日志（便于区分控制面/数据面）：
  - `drivers/usb/gadget/function/f_uvc.c`：`bind` 时打印 `control/streaming` 接口号与所选 `ep`；`set_alt` 详情；`config_ep_by_speed` / `usb_ep_enable` 失败码。
  - `drivers/usb/dwc3/gadget.c`：`ep_enable` 失败、`__dwc3_gadget_ep_queue` 拒绝原因（ratelimited）。
  - `drivers/usb/gadget/function/uvc_video.c`：`usb_ep_queue` 失败与 `complete` 非 0 状态时带 `stream_intf`/`ep`。
- **当前抓到的现象**：未见 `queue detail` / `complete detail` / `ep_enable failed` 洪水；与板端持续送帧一致。

### 9.4 配置测试约定（带宽实验）

- **可先只改脚本 + `--size`**：`my_uvc_usb_config.sh -w/-h/-p/-n` 与 `my_uvc --size WxH` 对齐即可；`my_uvc.ini` 内 `channelN_fps` 建议最终与协商 fps 一致，但**非快速验证必要条件**。
- 尚未确认结果：**320×240 @ 10fps、8 路** 是否能让 Host 上 ch6–8 出数据；**换 USB3 线** 在 **gadget 仍为 HS** 时通常不改变 480M 事实，**关键在 DTS/硬件是否 SuperSpeed gadget**。

### 9.5 建议的下一步（给下一会话）

1. Host：`lsusb -v -s <bus:dev> | grep -i bcdUSB` 确认设备报告版本；板端 DTS 查 `dwc3`/USB DRD `maximum-speed`、`dr_mode`。
2. 跑一轮 **8 路 320×240 @ 10fps**，Host 再测 `video12/14/16`。
3. 若仍为 0：在 **另一台 PC 或 USB 控制器** 上复现，或接受 **USB2 单口多路 UVC 的工程上限约 5 路**（与当前 4/6/8 规律一致）作为产品规格输入。

