#!/usr/bin/env bash
# P5-T4: Host (non-cross) configure + CTest — unit tests only; no Rockchip sysroot required.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build-host-unit"
cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release -DMY_UVC_ENABLE_UNIT_TESTS=ON
# Only build unit-test binaries (avoid linking full libmy_uvc.so without Rockchip sysroot).
cmake --build "${BUILD}" -j"$(nproc)" \
	--target test_my_uvc_config_mapping test_my_uvc_submit_errors \
	test_uvctest_cli_overrides_ini test_app_config_path_directory_vs_file test_app_config_ini_merge \
	test_pip_tile_layout_rects test_pip_helper_config_defaults \
	test_pip_stale_timeout_policy test_pip_ini_stale_timeout_parse test_pip_tile_ini_parse \
	test_uvctest_pip_tile_nv12_paths_ini \
	test_my_uvc_ini_section_loader test_my_uvc_load_ini_section_c_api test_my_uvc_channel_video_stub test_pip_compose_offline_golden
ctest --test-dir "${BUILD}" --output-on-failure "$@"
