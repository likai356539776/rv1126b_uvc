# HANDOVER

## 1) 当前状态（精简）

- 项目路径：`/home/kama/workspace/ubuntu20.04/uvc_sigle/my_uvc`
- 目标平台：RV1126B (aarch64)
- 当前分支：`PicInPic`
- 最近提交：`dd25558`  
  `fix(uvc,mjpeg): stabilize frame negotiation and hardware PiP path`
- 当前能力：UVC 多路（1~16）、H.264/MJPEG、USB 热拔插恢复、MJPEG 实时 PiP（MPP+RGA 硬件路径）

## 2) 本轮核心改动

### A. MJPEG PiP 改为硬件流水线（性能路径）
- 新增：`include/mpp_jpeg.h`、`src/mpp_jpeg.cpp`
- `main.cpp` 接入 `pip_hw_*`，每个 `channel_worker` 独立上下文（多线程并行）
- 流水线：`MPP JPEG decode -> RGA resize -> NV12 overlay -> MPP JPEG encode`
- `CMakeLists.txt` 新增 `rockchip_mpp` / `rga` 依赖

### B. 修复 JPEGD 重置与解码失败
- 原因：JPEG 解码输入包使用普通内存，硬件不可直接 DMA
- 修复：decode 输入改为 `MppBuffer` + `mpp_packet_set_buffer`
- 补齐 decoder `info_change` 后 `mpp_buffer_group_limit_config`

### C. UVC 协商/帧率稳定性
- `uvc-gadget.c` 中 MJPEG `dwMaxVideoFrameSize` 调整为压缩场景（避免主机误判带宽）
- 协商日志改为 stderr，并新增可控详日志：
  - 默认简洁：`[uvc] commit ...`
  - 详细开关：`MY_UVC_NEGO=1`
- 避免刷屏日志（如 `rgb_to_nv12 ok`）

### D. USB 配置脚本增强（多路策略）
- 文件：`scripts/my_uvc_usb_config.sh`
- MJPEG `dwFrameInterval` 改为单值（匹配 `-p`），避免主机回退 5fps
- 新增自适应策略：按 `channels + fps` 计算 `dwMaxVideoFrameBufferSize`
- 新增覆盖参数：`--mjpeg-max-frame-size <bytes>`

### E. 配置与文档同步
- `config/my_uvc.ini` 与 `config/profiles/*.ini` 新参数同步：
  - `prefer_host_fps`
  - `pip_*` 参数组（默认 profile 中 `pip_enable=0`）
- `config/` 下全部 `fps` / `channelN_fps` 已统一为 `30`
- `docs/README.md` 已新增 1~8 路 MJPEG USB 推荐命令矩阵

## 3) 关键文件地图（本轮）

- 代码：
  - `src/main.cpp`
  - `src/mpp_jpeg.cpp`
  - `include/mpp_jpeg.h`
  - `third_party/uvc/uvc-gadget.c`
- 脚本：
  - `scripts/my_uvc_usb_config.sh`
- 配置：
  - `config/my_uvc.ini`
  - `config/profiles/my_uvc_*ch_independent.ini`
- 文档：
  - `docs/README.md`

## 4) 运行/验证最小命令

```bash
# 1) USB gadget（示例：单路 MJPEG 640x480@25）
my_uvc_usb_config.sh -f MJPEG -w 640 -h 480 -p 25 -n 1 --stop-system-usb

# 2) 启动应用
my_uvc -c /userdata/my_uvc.ini --codec mjpeg --file /userdata/mjpeg_frames_dir \
  --pip-enable 1 --pip-overlay /userdata/mjpeg_overlay \
  --pip-x 20 --pip-y 20 --pip-w 160 --pip-h 120 --pip-jpeg-quality 85

# 3) 查看协商日志（简版）
grep "\[uvc\] commit" /userdata/my_uvc_pip.log

# 4) 需要协商细节时（仅调试）
export MY_UVC_NEGO=1
```

## 5) 已知风险/注意事项

- 多路（特别 6/8 路）在 USB2 下可能受主机带宽/调度限制，出现“后几路无图”。
- 需要协同调参：`channels`、`fps`、`dwMaxVideoFrameBufferSize`（脚本已提供自动策略和手动覆盖）。
- 若出现 `rc_model_v2 alloc_bits` 断言，已在 `mpp_jpeg.cpp` 中将 MJPEG 编码显式设为 FIXQP+RC 基础参数，需确保部署的是新二进制。

## 6) 下次会话建议起步

1. 先读：`HANDOVER.md`、`docs/README.md`、`config/my_uvc.ini`
2. 优先复核板端实际生效内容：
   - `/sys/kernel/config/usb_gadget/.../dwFrameInterval`
   - `/sys/kernel/config/usb_gadget/.../dwMaxVideoFrameBufferSize`
3. 若多路异常，先固定单路验证，再按 1~8 路矩阵递增定位。
