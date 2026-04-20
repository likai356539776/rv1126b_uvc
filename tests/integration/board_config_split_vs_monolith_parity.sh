#!/usr/bin/env bash
# P5-T2: 分文件配置目录（libmy_uvc.ini + libmy_uvc_pip.ini + uvctest.ini）与单文件合并 ini
# 在 load_app_config 下得到相同 AppConfig — 逻辑由 **P2-U2** 单测覆盖；本脚本在宿主机跑该 CTest。
# 与 **`scripts/run_p5_host_smoke.sh`** 中的 CTest **test_app_config_path_directory_vs_file** 等价，可单独复跑。
#
# 用法（在 my_uvc 仓库根执行）:
#   ./tests/integration/board_config_split_vs_monolith_parity.sh
#
# 可选:
#   BUILD_DIR=/path/to/build-host-unit ./tests/integration/board_config_split_vs_monolith_parity.sh
set -euo pipefail

MY_UVC_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${MY_UVC_ROOT}/build-host-unit}"

cmake -S "${MY_UVC_ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DMY_UVC_ENABLE_UNIT_TESTS=ON
cmake --build "${BUILD_DIR}" -j"$(nproc)" --target test_app_config_path_directory_vs_file
ctest --test-dir "${BUILD_DIR}" --output-on-failure -R test_app_config_path_directory_vs_file

echo "board_config_split_vs_monolith_parity: ok (P2-U2 / test_app_config_path_directory_vs_file)"
