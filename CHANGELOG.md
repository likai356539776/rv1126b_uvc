# Changelog

All notable changes to this project are documented here. The format is informal; release tags may add date-stamped sections.

## [Unreleased]

### AppConfig (P2-T3)

- **`AppConfig`** 拆为 **`libmy_uvc`**（`LibmyUvcIniFields`）、**`libmy_uvc_pip`**（`LibmyUvcPipIniFields`）、**`uvctest`**（`UvctestIniFields`），与 **`config/libmy_uvc.ini` / `libmy_uvc_pip.ini` / `uvctest.ini`** 一一对应；`app_config.cpp` 与各消费者已迁命名成员。说明见 **`config/README_CONFIG.md`**；**`my_uvc.h`** / **`pip_helper.h`** / **`uvctest_cli.hpp`** 补充交叉引用。

### Configuration

- PiP overlay window defaults: **`pip_w`×`pip_h` = 640×480**（左上角小窗分辨率；`default_app_config()`、profiles、文档与板端集成脚本 CLI 已对齐）。
- Default **canvas** resolution set to **1920×1080** in `default_app_config()`, USB/scripts defaults (`my_uvc_usb_config.sh`, `select_profile.sh`, `probe_uvc_node_mapping.sh`), product **profiles** (`config/profiles/*.ini`), unit tests, PiP integration scripts’ `SIZE`, and docs (`REQUIREMENTS.md`, `docs/README.md`, `TEST_CHECKLIST*.md`, `HANDOVER.md`). MJPEG multi-channel matrix in `docs/README.md` §3 remains a **640×480** bandwidth reference; 1080p requires re-tuning `--mjpeg-max-frame-size`.

### Tooling / integration

- **P0-T2**: **`test_app_config_ini_merge`** — 宿主机断言目录合并（`libmy_uvc.ini` → `libmy_uvc_pip.ini` → `uvctest.ini`）、同区段后文件覆盖、`my_uvc.ini` 回退、非法键拒收；集成脚本 **`tests/integration/board_config_directory_load_smoke.sh`**（默认跑 CTest；`board` 检查 `/userdata` 下分文件）。已纳入 **`scripts/run_unit_tests_host.sh`**。

- **P0-T3**: **`tests/integration/board_userdata_config_present_smoke.sh`** — `check`：仓库 **`config/`** 下分文件 ini + **`README_CONFIG.md`** 存在且安装脚本含分文件推送；`board`：adb（或设备本机 **`/userdata`**）断言四份 ini 已部署可读；无设备时与 **`doc_deploy_walkthrough_smoke.sh board`** 相同跳过策略。

- **P1-I1 / P1-I2 / P2-I1 / P2-I3（集成脚本补全）**：**`tests/integration/board_libmy_uvc_submit_smoke.sh`**（`check|smoke`，MJPEG `sent=` / `open uvc` 标记）；**`board_usb_replug_channel_submit.sh`**（`check|steps`，**`TEST_CHECKLIST_CN.md` §4.4** 拔插步骤）；**`board_uvctest_parity_regression.sh`**（`check|smoke`，H.264+MJPEG 短时矩阵 + CTest 指针）；**`board_usb_multichannel_remap.sh`**（`check|smoke`，多路 `channel N mapped video_id=`）。均在板端 UVC + 媒体路径就绪时跑 **`smoke`**；宿主机可只跑 **`check`** 核对命令。

- **uvctest CLI**: `--pip-enable 0` is no longer overridden by later `--pip-overlay` / `--pip-*` (previously `merge_cli_into_config` and `parse_cli` forced `pip_enable=true` when overlay was set). Unit test extended in **`test_uvctest_cli_overrides_ini`**.

- **P4-T1**: `load_app_config_section_from_file()` in `app_config` — merge a single ini applying only one `[section]`; tests `test_my_uvc_ini_section_loader` + `tests/integration/board_ini_loader_parity_smoke.sh`. **`my_uvc_load_ini_section_only()`** in `my_uvc.h` / `src/core/my_uvc_load_ini_section_c.cpp` (C ABI on `libmy_uvc`); unit test `test_my_uvc_load_ini_section_c_api`. Submit path uses `my_uvc_resolve_video_id_for_submit()` + test hook `my_uvc_test_set_video_id_hook()` in `src/core/my_uvc_video_id_resolve.cpp` (**P1-U3** / `test_my_uvc_channel_video_stub`).
- **P3-U3**: `test_pip_compose_offline_golden` — NV12 size + JPEG fixture token (no MPP on host).
- **P5-T4**: GitHub Actions workflow **`.github/workflows/my_uvc_unit_tests.yml`**（**以 `my_uvc` 为 Git 仓库根**时在仓库根执行 `scripts/run_unit_tests_host.sh`，勿再设 `working-directory: my_uvc` 或 `paths: my_uvc/**`）。

### Documentation

- **P4-T2**: Install layout + **`my_uvc_load_ini_section_only`** documented in **`HANDOVER.md` §4**, **`docs/README.md` §6**, **`config/README_CONFIG.md`**. **`doc_deploy_walkthrough_smoke.sh`** `check` asserts `my_uvc.h` contains the C API; **`check_libmy_uvc_soname_exports.sh`** requires the symbol in **`libmy_uvc.so`** DYNSYM.

### Packaging

- **Executable name**: **`uvctest`** only — removed **`/usr/bin/my_uvc`** symlink from **`my_uvc_install_to_device.sh`** and CMake **`install(SCRIPT … install_my_uvc_symlink.cmake)`** (file deleted). **`select_profile.sh`** / **`TEST_CHECKLIST*.md`** / **`HANDOVER.md`** updated. **P4-T3**: **`board_select_profile_uvc_binary_smoke.sh`** asserts suggested commands use **`uvctest`** only.

### Release gate (P5)

- **`scripts/run_p5_host_smoke.sh`**: host-only bundle — **`run_unit_tests_host.sh`** + **`doc_deploy_walkthrough_smoke.sh check`** + **`board_select_profile_uvc_binary_smoke.sh check`**, with pointers to **P5-T2** (CTest `test_app_config_path_directory_vs_file`), **P5-T3** (`check_uvctest_and_lib_deps.sh`), **P5-T1** (manual checklist). **`tests/integration/record_release_regression.md`** — **2026-04-17**：**P5-T1**（`TEST_CHECKLIST_CN.md`）与 **P5-T3**（`check_uvctest_and_lib_deps.sh` + `check_libmy_uvc_soname_exports.sh` on **`build-rv1126b`**）已测通过并记录。
