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

