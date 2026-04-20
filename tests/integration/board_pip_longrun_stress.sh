#!/usr/bin/env bash
# P3-I3: PiP 长稳 — 泄漏 / 死锁粗测。可与 P5 全量回归合并执行。
#
# 用法:
#   ./board_pip_longrun_stress.sh              # 默认跑 PIP_STRESS_SEC（默认 120）秒后正常结束
#   PIP_STRESS_SEC=600 ./board_pip_longrun_stress.sh
#
# 说明:
#   使用 timeout 在固定时长内向 uvctest 发送 SIGTERM；退出码 124 表示跑满时长未崩溃，视为通过。
#   更严格检查请在并行终端观察: top / pidstat -r -p <pid>，或 P5 记录模板。
#
# 环境变量:
#   UVCTEST, CONFIG, MJPEG_SOURCE, PIP_OVERLAY, SIZE, PIP_STRESS_SEC（默认 120）
set -euo pipefail

UVCTEST="${UVCTEST:-uvctest}"
CONFIG="${CONFIG:-/userdata}"
MJPEG_SOURCE="${MJPEG_SOURCE:-/userdata/mjpeg_frames_dir}"
PIP_OVERLAY="${PIP_OVERLAY:-/userdata/mjpeg_overlay}"
SIZE="${SIZE:-1920x1080}"
PIP_STRESS_SEC="${PIP_STRESS_SEC:-120}"

usage()
{
	cat <<EOF
Usage: $0
  Long-run PiP path under timeout (default ${PIP_STRESS_SEC}s). Exit 0 if timeout ends process cleanly.

Environment:
  PIP_STRESS_SEC   duration in seconds (default 120)
  UVCTEST CONFIG MJPEG_SOURCE PIP_OVERLAY SIZE — same as other board_pip_*.sh
EOF
}

for a in "$@"; do
	if [[ "${a}" == "-h" || "${a}" == "--help" ]]; then
		usage
		exit 0
	fi
done

command -v "${UVCTEST}" >/dev/null 2>&1 || {
	echo "missing ${UVCTEST}"
	exit 1
}
command -v timeout >/dev/null 2>&1 || {
	echo "needs GNU timeout"
	exit 1
}
[[ -e "${CONFIG}" && -e "${MJPEG_SOURCE}" && -e "${PIP_OVERLAY}" ]] || {
	echo "set CONFIG, MJPEG_SOURCE, PIP_OVERLAY to existing paths on device"
	exit 1
}

echo "=== P3-I3 longrun: ${PIP_STRESS_SEC}s PiP MJPEG (timeout => exit 124 = ok) ==="

set +e
timeout "${PIP_STRESS_SEC}" "${UVCTEST}" -c "${CONFIG}" --codec mjpeg --file "${MJPEG_SOURCE}" --size "${SIZE}" \
	--pip-enable 1 --pip-overlay "${PIP_OVERLAY}" \
	--pip-x 20 --pip-y 20 --pip-w 640 --pip-h 480 --pip-jpeg-quality 85
ec=$?
set -e

if [[ "${ec}" -eq 124 ]]; then
	echo "board_pip_longrun_stress: ok (completed ${PIP_STRESS_SEC}s, timeout)"
	exit 0
fi

echo "board_pip_longrun_stress: uvctest exited early with code ${ec} (expected 124 for full stress duration)"
exit 1
