#!/usr/bin/env bash
# P4-T1: ini 分段加载与全文件加载一致性 — 由 test_my_uvc_ini_section_loader（宿主机 CTest）验证。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build-host-unit}"

cmake -S "${ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DMY_UVC_ENABLE_UNIT_TESTS=ON
cmake --build "${BUILD_DIR}" -j"$(nproc)" --target test_my_uvc_ini_section_loader
ctest --test-dir "${BUILD_DIR}" --output-on-failure -R test_my_uvc_ini_section_loader

echo "board_ini_loader_parity_smoke: ok"
