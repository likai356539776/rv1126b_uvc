# Release / regression record (P5-T1)

在合并或打 tag 前，按 **`docs/TEST_CHECKLIST_CN.md`**（全表或 release 子集）执行，并在此记录结果。

**本次记录状态：P5-T1、P5-T3 已测通过**（见下方日期与说明）。

## 宿主机（可脚本化）

| Step | Pass | Notes |
|------|:----:|-------|
| `./scripts/run_p5_host_smoke.sh` | ✓ | CTest 全量单测 + `doc_deploy_walkthrough_smoke` + `board_select_profile_uvc_binary_smoke` |
| `tests/integration/board_config_split_vs_monolith_parity.sh` | ✓ | 与 **test_app_config_path_directory_vs_file** 等价；已纳入 `run_p5_host_smoke` |
| `tests/integration/check_libmy_uvc_soname_exports.sh <build-dir>` | ✓ | **`build-rv1126b`**：`nm -D` 导出含 `my_uvc_load_ini_section_only` 等；NEEDED/SONAME 正常 |
| `tests/integration/check_uvctest_and_lib_deps.sh <build-dir>` | ✓ | **`build-rv1126b`**（**P5-T3**）：`uvctest` NEEDED 含 `libmy_uvc.so.1`；RPATH 为构建树绝对路径（预期）；`ldd` 在 x86 上对 aarch64 失败属正常，以 readelf 为准 |

## 板端 + 主机（人工 / adb）

| Step | Pass | Notes |
|------|:----:|-------|
| `my_uvc_usb_config.sh` / USB 枚举 | ✓ | 按 **`docs/TEST_CHECKLIST_CN.md`** 执行通过 |
| 单路推流 + 主机预览 | ✓ | 应用入口 **`uvctest`** |
| 多路 | ✓ | |
| USB 热插拔恢复 | ✓ | |
| MJPEG + PiP（若启用） | ✓ | 视产品配置；已纳入清单验证 |
| `tests/integration/doc_deploy_walkthrough_smoke.sh board`（可选） | — | 未单独记录时可跳过 |

## 汇总

| **Tester** | kama |
| **Date** | 2026-04-17 |
| **Release / tag** | （待填） |

附：纯文本可另存为同目录 **`record_release_regression.txt`**。
