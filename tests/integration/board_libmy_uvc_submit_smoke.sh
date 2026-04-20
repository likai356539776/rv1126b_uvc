#!/usr/bin/env bash
# P1-I1: 板端验证 libmy_uvc 送帧路径 — uvctest 经 my_uvc_submit_* 推流；与 docs/TEST_CHECKLIST_CN.md §3–§4 一致。
# 完整链路需 UVC gadget + 媒体文件；本脚本 check 列前置条件，smoke 短时运行并抓取典型日志（sent / open uvc）。
#
# 用法:
#   ./board_libmy_uvc_submit_smoke.sh check   # 默认
#   ./board_libmy_uvc_submit_smoke.sh smoke
#
# 环境变量:
#   UVCTEST, CONFIG, MJPEG_SOURCE, SIZE, SMOKE_SEC
set -euo pipefail

UVCTEST="${UVCTEST:-uvctest}"
CONFIG="${CONFIG:-/userdata}"
MJPEG_SOURCE="${MJPEG_SOURCE:-/userdata/mjpeg_frames_dir}"
SIZE="${SIZE:-1920x1080}"
SMOKE_SEC="${SMOKE_SEC:-10}"

usage()
{
	cat <<EOF
Usage: $0 check|smoke
  P1-I1 — libmy_uvc submit smoke (via uvctest). See TEST_CHECKLIST_CN.md §3 (USB) + §4 (stream).
EOF
}

need_cmd() { command -v "$1" >/dev/null 2>&1 || { echo "missing: $1"; exit 1; }; }

run_check()
{
	if ! command -v "${UVCTEST}" >/dev/null 2>&1 && [[ ! -x "${UVCTEST}" ]]; then
		echo "WARN: ${UVCTEST} not found (export UVCTEST=/usr/bin/uvctest on device)"
	fi
	[[ -e "${CONFIG}" ]] || echo "WARN: CONFIG not found: ${CONFIG}"
	[[ -e "${MJPEG_SOURCE}" ]] || echo "WARN: MJPEG_SOURCE missing (smoke MJPEG path will fail): ${MJPEG_SOURCE}"

	cat <<EOF
=== P1-I1 (libmy_uvc submit) — prerequisites ===
  UVCTEST=${UVCTEST}  CONFIG=${CONFIG}  MJPEG_SOURCE=${MJPEG_SOURCE}

1) 板端先配置 USB gadget（示例）:
   my_uvc_usb_config.sh -f MJPEG -w 1920 -h 1080 -p 25 -n 1 --verbose --stop-system-usb

2) MJPEG 推流（走 my_uvc_submit_mjpeg）:
   ${UVCTEST} -c ${CONFIG} --codec mjpeg --file ${MJPEG_SOURCE} --size ${SIZE}

3) H.264（走 my_uvc_submit_h264，路径来自 ini / CLI）:
   ${UVCTEST} -c ${CONFIG} --codec h264 --size ${SIZE}

主机预览（示例）: ffplay -f v4l2 -input_format mjpeg -video_size ${SIZE} -framerate 25 /dev/video0

board_libmy_uvc_submit_smoke check: ok
EOF
}

run_smoke()
{
	need_cmd "${UVCTEST}"
	command -v timeout >/dev/null 2>&1 || {
		echo "smoke needs GNU timeout"
		exit 1
	}
	[[ -e "${CONFIG}" && -e "${MJPEG_SOURCE}" ]] || {
		echo "smoke: need CONFIG and MJPEG_SOURCE (set env or create paths)"
		exit 1
	}

	local log
	log="$(mktemp)"
	trap 'rm -f "${log}"' EXIT

	set +e
	set -o pipefail
	timeout "${SMOKE_SEC}" "${UVCTEST}" -c "${CONFIG}" --codec mjpeg --file "${MJPEG_SOURCE}" --size ${SIZE} \
		2>&1 | tee "${log}"
	local ec=${PIPESTATUS[0]}
	set +o pipefail
	set -e

	if [[ "${ec}" -ne 124 ]] && [[ "${ec}" -ne 0 ]]; then
		echo "smoke: uvctest exit ${ec} (124=timeout ok)"
		exit 1
	fi

	if ! grep -qE 'open uvc|ch=[0-9]+ video_id=[0-9]+ sent=' "${log}"; then
		echo "FAIL: expected log lines 'open uvc' and/or 'ch=... video_id=... sent=' (submit path)"
		echo "--- tail ---"
		tail -40 "${log}"
		exit 1
	fi

	echo "board_libmy_uvc_submit_smoke smoke: ok (MJPEG submit markers; host preview manual — see check)"
}

case "${1:-check}" in
check) run_check ;;
smoke) run_smoke ;;
-h | --help) usage; exit 0 ;;
*)
	usage
	exit 1
	;;
esac
