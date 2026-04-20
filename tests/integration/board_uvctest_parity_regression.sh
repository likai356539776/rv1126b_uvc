#!/usr/bin/env bash
# P2-I1: uvctest 与「同 ini + CLI」下的行为回归矩阵（单路/多路、H.264、MJPEG）。
# 配置级与目录合并级parity已由宿主机 CTest（test_app_config_path_directory_vs_file / test_app_config_ini_merge）覆盖。
# 与历史「重构前二进制」逐字节对比需自备基线：可选环境变量 UVCTEST_BASELINE。
#
# 用法:
#   ./board_uvctest_parity_regression.sh check     # 打印矩阵与 CTest 指针
#   ./board_uvctest_parity_regression.sh smoke       # 短时跑 H.264 + MJPEG（路径存在时）
#
# 环境: UVCTEST, CONFIG, MJPEG_SOURCE, SIZE, SMOKE_SEC, CHANNELS
set -euo pipefail

UVCTEST="${UVCTEST:-uvctest}"
CONFIG="${CONFIG:-/userdata}"
MJPEG_SOURCE="${MJPEG_SOURCE:-/userdata/mjpeg_frames_dir}"
SIZE="${SIZE:-1920x1080}"
SMOKE_SEC="${SMOKE_SEC:-8}"
CHANNELS="${CHANNELS:-1}"

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

run_check()
{
	cat <<EOF
=== P2-I1 uvctest parity / regression ===

宿主机（配置与合并）:
  ${ROOT}/scripts/run_unit_tests_host.sh  # 含 test_app_config_path_directory_vs_file, test_app_config_ini_merge

板端矩阵（人工或 smoke 子集）:
  - 单路 H.264:  ${UVCTEST} -c ${CONFIG} --codec h264 --size ${SIZE}
  - 单路 MJPEG: ${UVCTEST} -c ${CONFIG} --codec mjpeg --file ${MJPEG_SOURCE} --size ${SIZE}
  - 多路:        ${UVCTEST} -c ${CONFIG} --channels <N> ...（先 my_uvc_usb_config.sh -n N）

可选基线对比（若仍保留旧可执行文件）:
  export UVCTEST_BASELINE=/path/to/legacy_bin
  # 对同一 CONFIG 与相同参数分别运行相同时长的 timeout，对比日志与主机预览（无自动 diff）。

board_uvctest_parity_regression check: ok
EOF
	if [[ -n "${UVCTEST_BASELINE:-}" ]]; then
		echo "UVCTEST_BASELINE=${UVCTEST_BASELINE} — compare manually with ${UVCTEST}"
	fi
}

run_smoke()
{
	command -v "${UVCTEST}" >/dev/null 2>&1 || {
		echo "missing ${UVCTEST}"
		exit 1
	}
	command -v timeout >/dev/null 2>&1 || {
		echo "need GNU timeout"
		exit 1
	}
	[[ -e "${CONFIG}" ]] || {
		echo "CONFIG missing: ${CONFIG}"
		exit 1
	}

	local log ec
	log="$(mktemp)"
	trap 'rm -f "${log}"' EXIT

	# H.264
	set +e
	set -o pipefail
	timeout "${SMOKE_SEC}" "${UVCTEST}" -c "${CONFIG}" --codec h264 --size "${SIZE}" --channels "${CHANNELS}" 2>&1 | tee "${log}"
	ec=${PIPESTATUS[0]}
	set +o pipefail
	set -e
	if [[ "${ec}" -ne 124 ]] && [[ "${ec}" -ne 0 ]]; then
		echo "smoke H264: unexpected exit ${ec}"
		exit 1
	fi
	if ! grep -qE 'open uvc|channel [0-9]+ mapped video_id' "${log}"; then
		echo "FAIL: H264 smoke — missing expected uvc / video_id log"
		tail -30 "${log}"
		exit 1
	fi

	if [[ ! -e "${MJPEG_SOURCE}" ]]; then
		echo "skip MJPEG smoke (MJPEG_SOURCE missing): ${MJPEG_SOURCE}"
		echo "board_uvctest_parity_regression smoke: ok (H264 only)"
		exit 0
	fi

	log="$(mktemp)"
	set +e
	set -o pipefail
	timeout "${SMOKE_SEC}" "${UVCTEST}" -c "${CONFIG}" --codec mjpeg --file "${MJPEG_SOURCE}" --size "${SIZE}" --channels "${CHANNELS}" 2>&1 | tee "${log}"
	ec=${PIPESTATUS[0]}
	set +o pipefail
	set -e
	if [[ "${ec}" -ne 124 ]] && [[ "${ec}" -ne 0 ]]; then
		echo "smoke MJPEG: unexpected exit ${ec}"
		exit 1
	fi
	if ! grep -qE 'open uvc|sent=' "${log}"; then
		echo "FAIL: MJPEG smoke — missing expected log"
		tail -30 "${log}"
		exit 1
	fi

	echo "board_uvctest_parity_regression smoke: ok (H264 + MJPEG short runs)"
}

case "${1:-check}" in
check) run_check ;;
smoke) run_smoke ;;
-h | --help)
	echo "Usage: $0 [check|smoke]"
	exit 0
	;;
*)
	echo "Usage: $0 [check|smoke]"
	exit 1
	;;
esac
