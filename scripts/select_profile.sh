#!/usr/bin/env bash
set -euo pipefail

# ------------------------------------------------------------
# select_profile.sh
# 作用：
#   按产品路数快速选择并下发配置模板（1/2/4 路独立配置）。
#
# 默认行为：
#   仅打印将使用的 profile 与建议启动命令，不执行部署。
#
# 常用示例：
#   ./scripts/select_profile.sh 4 --install
#   ./scripts/select_profile.sh --profile 2 --install --adb-serial <serial>
#   ./scripts/select_profile.sh 4 --install --run
#   ./scripts/select_profile.sh 4 --install --run --fps 20
#   ./scripts/select_profile.sh 4 --install --run --size 1280x720
#   ./scripts/select_profile.sh 16 --install --run --fps 15
#   ./scripts/select_profile.sh 1
# ------------------------------------------------------------

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
INSTALL_SCRIPT="${PROJECT_DIR}/my_uvc_install_to_device.sh"
REMOTE_CONFIG_PATH="/userdata/my_uvc.ini"
PROFILE=""
DO_INSTALL=0
DO_RUN=0
ADB_SERIAL_VALUE=""
USB_FPS=25
USB_WIDTH=640
USB_HEIGHT=480

print_usage() {
	echo "Usage: $0 [1|2|4|6|8|10|12|16] [--profile 1|2|4|6|8|10|12|16] [--install] [--run] [--fps 5|10|15|20|25|30] [--size WxH] [--adb-serial <serial>] [--remote-config <path>] [--help]"
	echo "  profile              Select independent channel profile"
	echo "  --install           Deploy selected profile via my_uvc_install_to_device.sh"
	echo "  --run               Run board startup flow after selection/deploy"
	echo "  --fps               USB config fps used with --run (default: 25)"
	echo "  --size              USB config resolution used with --run, e.g. 640x480"
	echo "  --adb-serial        Optional adb serial passed to install script"
	echo "  --remote-config     Target path on board (default: /userdata/my_uvc.ini)"
}

adb_exec() {
	if [[ -n "${ADB_SERIAL_VALUE}" ]]; then
		adb -s "${ADB_SERIAL_VALUE}" "$@"
	else
		adb "$@"
	fi
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	1|2|4|6|8|10|12|16)
		PROFILE="$1"
		shift
		;;
	--profile)
		[[ $# -ge 2 ]] || { echo "Missing value for --profile"; exit 1; }
		PROFILE="$2"
		shift 2
		;;
	--install)
		DO_INSTALL=1
		shift
		;;
	--run)
		DO_RUN=1
		shift
		;;
	--fps)
		[[ $# -ge 2 ]] || { echo "Missing value for --fps"; exit 1; }
		USB_FPS="$2"
		shift 2
		;;
	--size)
		[[ $# -ge 2 ]] || { echo "Missing value for --size"; exit 1; }
		SIZE_VAL="$2"
		if [[ ! "${SIZE_VAL}" =~ ^([0-9]+)x([0-9]+)$ ]]; then
			echo "Invalid --size format: ${SIZE_VAL}. Use WxH, e.g. 640x480"
			exit 1
		fi
		USB_WIDTH="${BASH_REMATCH[1]}"
		USB_HEIGHT="${BASH_REMATCH[2]}"
		shift 2
		;;
	--adb-serial)
		[[ $# -ge 2 ]] || { echo "Missing value for --adb-serial"; exit 1; }
		ADB_SERIAL_VALUE="$2"
		shift 2
		;;
	--remote-config)
		[[ $# -ge 2 ]] || { echo "Missing value for --remote-config"; exit 1; }
		REMOTE_CONFIG_PATH="$2"
		shift 2
		;;
	-h|--help)
		print_usage
		exit 0
		;;
	*)
		echo "Unknown option: $1"
		print_usage
		exit 1
		;;
	esac
done

if [[ -z "${PROFILE}" ]]; then
	echo "Missing profile. Please choose 1/2/4/6/8/10/12/16."
	print_usage
	exit 1
fi

case "${USB_FPS}" in
5|10|15|20|25|30) ;;
*)
	echo "Invalid --fps value: ${USB_FPS}. Allowed: 5,10,15,20,25,30"
	exit 1
	;;
esac

if [[ "${USB_WIDTH}" -le 0 || "${USB_HEIGHT}" -le 0 ]]; then
	echo "Invalid --size value: ${USB_WIDTH}x${USB_HEIGHT}"
	exit 1
fi

case "${PROFILE}" in
1)
	LOCAL_CONFIG_PATH="${PROJECT_DIR}/config/profiles/my_uvc_1ch_independent.ini"
	USB_CHANNELS=1
	;;
2)
	LOCAL_CONFIG_PATH="${PROJECT_DIR}/config/profiles/my_uvc_2ch_independent.ini"
	USB_CHANNELS=2
	;;
4)
	LOCAL_CONFIG_PATH="${PROJECT_DIR}/config/profiles/my_uvc_4ch_independent.ini"
	USB_CHANNELS=4
	;;
6)
	LOCAL_CONFIG_PATH="${PROJECT_DIR}/config/profiles/my_uvc_6ch_independent.ini"
	USB_CHANNELS=6
	;;
8)
	LOCAL_CONFIG_PATH="${PROJECT_DIR}/config/profiles/my_uvc_8ch_independent.ini"
	USB_CHANNELS=8
	;;
10)
	LOCAL_CONFIG_PATH="${PROJECT_DIR}/config/profiles/my_uvc_10ch_independent.ini"
	USB_CHANNELS=10
	;;
12)
	LOCAL_CONFIG_PATH="${PROJECT_DIR}/config/profiles/my_uvc_12ch_independent.ini"
	USB_CHANNELS=12
	;;
16)
	LOCAL_CONFIG_PATH="${PROJECT_DIR}/config/profiles/my_uvc_16ch_independent.ini"
	USB_CHANNELS=16
	;;
*)
	echo "Invalid profile: ${PROFILE}. Only 1/2/4/6/8/10/12/16 are supported."
	exit 1
	;;
esac

if [[ ! -f "${LOCAL_CONFIG_PATH}" ]]; then
	echo "Profile file not found: ${LOCAL_CONFIG_PATH}"
	exit 1
fi

echo "[select_profile] Selected profile: ${PROFILE}ch"
echo "[select_profile] Local config: ${LOCAL_CONFIG_PATH}"
echo "[select_profile] Remote config: ${REMOTE_CONFIG_PATH}"
echo "[select_profile] USB fps: ${USB_FPS}"
echo "[select_profile] USB size: ${USB_WIDTH}x${USB_HEIGHT}"
echo "[select_profile] Suggested board commands:"
echo "  my_uvc_usb_config.sh -w ${USB_WIDTH} -h ${USB_HEIGHT} -p ${USB_FPS} -n ${USB_CHANNELS} --verbose"
echo "  my_uvc -c ${REMOTE_CONFIG_PATH} --size ${USB_WIDTH}x${USB_HEIGHT}"
echo "[select_profile] Suggested host commands:"
echo "  v4l2-ctl --list-devices"
for ((i=0; i<USB_CHANNELS; i++)); do
	echo "  ffplay -f v4l2 -input_format h264 -video_size ${USB_WIDTH}x${USB_HEIGHT} /dev/videoX   # channel ${i}"
done

if [[ ${DO_INSTALL} -eq 1 ]]; then
	if [[ ! -x "${INSTALL_SCRIPT}" ]]; then
		chmod +x "${INSTALL_SCRIPT}"
	fi
	INSTALL_ARGS=(--config "${LOCAL_CONFIG_PATH}" --remote-config "${REMOTE_CONFIG_PATH}")
	if [[ -n "${ADB_SERIAL_VALUE}" ]]; then
		INSTALL_ARGS+=(--adb-serial "${ADB_SERIAL_VALUE}")
	fi
	echo "[select_profile] Running install script..."
	"${INSTALL_SCRIPT}" "${INSTALL_ARGS[@]}"
	echo "[select_profile] Install done."
fi

if [[ ${DO_RUN} -eq 1 ]]; then
	echo "[select_profile] Running board startup flow..."
	adb_exec shell "/usr/bin/my_uvc_usb_config.sh -w ${USB_WIDTH} -h ${USB_HEIGHT} -p ${USB_FPS} -n ${USB_CHANNELS} --verbose"
	adb_exec shell "pkill -f '/usr/bin/my_uvc -c ${REMOTE_CONFIG_PATH}' || true"
	adb_exec shell "nohup /usr/bin/my_uvc -c ${REMOTE_CONFIG_PATH} --size ${USB_WIDTH}x${USB_HEIGHT} >/userdata/my_uvc.log 2>&1 &"
	echo "[select_profile] Board startup done."
	echo "[select_profile] Check board log: adb shell tail -f /userdata/my_uvc.log"
fi

