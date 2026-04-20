#!/usr/bin/env bash
# P2-I3: 多路 UVC 下各 channel 映射 video_id；拔插或重枚举后可出现 remap（见日志 channel N remap video_id ...）。
# 与 docs/TEST_CHECKLIST_CN.md §4.2 / §4.2.1 对齐。
#
# 用法:
#   ./board_usb_multichannel_remap.sh check              # 打印命令与前置
#   ./board_usb_multichannel_remap.sh smoke             # CHANNELS=2（默认）短时跑 uvctest，检查多路 mapped 日志
#
# 环境: UVCTEST, CONFIG, MJPEG_SOURCE, SIZE, SMOKE_SEC, CHANNELS
set -euo pipefail

UVCTEST="${UVCTEST:-uvctest}"
CONFIG="${CONFIG:-/userdata}"
MJPEG_SOURCE="${MJPEG_SOURCE:-/userdata/mjpeg_frames_dir}"
SIZE="${SIZE:-1920x1080}"
SMOKE_SEC="${SMOKE_SEC:-10}"
CHANNELS="${CHANNELS:-2}"

run_check()
{
	if command -v "${UVCTEST}" >/dev/null 2>&1; then
		:
	else
		echo "WARN: ${UVCTEST} not in PATH — commands below are for target board (or set UVCTEST)."
	fi
	[[ -e "${CONFIG}" ]] || echo "WARN: CONFIG not found: ${CONFIG}"

	cat <<EOF
=== P2-I3 multichannel video_id / remap ===

1) USB gadget 路数与配置一致，例如 2 路:
   my_uvc_usb_config.sh -w 1920 -h 1080 -p 25 -n ${CHANNELS} --verbose --stop-system-usb

2) 启动应用:
   ${UVCTEST} --channels ${CHANNELS} -c ${CONFIG}
   # MJPEG 有目录源时:
   ${UVCTEST} --channels ${CHANNELS} -c ${CONFIG} --codec mjpeg --file ${MJPEG_SOURCE} --size ${SIZE}

3) 期望日志中出现每路:
   channel <ch> mapped video_id=<n>
   以及周期 ch=<ch> video_id=<n> sent=...
   USB 重枚举后可能出现: channel <ch> remap video_id <a> -> <b>

主机: v4l2-ctl --list-devices 分别打开多个 /dev/videoX

board_usb_multichannel_remap check: ok
EOF
}

run_smoke()
{
	[[ "${CHANNELS}" =~ ^[0-9]+$ ]] && [[ "${CHANNELS}" -ge 2 ]] || {
		echo "CHANNELS must be >= 2"
		exit 1
	}
	command -v "${UVCTEST}" >/dev/null 2>&1 || {
		echo "missing ${UVCTEST}"
		exit 1
	}
	command -v timeout >/dev/null 2>&1 || {
		echo "need GNU timeout"
		exit 1
	}
	[[ -e "${CONFIG}" ]] || {
		echo "CONFIG missing"
		exit 1
	}
	[[ -e "${MJPEG_SOURCE}" ]] || {
		echo "smoke needs MJPEG_SOURCE (multichannel MJPEG push): ${MJPEG_SOURCE}"
		exit 1
	}

	local log
	log="$(mktemp)"
	trap 'rm -f "${log}"' EXIT

	set +e
	set -o pipefail
	timeout "${SMOKE_SEC}" "${UVCTEST}" --channels "${CHANNELS}" -c "${CONFIG}" --codec mjpeg \
		--file "${MJPEG_SOURCE}" --size "${SIZE}" 2>&1 | tee "${log}"
	local ec=${PIPESTATUS[0]}
	set +o pipefail
	set -e

	if [[ "${ec}" -ne 124 ]] && [[ "${ec}" -ne 0 ]]; then
		echo "smoke: exit ${ec}"
		exit 1
	fi

	local i found=0
	for ((i = 0; i < CHANNELS; i++)); do
		if grep -q "channel ${i} mapped video_id=" "${log}"; then
			found=$((found + 1))
		fi
	done
	if [[ "${found}" -lt "${CHANNELS}" ]]; then
		echo "FAIL: expected channel 0..$((CHANNELS - 1)) mapped video_id= in log"
		grep -E 'channel [0-9]+ mapped|ERROR' "${log}" | tail -40 || true
		exit 1
	fi

	echo "board_usb_multichannel_remap smoke: ok (${CHANNELS} channels mapped; replug/remap — manual §4.4)"
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
