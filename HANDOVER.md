# HANDOVER

## 1) 当前状态（精简）

- 项目路径：`/home/kama/workspace/ubuntu20.04/uvc_pip`
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
- `config/libmy_uvc*.ini` / `uvctest.ini` 与 `config/profiles/*.ini` 新参数同步：
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
  - `config/libmy_uvc.ini`, `config/libmy_uvc_pip.ini`, `config/uvctest.ini`
  - `config/profiles/my_uvc_*ch_independent.ini`
- 文档：
  - `docs/README.md`

## 4) 安装布局（libmy_uvc / uvctest）

- **CMake `install`**：`bin/uvctest`、`lib/libmy_uvc.so*`（SONAME `libmy_uvc.so.1`）、`lib/libmy_uvc_pip_helper.a`、`include/my_uvc/`（**稳定 C ABI**，如 `my_uvc.h`）、`include/my_uvc_pip/`、`share/` 下分文件配置模板。
- **应用入口名**：板端与文档均以 **`uvctest`** 为准（**不再**安装 `/usr/bin/my_uvc` 符号链接；旧脚本请改为 `uvctest` / `pkill -f uvctest`）。
- **推送**：`my_uvc_install_to_device.sh` 将上述产物同步到板端常用路径（与 `docs/README.md` 一致）。
- **第三方只链 `libmy_uvc.so` 时**：可用 **`my_uvc_load_ini_section_only(path, section, …)`**（声明于 `include/my_uvc/my_uvc.h`）从**单个 ini 文件**中只合并一个 `[section]`（如 `libmy_uvc`），得到 `my_uvc_config_t` 再 `my_uvc_create`。区段名与分文件配置对应关系见 `config/README_CONFIG.md`。宿主机/CI 不编全量 `.so` 时仍可用 `scripts/run_unit_tests_host.sh` 中的 `test_my_uvc_load_ini_section_c_api` 验证解析与映射。

### 板端 / SDK 自检（可选）

1. 交叉编译产出目录 `$BUILD` 下执行：  
   `tests/integration/check_libmy_uvc_soname_exports.sh "$BUILD"`  
   脚本会**要求**动态符号表中出现 **`my_uvc_load_ini_section_only`**（与 `my_uvc_create` 等一同导出）。
2. 设备已部署且 `adb` 可用时：  
   `tests/integration/doc_deploy_walkthrough_smoke.sh board`  
   检查 `/usr/bin/uvctest`、`libmy_uvc.so` / `/userdata` 下拆分 ini 是否齐全（详见脚本内说明）。

## 5) 运行/验证最小命令

```bash
# 1) USB gadget（示例：单路 MJPEG 1920x1080@25）
my_uvc_usb_config.sh -f MJPEG -w 1920 -h 1080 -p 25 -n 1 --stop-system-usb

# 2) 启动应用
uvctest -c /userdata --codec mjpeg --file /userdata/mjpeg_frames_dir \
  --pip-enable 1 --pip-overlay /userdata/mjpeg_overlay \
  --pip-x 20 --pip-y 20 --pip-w 640 --pip-h 480 --pip-jpeg-quality 85

# 3) 查看协商日志（简版）
grep "\[uvc\] commit" /userdata/my_uvc_pip.log

# 4) 需要协商细节时（仅调试）
export MY_UVC_NEGO=1
```

## 6) 已知风险/注意事项

- 多路（特别 6/8 路）在 USB2 下可能受主机带宽/调度限制，出现“后几路无图”。
- 需要协同调参：`channels`、`fps`、`dwMaxVideoFrameBufferSize`（脚本已提供自动策略和手动覆盖）。
- 若出现 `rc_model_v2 alloc_bits` 断言，已在 `mpp_jpeg.cpp` 中将 MJPEG 编码显式设为 FIXQP+RC 基础参数，需确保部署的是新二进制。

## 7) 下次会话建议起步

1. 先读：`HANDOVER.md`、`docs/README.md`、`config/README_CONFIG.md`
2. 优先复核板端实际生效内容：
   - `/sys/kernel/config/usb_gadget/.../dwFrameInterval`
   - `/sys/kernel/config/usb_gadget/.../dwMaxVideoFrameBufferSize`
3. 若多路异常，先固定单路验证，再按 1~8 路矩阵递增定位。
