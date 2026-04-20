#!/usr/bin/env bash
# P3-I1: PiP 开 / 关 — 主机预览对比；关时应与未走 PiP 合成路径一致（无 overlay）。
# 板端执行；详见 docs/TEST_CHECKLIST_CN.md §4 / §4.0.1。
#
# 用法:
#   ./board_pip_on_off_compare.sh check    # 仅检查依赖并打印推荐命令（默认）
#   ./board_pip_on_off_compare.sh smoke    # 短时运行 uvctest，校验日志中与 pip 相关的行
#
# 环境变量（可选）:
#   UVCTEST          默认可执行文件: uvctest（或 my_uvc）
#   CONFIG           -c 配置: /userdata 或单文件
#   MJPEG_SOURCE     --file: 目录或文件（MJPEG 源）
#   PIP_OVERLAY      PiP 开时 --pip-overlay（JPEG 文件或含多帧的目录）
#   SIZE             默认 1920x1080
#   SMOKE_SEC        smoke 模式每次运行的秒数（默认 8）
set -euo pipefail

UVCTEST="${UVCTEST:-uvctest}"
CONFIG="${CONFIG:-/userdata}"
MJPEG_SOURCE="${MJPEG_SOURCE:-/userdata/mjpeg_frames_dir}"
PIP_OVERLAY="${PIP_OVERLAY:-/userdata/mjpeg_overlay}"
SIZE="${SIZE:-1920x1080}"
SMOKE_SEC="${SMOKE_SEC:-8}"

usage()
{
	cat <<EOF
Usage: $0 check|smoke
  check  Verify uvctest + paths; print host ffplay steps (see TEST_CHECKLIST_CN.md).
  smoke  Run uvctest twice (pip off / pip on) under timeout and grep log markers.
EOF
}

need_cmd()
{
	command -v "$1" >/dev/null 2>&1 || {
		echo "missing command: $1"
		exit 1
	}
}

run_check()
{
	need_cmd "${UVCTEST}"
	if [[ ! -e "${CONFIG}" ]]; then
		echo "CONFIG not found: ${CONFIG}"
		exit 1
	fi
	if [[ ! -e "${MJPEG_SOURCE}" ]]; then
		echo "MJPEG_SOURCE not found: ${MJPEG_SOURCE}"
		exit 1
	fi
	if [[ ! -e "${PIP_OVERLAY}" ]]; then
		echo "PIP_OVERLAY not found: ${PIP_OVERLAY}"
		exit 1
	fi

	cat <<EOF
=== P3-I1 prerequisites: ok ===
  UVCTEST=${UVCTEST}
  CONFIG=${CONFIG}
  MJPEG_SOURCE=${MJPEG_SOURCE}
  PIP_OVERLAY=${PIP_OVERLAY}

--- A) PiP 关（应与无 PiP 直送 MJPEG 一致）---
  ${UVCTEST} -c ${CONFIG} --codec mjpeg --file ${MJPEG_SOURCE} --size ${SIZE} --pip-enable 0

--- B) PiP 开（应出现主讲人/overlay 小窗）---
  ${UVCTEST} -c ${CONFIG} --codec mjpeg --file ${MJPEG_SOURCE} --size ${SIZE} \\
    --pip-enable 1 --pip-overlay ${PIP_OVERLAY} \\
    --pip-x 20 --pip-y 20 --pip-w 640 --pip-h 480 --pip-jpeg-quality 85

主机预览（示例，设备节点按实机调整）:
  ffplay -f v4l2 -input_format mjpeg -video_size ${SIZE} -framerate 25 /dev/video0

人工对比: B 相对 A 应多出 overlay；A 不应出现 PiP 合成特有画面差异（与「仅关 pip」预期一致）。
EOF
}

run_smoke()
{
	need_cmd "${UVCTEST}"
	if ! command -v timeout >/dev/null 2>&1; then
		echo "smoke mode needs GNU coreutils 'timeout'"
		exit 1
	fi
	[[ -e "${CONFIG}" && -e "${MJPEG_SOURCE}" && -e "${PIP_OVERLAY}" ]] || {
		echo "smoke: set CONFIG, MJPEG_SOURCE, PIP_OVERLAY to existing paths"
		exit 1
	}

	local log_off log_on
	log_off="$(mktemp)"
	log_on="$(mktemp)"
	trap 'rm -f "${log_off}" "${log_on}"' EXIT

	# PiP 关: 不应出现 "config: pip=1"
	set +e
	set -o pipefail
	timeout "${SMOKE_SEC}" "${UVCTEST}" -c "${CONFIG}" --codec mjpeg --file "${MJPEG_SOURCE}" --size "${SIZE}" \
		--pip-enable 0 2>&1 | tee "${log_off}"
	local ec_off=${PIPESTATUS[0]}
	set +o pipefail
	set -e
	if [[ "${ec_off}" -ne 124 ]] && [[ "${ec_off}" -ne 0 ]]; then
		echo "pip-off run: unexpected exit ${ec_off} (124=timeout is ok)"
		exit 1
	fi
	if grep -q "config: pip=1" "${log_off}"; then
		echo "FAIL: pip-off log should not contain 'config: pip=1'"
		exit 1
	fi

	# PiP 开: 应出现配置行；若两路以上还可检查各 ch 的 pip 日志
	set +e
	set -o pipefail
	timeout "${SMOKE_SEC}" "${UVCTEST}" -c "${CONFIG}" --codec mjpeg --file "${MJPEG_SOURCE}" --size "${SIZE}" \
		--pip-enable 1 --pip-overlay "${PIP_OVERLAY}" \
		--pip-x 20 --pip-y 20 --pip-w 640 --pip-h 480 --pip-jpeg-quality 85 2>&1 | tee "${log_on}"
	local ec_on=${PIPESTATUS[0]}
	set +o pipefail
	set -e
	if [[ "${ec_on}" -ne 124 ]] && [[ "${ec_on}" -ne 0 ]]; then
		echo "pip-on run: unexpected exit ${ec_on}"
		exit 1
	fi
	if ! grep -q "config: pip=1" "${log_on}"; then
		echo "FAIL: pip-on log should contain 'config: pip=1'"
		exit 1
	fi

	echo "board_pip_on_off_compare smoke: ok (config markers; full picture needs host ffplay — see check)"
}

mode="${1:-check}"
case "${mode}" in
	check) run_check ;;
	smoke) run_smoke ;;
	-h|--help|help) usage; exit 0 ;;
	*)
		usage
		exit 1
		;;
esac
