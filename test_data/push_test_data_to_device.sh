#!/bin/bash
set -euo pipefail

# -----------------------------------------------------------------------------
# test_data 一键推送到目标板
# 用途:
#   将本目录（含子目录）下的所有文件通过 adb push 同步到设备上的 /userdata/
#   相对路径保持不变（例如 mjpeg_frames_dir_1080p/foo.jpg ->
#   /userdata/mjpeg_frames_dir_1080p/foo.jpg）。
#
# 约定:
#   - 本脚本自身不会被推送（避免把部署脚本也拷到板上）。
#   - 远端根目录默认为 /userdata，可用 --remote-base 覆盖。
# -----------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_NAME="$(basename "${BASH_SOURCE[0]}")"

# Optional target device serial.
# Priority:
#   1) --adb-serial <serial> argument
#   2) ADB_SERIAL environment variable
# If neither is provided, adb runs without -s (default behavior).
ADB_SERIAL_ENV="${ADB_SERIAL:-}"
ADB_SERIAL_ARG=""
REMOTE_BASE="/userdata"

while [[ $# -gt 0 ]]; do
	case "$1" in
	--adb-serial)
		[[ $# -ge 2 ]] || { echo "Missing value for --adb-serial"; exit 1; }
		ADB_SERIAL_ARG="$2"
		shift 2
		;;
	--remote-base)
		[[ $# -ge 2 ]] || { echo "Missing value for --remote-base"; exit 1; }
		REMOTE_BASE="${2%/}"
		shift 2
		;;
	-h|--help)
		echo "Usage: $0 [--adb-serial <serial>] [--remote-base <path>]"
		echo "  --remote-base  Remote directory root (default: /userdata)"
		echo "  Pushes all files under: ${SCRIPT_DIR}"
		echo "  Excludes this script: ${SCRIPT_NAME}"
		exit 0
		;;
	*)
		echo "Unknown option: $1"
		echo "Usage: $0 [--adb-serial <serial>] [--remote-base <path>]"
		exit 1
		;;
	esac
done

ADB_SERIAL_FINAL="${ADB_SERIAL_ARG:-$ADB_SERIAL_ENV}"

adb_exec() {
	if [[ -n "${ADB_SERIAL_FINAL}" ]]; then
		adb -s "${ADB_SERIAL_FINAL}" "$@"
	else
		adb "$@"
	fi
}

if [[ -n "${ADB_SERIAL_FINAL}" ]]; then
	echo "[test_data_push] Using ADB serial: ${ADB_SERIAL_FINAL}"
else
	echo "[test_data_push] Using default adb target (no ADB_SERIAL specified)"
fi
echo "[test_data_push] Local root:  ${SCRIPT_DIR}"
echo "[test_data_push] Remote root: ${REMOTE_BASE}"

mapfile -d '' -t FILES < <(find "${SCRIPT_DIR}" -type f ! -path "${SCRIPT_DIR}/${SCRIPT_NAME}" -print0 | sort -z)

if [[ ${#FILES[@]} -eq 0 ]]; then
	echo "[test_data_push] No files to push (directory empty or only contains this script)."
	exit 0
fi

for f in "${FILES[@]}"; do
	rel="${f#"${SCRIPT_DIR}/"}"
	dest="${REMOTE_BASE}/${rel}"
	parent="$(dirname "${dest}")"
	echo "[test_data_push] ${rel} -> ${dest}"
	adb_exec shell mkdir -p "$(printf '%q' "${parent}")"
	adb_exec push "${f}" "${dest}"
done

echo "[test_data_push] Done. Pushed ${#FILES[@]} file(s) under ${REMOTE_BASE}/"
