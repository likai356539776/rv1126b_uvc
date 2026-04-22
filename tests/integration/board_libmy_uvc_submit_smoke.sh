#!/usr/bin/env bash
# P1-I1: 板端验证 libmy_uvc 送帧路径 — uvctest 经 my_uvc_submit_* 推流；与 docs/TEST_CHECKLIST_CN.md §3–§4 一致。
# 完整链路需 UVC gadget + 媒体文件；本脚本 check 列前置条件，smoke 短时运行并抓取典型日志（sent / open uvc）。
#
# 用法:
#   ./board_libmy_uvc_submit_smoke.sh check   # 默认
#   ./board_libmy_uvc_submit_smoke.sh smoke
#
# 环境变量:
#   UVCTEST, CONFIG, MJPEG_SOURCE, SMOKE_SIZE (默认 1920x1080; 勿用 SIZE — 易与终端 SIZE 冲突),
#   SMOKE_SEC,
#   SMOKE_DISABLE_PIP (默认 1): smoke 时对 uvctest 追加 --pip-enable 0，避免 ini 打开 PiP 但 overlay 缺失导致 worker
#   提前退出、无 sent=/open uvc。要按 ini 含 PiP 测 smoke 时设 SMOKE_DISABLE_PIP=0 并保证 overlay 可读。
#
# 本脚本需在板端执行（ssh / adb shell）；宿主机上通常没有 uvctest，会报找不到。
# 若安装到 /usr/bin 而非在 PATH 中，可不设 UVCTEST：会自动尝试 PATH 再尝试 /usr/bin/uvctest。
set -euo pipefail

# 仅当调用方未设置 UVCTEST 时自动解析（PATH → 常见安装路径 /usr/bin）。
if [[ -z "${UVCTEST+x}" ]]; then
	if command -v uvctest >/dev/null 2>&1; then
		UVCTEST=uvctest
	elif [[ -x /usr/bin/uvctest ]]; then
		UVCTEST=/usr/bin/uvctest
	else
		UVCTEST=uvctest
	fi
fi
CONFIG="${CONFIG:-/userdata}"
MJPEG_SOURCE="${MJPEG_SOURCE:-/userdata/mjpeg_frames_dir}"
# 不用名 SIZE：常见 login/getty 会设 SIZE=行数（例如 19），会覆盖默认并搞乱 --size / 分辨率。
SMOKE_SIZE="${SMOKE_SIZE:-1920x1080}"
SMOKE_SEC="${SMOKE_SEC:-10}"
SMOKE_DISABLE_PIP="${SMOKE_DISABLE_PIP:-1}"

# smoke 临时日志路径须为全局：EXIT trap 在 run_smoke 返回后执行，local 会触发 set -u 下 “log: unbound variable”。
_BSS_TMPLOG=""
_bss_tmp_cleanup() {
	[[ -n "${_BSS_TMPLOG:-}" ]] && rm -f -- "${_BSS_TMPLOG}"
}

usage()
{
	cat <<EOF
Usage: $0 check|smoke
  P1-I1 — libmy_uvc submit smoke (via uvctest). See TEST_CHECKLIST_CN.md §3 (USB) + §4 (stream).
EOF
}

need_cmd()
{
	if command -v "$1" >/dev/null 2>&1 || [[ -x "$1" ]]; then
		return 0
	fi
	echo "missing: $1 (install on device or export UVCTEST=/path/to/uvctest; run this script on the board)"
	exit 1
}

run_check()
{
	if ! command -v "${UVCTEST}" >/dev/null 2>&1 && [[ ! -x "${UVCTEST}" ]]; then
		echo "WARN: ${UVCTEST} not found (install to PATH or /usr/bin; or export UVCTEST=/path/to/uvctest — run check/smoke on the board)"
	fi
	[[ -e "${CONFIG}" ]] || echo "WARN: CONFIG not found: ${CONFIG}"
	[[ -e "${MJPEG_SOURCE}" ]] || echo "WARN: MJPEG_SOURCE missing (smoke MJPEG path will fail): ${MJPEG_SOURCE}"

	cat <<EOF
=== P1-I1 (libmy_uvc submit) — prerequisites ===
  UVCTEST=${UVCTEST}  CONFIG=${CONFIG}  MJPEG_SOURCE=${MJPEG_SOURCE}

1) 板端先配置 USB gadget（示例）:
   my_uvc_usb_config.sh -f MJPEG -w 1920 -h 1080 -p 25 -n 1 --verbose --stop-system-usb
   未配置时 uvctest 会在 my_uvc_start 内长时间无输出（看似卡住）；smoke 用 timeout 兜底。

2) MJPEG 推流（走 my_uvc_submit_mjpeg）:
   # smoke 默认会加 --pip-enable 0（见 SMOKE_DISABLE_PIP），本行仅为示例命令：
   ${UVCTEST} -c "${CONFIG}" --codec mjpeg --file "${MJPEG_SOURCE}" --size "${SMOKE_SIZE}"

3) H.264（走 my_uvc_submit_h264，路径来自 ini / CLI）:
   ${UVCTEST} -c "${CONFIG}" --codec h264 --size "${SMOKE_SIZE}"

主机预览（示例，在 PC 上执行）:
   ffplay -f v4l2 -input_format mjpeg -video_size "${SMOKE_SIZE}" -framerate 25 /dev/video0

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

	# my_uvc_start → uvc_control_run 会在 UVC gadget 节点就绪前阻塞（仅打印首行 config 后无输出属正常）。
	# 须先按 check 中步骤配置 gadget（如 my_uvc_usb_config.sh）；否则请等待 ${SMOKE_SEC}s 由 timeout 结束。
	echo "smoke: timeout ${SMOKE_SEC}s; if no UVC gadget yet, uvctest may print only the first config line until my_uvc_start unblocks."

	_BSS_TMPLOG="$(mktemp)"
	trap '_bss_tmp_cleanup' EXIT

	set +e
	set -o pipefail
	local -a smoke_args=(
		-c "${CONFIG}" --codec mjpeg --file "${MJPEG_SOURCE}" --size "${SMOKE_SIZE}"
	)
	if [[ "${SMOKE_DISABLE_PIP}" != "0" ]]; then
		smoke_args+=(--pip-enable 0)
	fi
	timeout "${SMOKE_SEC}" "${UVCTEST}" "${smoke_args[@]}" \
		2>&1 | tee "${_BSS_TMPLOG}"
	local ec=${PIPESTATUS[0]}
	set +o pipefail
	set -e

	if [[ "${ec}" -ne 124 ]] && [[ "${ec}" -ne 0 ]]; then
		echo "smoke: uvctest exit ${ec} (124=timeout ok)"
		exit 1
	fi

	# open uvc = uvctest 回调（主机开流）；uvc open succeeded = gadget 栈；sent= = channel_worker 送帧（需 PiP 成功或已禁用 PiP，且主机 STREAMON）
	if ! grep -qE 'open uvc|uvc open succeeded|ch=[0-9]+ video_id=[0-9]+ sent=' "${_BSS_TMPLOG}"; then
		echo "FAIL: expected 'open uvc' and/or 'uvc open succeeded' and/or ch=... sent= (submit path)"
		echo "--- tail ---"
		tail -40 "${_BSS_TMPLOG}"
		exit 1
	fi

	trap - EXIT
	rm -f -- "${_BSS_TMPLOG}"
	_BSS_TMPLOG=""
	echo "board_libmy_uvc_submit_smoke smoke: ok"
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
