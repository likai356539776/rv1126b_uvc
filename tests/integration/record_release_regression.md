# Release / regression record (P5-T1)

在合并或打 tag 前，按 `**docs/TEST_CHECKLIST_CN.md**`（全表或 release 子集）执行，并在此记录结果。

**本次记录状态：P5-T1、P5-T3 已测通过**；**闸口 P1-I3、P1-I1**（2026-04-20，kama）；**P2-I1、P2-I3、P3-I1、P3-I2、P3-I3**（2026-04-21，kama）；**P3-I4**（2026-04-21，宿主机 strict，见下表）；**P3-I5**（2026-04-21，板端 rv1126b-buildroot，`smoke` + **`EXPECT_N_ACTIVE=2`** 网格日志闸口通过，见下表）。

## 宿主机（可脚本化）


| Step                                                              | Pass | Notes                                                                                                                                              |
| ----------------------------------------------------------------- | ---- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| `./scripts/run_p5_host_smoke.sh`                                  | ✓    | CTest 全量单测 + `doc_deploy_walkthrough_smoke` + `board_select_profile_uvc_binary_smoke` + `**check_pip_helper_no_product_fopen.sh --strict`**（P3-I4） |
| `./scripts/run_p3_host_smoke.sh`                                  | ✓    | 同上 CTest 子集 + `**check_pip_helper_no_product_fopen.sh --strict**`（P3-I4）；**2026-04-21** 退出码 0。                                                     |
| `tests/integration/board_config_split_vs_monolith_parity.sh`      | ✓    | 与 **test_app_config_path_directory_vs_file** 等价；已纳入 `run_p5_host_smoke`                                                                            |
| `tests/integration/check_libmy_uvc_soname_exports.sh <build-dir>` | ✓    | `**build-rv1126b`**：`nm -D` 导出含 `my_uvc_load_ini_section_only` 等；NEEDED/SONAME 正常                                                                  |
| `tests/integration/check_uvctest_and_lib_deps.sh <build-dir>`     | ✓    | `**build-rv1126b`**（**P5-T3**）：`uvctest` NEEDED 含 `libmy_uvc.so.1`；RPATH 为构建树绝对路径（预期）；`ldd` 在 x86 上对 aarch64 失败属正常，以 readelf 为准                    |


## 板端 + 主机（人工 / adb）


| Step                                                          | Pass | Notes                                  |
| ------------------------------------------------------------- | ---- | -------------------------------------- |
| `my_uvc_usb_config.sh` / USB 枚举                               | ✓    | 按 `**docs/TEST_CHECKLIST_CN.md`** 执行通过 |
| 单路推流 + 主机预览                                                   | ✓    | 应用入口 `**uvctest`**                     |
| 多路                                                            | ✓    |                                        |
| USB 热插拔恢复                                                     | ✓    |                                        |
| MJPEG + PiP（若启用）                                              | ✓    | 视产品配置；已纳入清单验证                          |
| `tests/integration/doc_deploy_walkthrough_smoke.sh board`（可选） | —    | 未单独记录时可跳过                              |


## 板端闸口（P1 / P2 / P3）— 执行记录

与 `**docs/LIBMY_UVC_REFACTOR_TASKS.md**` §3.3 / §4.3 / §5.3 对齐：下表由协助测试 **逐条回填**（Pass：✓ / 失败：✗ / 跳过：—）；**Notes** 可贴最后一行脚本输出或现象。


| ID        | 步骤摘要                                                                                                                                 | Pass | Notes                                                                                                                                                                                                                                                                                                                                                                                        |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------ | ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **P1-I3** | 宿主机：交叉产物 `check_libmy_uvc_soname_exports.sh` + `check_uvctest_and_lib_deps.sh`                                                       | ✓    | **2026-04-20** 两脚本均以退出码 0 结束；`libmy_uvc.so` SONAME `libmy_uvc.so.1`，导出含 `my_uvc_load_ini_section_only` / submit / `my_uvc_*`；`uvctest` NEEDED 含 `libmy_uvc.so.1`；`ldd` 对外架构二进制失败属预期。可选 `**READELF=aarch64-buildroot-linux-gnu-readelf`** 未安装（command not found），以**默认宿主 `readelf`** 结果为准。                                                                                                    |
| **P1-I1** | 板：`board_libmy_uvc_submit_smoke.sh` → `check` 再 `smoke`                                                                              | ✓    | **2026-04-20** rv1126b：先 `my_uvc_usb_config.sh -f MJPEG …`；`smoke` 末行 `board_libmy_uvc_submit_smoke smoke: ok`；日志含 `uvc open succeeded` / gadget 映射；`width=19205` 为窄终端换行视觉拼接，后文 `resolution is 1920x1080` 为准。若 `check` 里 MJPEG 行仍显示 `--size "19"`，为旧脚本或终端 **SIZE** 环境冲突，请同步最新脚本（`SMOKE_SIZE`）或 `unset SIZE`。                                                                                 |
| **P1-I2** | 板：按 `board_usb_replug_channel_submit.sh steps` 做 USB 拔插恢复                                                                            | ✓    | **2026-04-20** `check` ok；旧版 `steps` 在脚本位于 `/usr/bin/` 时找不到 `docs/` 故仅提示见文档（**仓库已内嵌 §4.4.1 回退**）。**闸口通过**仍须人工拔插 + 主机重开摄像头，并确认 `remap video_id` / `ch=… sent=` 等（见清单 §4.4.1）。                                                                                                                                                                                                                 |
| **P2-I1** | 板：`board_uvctest_parity_regression.sh` → `check` 再 `smoke`                                                                           | ✓    | **2026-04-21** `check` / `smoke` 均无问题；与 `**docs/TEST_CHECKLIST_CN.md`** 单路/多路、H.264/MJPEG 行为一致。                                                                                                                                                                                                                                                                                              |
| **P2-I3** | 板：`board_usb_multichannel_remap.sh` → `check` 再 `smoke`（如 2 路 gadget）                                                                | ✓    | **2026-04-21** 多路 `video_id` 重映射与清单相关条一致；脚本通过。                                                                                                                                                                                                                                                                                                                                               |
| **P3-I1** | 板：`board_pip_on_off_compare.sh` → `check` 再 `smoke`（需 MJPEG+PiP 源）                                                                   | ✓    | **2026-04-21** PiP 开/关对比无异常；关路径与无 PiP 一致。                                                                                                                                                                                                                                                                                                                                                    |
| **P3-I2** | 板：`board_pip_per_channel_isolation.sh`（见脚本 `check`/`smoke`）                                                                          | ✓    | **2026-04-21** 多 `channel_id` 下 PiP 状态无串扰。                                                                                                                                                                                                                                                                                                                                                   |
| **P3-I3** | 板：`board_pip_longrun_stress.sh` 或 TEST_CHECKLIST 长稳 §5                                                                               | ✓    | **2026-04-21** 按任务文档任选其一执行，通过；无异常。                                                                                                                                                                                                                                                                                                                                                           |
| **P3-I4** | 宿主机：`tests/integration/check_pip_helper_no_product_fopen.sh --strict`（`run_p3_host_smoke.sh` / `run_p5_host_smoke.sh` 均带 `--strict`） | ✓    | **2026-04-21** 退出码 0；`src/pip_helper` 与 `src/pip_mjpeg.cpp` 无 `fopen`。                                                                                                                                                                                                                                                                                                                       |
| **P3-I5** | 板：`board_pip_stale_and_n_active_smoke.sh` → `check`；按需 `smoke`（可选 `EXPECT_N_ACTIVE`，ini 须含网格 NV12 测试段）                               | ✓    | **2026-04-21** rv1126b-buildroot（复核）：先 `my_uvc_usb_config.sh -f MJPEG -w 1920 -h 1080 -p 30 -n 1 --stop-system-usb`（日志 `Configured UVC MJPEG 1920x1080@30fps channels=1`）；`usbdevice stop` 后内核偶现 `dwc3 … request … was not queued to ep0out`，与断开 gadget 相关，**不作为闸口失败项**。再执行 `EXPECT_N_ACTIVE=2 board_pip_stale_and_n_active_smoke.sh smoke`：`[uvctest][I] ch=0 pip grid NV12 test n_active=2`，末行 `board_pip_stale_and_n_active_smoke: ok`；短时内 `stats … on=0 realtime_fps=0.00 total=0` 可接受。须仓库版脚本（`_PIP_STALE_TMPLOG` + `SMOKE_SIZE`）。窄串口若出现 `width=19205` / `quality=5` 折行，以 `pip_hw` / UVC `1920x1080` 为准。断流语义、ini 见 `**docs/TEST_CHECKLIST_CN.md`** §4.0.2。 |


| **Tester** | kama |
| **Date** | 2026-04-20（P1-I3 / P1-I1）；**2026-04-21**（P2-I1、P2-I3、P3-I1、P3-I2、P3-I3、**P3-I4**、**P3-I5**） |
| **Build** | `build-rv1126b/`（路径示例：`.../uvc_sigle/my_uvc/build-rv1126b`） |

## 汇总

| **Tester** | kama |
| **Date** | 2026-04-20（P1-I3、P1-I1）；**2026-04-21**（P2-I1、P2-I3、P3-I1、P3-I2、P3-I3、P3-I4、P3-I5） |
| **Release / tag** | （待填） |

附：纯文本可另存为同目录 `**record_release_regression.txt`**。