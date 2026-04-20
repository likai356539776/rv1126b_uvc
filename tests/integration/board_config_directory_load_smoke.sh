#!/usr/bin/env bash
# P0-T2: 目录多文件 ini 合并（libmy_uvc.ini → libmy_uvc_pip.ini → uvctest.ini）与区段约束。
# 逻辑由宿主机 CTest **test_app_config_ini_merge** 覆盖；本脚本在仓库根配置并运行该用例。
#
# 用法（在 my_uvc 仓库根）:
#   ./tests/integration/board_config_directory_load_smoke.sh
#
# 板端快速检查（可选，不跑 CTest）:
#   ./tests/integration/board_config_directory_load_smoke.sh board
#
# 可选:
#   BUILD_DIR=/path/to/build-host-unit ./tests/integration/board_config_directory_load_smoke.sh
set -euo pipefail

if [[ "${1:-}" == "board" ]]; then
	ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
	# 与文档默认部署路径一致：分文件位于 /userdata
	for need in /userdata/libmy_uvc.ini /userdata/libmy_uvc_pip.ini /userdata/uvctest.ini; do
		if [[ ! -f "${need}" ]]; then
			echo "board_config_directory_load_smoke: missing ${need}"
			exit 1
		fi
	done
	if [[ -f /userdata/my_uvc.ini ]]; then
		echo "board_config_directory_load_smoke: ok (split + legacy my_uvc.ini present under /userdata)"
	else
		echo "board_config_directory_load_smoke: ok (split configs under /userdata; optional my_uvc.ini not found)"
	fi
	exit 0
fi

MY_UVC_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${MY_UVC_ROOT}/build-host-unit}"

cmake -S "${MY_UVC_ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DMY_UVC_ENABLE_UNIT_TESTS=ON
cmake --build "${BUILD_DIR}" -j"$(nproc)" --target test_app_config_ini_merge
ctest --test-dir "${BUILD_DIR}" --output-on-failure -R test_app_config_ini_merge

echo "board_config_directory_load_smoke: ok (P0-T2 / test_app_config_ini_merge)"
