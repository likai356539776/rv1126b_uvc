#!/bin/bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"

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
# 默认推送拆分配置: libmy_uvc.ini, libmy_uvc_pip.ini, uvctest.ini（见 config/README_CONFIG.md）
# 后续如新增产物，请在 "Deploy artifacts" 区域追加 adb push。
# -----------------------------------------------------------------------------

# Optional target device serial.
# Priority:
#   1) --adb-serial <serial> argument
#   2) ADB_SERIAL environment variable
# If neither is provided, adb runs without -s (default behavior).
ADB_SERIAL_ENV="${ADB_SERIAL:-}"
ADB_SERIAL_ARG=""
LOCAL_CONFIG_DIR="${PROJECT_DIR}/config"
DEPLOY_SINGLE_INI=0
LOCAL_CONFIG_PATH="${LOCAL_CONFIG_DIR}/libmy_uvc.ini"
REMOTE_CONFIG_PATH="/userdata/profile.ini"
while [[ $# -gt 0 ]]; do
	case "$1" in
	--adb-serial)
		[[ $# -ge 2 ]] || { echo "Missing value for --adb-serial"; exit 1; }
		ADB_SERIAL_ARG="$2"
		shift 2
		;;
	--config-dir)
		[[ $# -ge 2 ]] || { echo "Missing value for --config-dir"; exit 1; }
		LOCAL_CONFIG_DIR="$2"
		shift 2
		;;
	--config)
		[[ $# -ge 2 ]] || { echo "Missing value for --config"; exit 1; }
		LOCAL_CONFIG_PATH="$2"
		DEPLOY_SINGLE_INI=1
		shift 2
		;;
	--remote-config)
		[[ $# -ge 2 ]] || { echo "Missing value for --remote-config"; exit 1; }
		REMOTE_CONFIG_PATH="$2"
		shift 2
		;;
	-h|--help)
		echo "Usage: $0 [--adb-serial <serial>] [--config-dir <dir>] [--config <local_ini> --remote-config <remote>]"
		echo "  Default: push split configs from config/: libmy_uvc.ini, libmy_uvc_pip.ini, uvctest.ini -> /userdata/"
		echo "  --config-dir     Local directory containing ini files (default: <project>/config)"
		echo "  --config         Deploy a single local ini file (use with --remote-config)"
		echo "  --remote-config  Remote path when using --config (default: /userdata/profile.ini)"
		exit 0
		;;
	*)
		echo "Unknown option: $1"
		echo "Usage: $0 [--adb-serial <serial>] [--config-dir <dir>] [--config <local_ini> --remote-config <remote>]"
		exit 1
		;;
	esac
done

ADB_SERIAL_FINAL="${ADB_SERIAL_ARG:-$ADB_SERIAL_ENV}"

if [[ "${DEPLOY_SINGLE_INI}" -eq 1 ]]; then
	if [[ ! -f "${LOCAL_CONFIG_PATH}" ]]; then
		echo "[my_uvc_install] Config file not found: ${LOCAL_CONFIG_PATH}"
		exit 1
	fi
else
	if [[ ! -d "${LOCAL_CONFIG_DIR}" ]]; then
		echo "[my_uvc_install] Config directory not found: ${LOCAL_CONFIG_DIR}"
		exit 1
	fi
fi

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
if [[ "${DEPLOY_SINGLE_INI}" -eq 1 ]]; then
	echo "[my_uvc_install] Local config file: ${LOCAL_CONFIG_PATH}"
	echo "[my_uvc_install] Remote config file: ${REMOTE_CONFIG_PATH}"
else
	echo "[my_uvc_install] Local config dir: ${LOCAL_CONFIG_DIR} -> /userdata/*.ini"
fi

# Deploy artifacts: primary binary `uvctest`, shared lib, usb script, configs.
adb_exec push "${PROJECT_DIR}/build-rv1126b/uvc_main/uvctest" /usr/bin/
adb_exec push "${PROJECT_DIR}/build-rv1126b/libuvc/libmy_uvc.so.1.0.0" /usr/lib/
adb_exec shell ln -sf libmy_uvc.so.1.0.0 /usr/lib/libmy_uvc.so.1
adb_exec shell ln -sf libmy_uvc.so.1 /usr/lib/libmy_uvc.so
adb_exec push "${PROJECT_DIR}/scripts/my_uvc_usb_config.sh" /usr/bin/
if [[ "${DEPLOY_SINGLE_INI}" -eq 1 ]]; then
	adb_exec push "${LOCAL_CONFIG_PATH}" "${REMOTE_CONFIG_PATH}"
	adb_exec shell chmod 666 "${REMOTE_CONFIG_PATH}"
else
	for f in libmy_uvc.ini libmy_uvc_pip.ini uvctest.ini; do
		if [[ -f "${LOCAL_CONFIG_DIR}/${f}" ]]; then
			echo "[my_uvc_install] push ${f} -> /userdata/${f}"
			adb_exec push "${LOCAL_CONFIG_DIR}/${f}" "/userdata/${f}"
			adb_exec shell chmod 666 "/userdata/${f}"
		fi
	done
fi

adb_exec shell chmod +x /usr/bin/uvctest
adb_exec shell chmod +x /usr/bin/my_uvc_usb_config.sh

echo "[my_uvc_install] Deploy finished."

# Quick sanity checks on target board.
echo "[my_uvc_install] Verify binary and script versions on target:"
adb_exec shell "/usr/bin/uvctest --help || true"
adb_exec shell "/usr/bin/my_uvc_usb_config.sh --help || true"
