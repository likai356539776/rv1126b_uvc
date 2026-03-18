#!/bin/bash
set -euo pipefail

# -----------------------------------------------------------------------------
# my_uvc 一键部署脚本
# 用途:
#   1) 将当前工程产物部署到目标板
#   2) 设置执行权限和配置文件权限
#
# 约定:
#   - 可执行程序部署到: /usr/bin
#   - 配置文件部署到:   /userdata
#
# 后续如新增产物(例如多路配置文件/额外脚本/资源文件)，请在
# "Deploy artifacts" 区域追加对应 adb push 即可。
# -----------------------------------------------------------------------------

# Optional target device serial.
# Priority:
#   1) --adb-serial <serial> argument
#   2) ADB_SERIAL environment variable
# If neither is provided, adb runs without -s (default behavior).
ADB_SERIAL_ENV="${ADB_SERIAL:-}"
ADB_SERIAL_ARG=""
while [[ $# -gt 0 ]]; do
	case "$1" in
	--adb-serial)
		[[ $# -ge 2 ]] || { echo "Missing value for --adb-serial"; exit 1; }
		ADB_SERIAL_ARG="$2"
		shift 2
		;;
	-h|--help)
		echo "Usage: $0 [--adb-serial <serial>]"
		exit 0
		;;
	*)
		echo "Unknown option: $1"
		echo "Usage: $0 [--adb-serial <serial>]"
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
	echo "[my_uvc_install] Using ADB serial: ${ADB_SERIAL_FINAL}"
else
	echo "[my_uvc_install] Using default adb target (no ADB_SERIAL specified)"
fi

# Deploy artifacts: executable + usb config script + runtime config.
adb_exec push build-rv1126b/my_uvc /usr/bin/
adb_exec push scripts/my_uvc_usb_config.sh /usr/bin/
adb_exec push config/my_uvc.ini /userdata/

# Set permissions on target board.
adb_exec shell chmod +x /usr/bin/my_uvc
adb_exec shell chmod +x /usr/bin/my_uvc_usb_config.sh
adb_exec shell chmod 666 /userdata/my_uvc.ini

echo "[my_uvc_install] Deploy finished."

# Quick sanity checks on target board.
echo "[my_uvc_install] Verify binary and script versions on target:"
adb_exec shell "/usr/bin/my_uvc --help || true"
adb_exec shell "/usr/bin/my_uvc_usb_config.sh --help || true"
