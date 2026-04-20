#!/usr/bin/env bash
# P3-I2: 多 channel_id 下 PiP 状态不串扰 — uvctest 为每路 channel_worker 单独 pip_helper_create（§9 ④）。
# 当前 AppConfig 为全局 pip_*；本脚本验证多路时日志中出现按 channel 区分的 pip 行。
#
# 用法:
#   ./board_pip_per_channel_isolation.sh check   # 检查 USB 路数提示 + 打印命令
#   ./board_pip_per_channel_isolation.sh smoke    # --channels 2，短时运行并 grep pip: ch=0 / ch=1
#
# 环境变量:
#   UVCTEST, CONFIG, MJPEG_SOURCE, PIP_OVERLAY, SIZE, CHANNELS（默认 2）, SMOKE_SEC（默认 8）
#
# 前置: my_uvc_usb_config.sh -n <CHANNELS> 已与配置一致。
set -euo pipefail

UVCTEST="${UVCTEST:-uvctest}"
CONFIG="${CONFIG:-/userdata}"
MJPEG_SOURCE="${MJPEG_SOURCE:-/userdata/mjpeg_frames_dir}"
PIP_OVERLAY="${PIP_OVERLAY:-/userdata/mjpeg_overlay}"
SIZE="${SIZE:-1920x1080}"
CHANNELS="${CHANNELS:-2}"
SMOKE_SEC="${SMOKE_SEC:-8}"

usage()
{
	cat <<EOF
Usage: $0 check|smoke
  check   Print multi-channel + PiP commands (needs UVC gadget with enough nodes).
  smoke   timeout-run uvctest with --channels N and grep 'pip: ch=' for each id.
EOF
}

need_cmd() { command -v "$1" >/dev/null 2>&1 || {
	echo "missing: $1"
	exit 1
}; }

run_check()
{
	need_cmd "${UVCTEST}"
	[[ -e "${CONFIG}" && -e "${MJPEG_SOURCE}" && -e "${PIP_OVERLAY}" ]] || {
		echo "set CONFIG, MJPEG_SOURCE, PIP_OVERLAY to existing paths"
		exit 1
	}
	cat <<EOF
=== P3-I2 prerequisites ===
  Ensure gadget exposes at least ${CHANNELS} UVC video nodes (see my_uvc_usb_config.sh -n ${CHANNELS}).

Example (PiP on, ${CHANNELS} channels — each channel gets its own pip_helper instance):
  ${UVCTEST} -c ${CONFIG} --codec mjpeg --channels ${CHANNELS} --file ${MJPEG_SOURCE} --size ${SIZE} \\
    --pip-enable 1 --pip-overlay ${PIP_OVERLAY} \\
    --pip-x 20 --pip-y 20 --pip-w 640 --pip-h 480 --pip-jpeg-quality 85

Expect in stderr/log for each active channel with valid video_id:
  pip: ch=0 ...
  pip: ch=1 ...
  (no cross-channel pip_helper reuse — one create per channel_id)
EOF
}

run_smoke()
{
	need_cmd "${UVCTEST}"
	command -v timeout >/dev/null 2>&1 || {
		echo "smoke needs 'timeout'"
		exit 1
	}
	[[ -e "${CONFIG}" && -e "${MJPEG_SOURCE}" && -e "${PIP_OVERLAY}" ]] || {
		echo "missing CONFIG/MJPEG_SOURCE/PIP_OVERLAY"
		exit 1
	}
	if [[ "${CHANNELS}" -lt 2 ]]; then
		echo "CHANNELS must be >= 2 for isolation smoke"
		exit 1
	fi

	local log
	log="$(mktemp)"
	trap 'rm -f "${log}"' EXIT

	set +e
	set -o pipefail
	timeout "${SMOKE_SEC}" "${UVCTEST}" -c "${CONFIG}" --codec mjpeg --channels "${CHANNELS}" \
		--file "${MJPEG_SOURCE}" --size "${SIZE}" \
		--pip-enable 1 --pip-overlay "${PIP_OVERLAY}" \
		--pip-x 20 --pip-y 20 --pip-w 640 --pip-h 480 --pip-jpeg-quality 85 2>&1 | tee "${log}"
	local ec=${PIPESTATUS[0]}
	set +o pipefail
	set -e
	if [[ "${ec}" -ne 124 ]] && [[ "${ec}" -ne 0 ]]; then
		echo "unexpected exit ${ec}"
		exit 1
	fi

	local i c_ok=0
	for ((i = 0; i < CHANNELS; i++)); do
		if grep -E "pip: ch=${i} " "${log}" >/dev/null; then
			c_ok=$((c_ok + 1))
		fi
	done
	if [[ "${c_ok}" -lt 2 ]]; then
		echo "FAIL: expected pip log lines for at least ch=0 and ch=1; got matches for ${c_ok} channel(s)."
		echo "Hint: verify video_id for each channel (no video_id → worker may skip) and overlay readable."
		exit 1
	fi

	echo "board_pip_per_channel_isolation smoke: ok (found per-channel pip: ch= lines)"
}

case "${1:-check}" in
	check) run_check ;;
	smoke) run_smoke ;;
	-h|--help|help) usage; exit 0 ;;
	*) usage; exit 1 ;;
esac
