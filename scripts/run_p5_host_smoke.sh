#!/usr/bin/env bash
# P5 — 宿主机「发布门禁」子集：单元测试（P5-T4）+ 文档/脚本一致性检查 + P5-T2 说明。
# 交叉编译后的 ldd/readelf（P5-T3）与人工清单（P5-T1）不在此脚本内。
#
# 用法（在 my_uvc 仓库根）:
#   ./scripts/run_p5_host_smoke.sh
#   额外参数会传给 ctest（见 run_unit_tests_host.sh）。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

"${ROOT}/scripts/run_unit_tests_host.sh" "$@"

"${ROOT}/tests/integration/doc_deploy_walkthrough_smoke.sh" check
"${ROOT}/tests/integration/board_select_profile_uvc_binary_smoke.sh" check

echo ""
echo "P5-T2: 分文件目录 vs 单文件 my_uvc.ini — 已由 CTest **test_app_config_path_directory_vs_file** 覆盖（含于上方）。"
echo "      单独复跑: tests/integration/board_config_split_vs_monolith_parity.sh"
echo "P5-T3: 交叉编译产物目录下执行: tests/integration/check_uvctest_and_lib_deps.sh <build-dir>"
echo "      （另: tests/integration/check_libmy_uvc_soname_exports.sh <build-dir>）"
echo "P5-T1: 人工执行 docs/TEST_CHECKLIST_CN.md，结果记入 tests/integration/record_release_regression.md"
echo "板端集成（P1/P2 §闸口）脚本示例: tests/integration/board_libmy_uvc_submit_smoke.sh check, board_uvctest_parity_regression.sh check"
echo ""
echo "run_p5_host_smoke: ok"
