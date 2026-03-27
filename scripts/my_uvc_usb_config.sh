#!/bin/sh
set -eu

FORMAT="H.264"
WIDTH="640"
HEIGHT="480"
FPS="25"
CHANNELS="1"
GADGET_DIR="/sys/kernel/config/usb_gadget/rockchip"
VERBOSE=0
DO_UNBIND=1
STOP_SYSTEM_USB=0
STREAMING_MAXPACKET=""
STREAMING_INTERVAL=""
MJPEG_MAX_FRAME_SIZE=""

usage() {
	echo "Usage: $0 [-f H.264|MJPEG] [-w width] [-h height] [-p fps] [-n channels] [--verbose] [--no-unbind] [--stop-system-usb] [--streaming-maxpacket n] [--streaming-interval n] [--mjpeg-max-frame-size bytes]"
	echo "Example: $0 -f H.264 -w 640 -h 480"
	echo "  -f: UVC payload format (same layout as rkipc rkipc_usb_config.sh)"
	echo "  --streaming-maxpacket: override per-UVC function streaming_maxpacket"
	echo "  --streaming-interval: override per-UVC function streaming_interval"
	echo "  --mjpeg-max-frame-size: override MJPEG dwMaxVideoFrameBufferSize (bytes)"
}

logv() {
	if [ "$VERBOSE" -eq 1 ]; then
		echo "[my_uvc_usb_config] $*"
	fi
}

fps_to_interval() {
	case "$1" in
	30) echo 333333 ;;
	25) echo 400000 ;;
	20) echo 500000 ;;
	15) echo 666666 ;;
	10) echo 1000000 ;;
	5)  echo 2000000 ;;
	*)
		echo "Unsupported fps: $1 (supported: 5/10/15/20/25/30)"
		exit 1
		;;
	esac
}

validate_channels() {
	case "$1" in
	1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16) ;;
	*)
		echo "Unsupported channels: $1 (supported: 1..16)"
		exit 1
		;;
	esac
}

calc_mjpeg_max_frame_size() {
	_base=$((WIDTH * HEIGHT))
	_den=2

	# Multi-channel strategy:
	# more channels / higher fps -> smaller declared max frame size
	# to avoid host-side bandwidth over-reservation.
	if [ "$CHANNELS" -le 1 ]; then
		_den=2
	elif [ "$CHANNELS" -le 2 ]; then
		_den=3
	elif [ "$CHANNELS" -le 4 ]; then
		_den=4
	elif [ "$CHANNELS" -le 6 ]; then
		_den=5
	else
		_den=6
	fi

	if [ "$FPS" -ge 30 ]; then
		_den=$((_den + 1))
	fi

	_size=$((_base / _den))
	_max=$((_base / 2))
	_min=$((_base / 10))

	# Hard guard rails for stability and compatibility.
	if [ "$_min" -lt 32768 ]; then
		_min=32768
	fi
	if [ "$_size" -lt "$_min" ]; then
		_size=$_min
	fi
	if [ "$_size" -gt "$_max" ]; then
		_size=$_max
	fi

	# Align to 1KB boundary for cleaner descriptor values.
	_size=$(((_size + 1023) / 1024 * 1024))
	echo "$_size"
}

while [ $# -gt 0 ]; do
	case "$1" in
	-f)
		[ $# -ge 2 ] || { usage; exit 1; }
		FORMAT="$2"
		shift 2
		;;
	-w)
		[ $# -ge 2 ] || { usage; exit 1; }
		WIDTH="$2"
		shift 2
		;;
	-h)
		[ $# -ge 2 ] || { usage; exit 1; }
		HEIGHT="$2"
		shift 2
		;;
	-p|--fps)
		[ $# -ge 2 ] || { usage; exit 1; }
		FPS="$2"
		shift 2
		;;
	-n|--channels)
		[ $# -ge 2 ] || { usage; exit 1; }
		CHANNELS="$2"
		shift 2
		;;
	--verbose)
		VERBOSE=1
		shift
		;;
	--no-unbind)
		DO_UNBIND=0
		shift
		;;
	--stop-system-usb)
		STOP_SYSTEM_USB=1
		shift
		;;
	--streaming-maxpacket)
		[ $# -ge 2 ] || { usage; exit 1; }
		STREAMING_MAXPACKET="$2"
		shift 2
		;;
	--streaming-interval)
		[ $# -ge 2 ] || { usage; exit 1; }
		STREAMING_INTERVAL="$2"
		shift 2
		;;
	--mjpeg-max-frame-size)
		[ $# -ge 2 ] || { usage; exit 1; }
		MJPEG_MAX_FRAME_SIZE="$2"
		shift 2
		;;
	--help|-help|-?)
		usage
		exit 0
		;;
	*)
		echo "Unknown option: $1"
		usage
		exit 1
		;;
	esac
done

case "$FORMAT" in
H.264|MJPEG) ;;
*)
	echo "Unsupported -f FORMAT: ${FORMAT} (use H.264 or MJPEG, same as rkipc_usb_config.sh)"
	exit 1
	;;
esac
validate_channels "$CHANNELS"

if [ "$STOP_SYSTEM_USB" -eq 1 ]; then
	if [ -x /usr/bin/usbdevice ]; then
		logv "stopping system usb manager: /usr/bin/usbdevice stop"
		/usr/bin/usbdevice stop || true
	fi
fi

mkdir -p /sys/kernel/config
mountpoint -q /sys/kernel/config || mount -t configfs none /sys/kernel/config
logv "configfs ready"

if [ -d "$GADGET_DIR" ]; then
	if [ "$DO_UNBIND" -eq 1 ] && [ -f "$GADGET_DIR/UDC" ]; then
		CUR_UDC="$(cat "$GADGET_DIR/UDC" 2>/dev/null || true)"
		logv "current UDC before unbind: '${CUR_UDC}'"
		# Default behavior: always try unbind first.
		# Some kernels may return ENODEV; treat as non-fatal and continue.
		echo "" > "$GADGET_DIR/UDC" 2>/dev/null || true
		AFTER_UNBIND_UDC="$(cat "$GADGET_DIR/UDC" 2>/dev/null || true)"
		logv "UDC after unbind attempt: '${AFTER_UNBIND_UDC}'"
	fi
	for f in "$GADGET_DIR/configs/b.1"/f*; do
		[ -L "$f" ] && rm -f "$f"
	done
	rm -rf "$GADGET_DIR/functions/uvc.gs"* || true
else
	mkdir -p "$GADGET_DIR"
fi

mkdir -p "$GADGET_DIR/strings/0x409"
mkdir -p "$GADGET_DIR/configs/b.1/strings/0x409"
mkdir -p "$GADGET_DIR/functions"

echo 0x2207 > "$GADGET_DIR/idVendor"
echo 0x0016 > "$GADGET_DIR/idProduct"
echo 0x0310 > "$GADGET_DIR/bcdDevice"
echo 0x0200 > "$GADGET_DIR/bcdUSB"
echo "myuvc0001" > "$GADGET_DIR/strings/0x409/serialnumber"
echo "rockchip" > "$GADGET_DIR/strings/0x409/manufacturer"
echo "my_uvc" > "$GADGET_DIR/strings/0x409/product"
echo 500 > "$GADGET_DIR/configs/b.1/MaxPower"

DEFAULT_INTERVAL="$(fps_to_interval "$FPS")"
if [ -n "$MJPEG_MAX_FRAME_SIZE" ]; then
	MJPEG_DECLARED_MAX="$MJPEG_MAX_FRAME_SIZE"
else
	MJPEG_DECLARED_MAX="$(calc_mjpeg_max_frame_size)"
fi
logv "fps=${FPS}, default interval=${DEFAULT_INTERVAL}, channels=${CHANNELS}, mjpeg_max_frame=${MJPEG_DECLARED_MAX}"

configure_one_uvc() {
	_idx="$1"
	_func="uvc.gs${_idx}"
	_name="my_uvc_${_idx}"
	_func_dir="$GADGET_DIR/functions/${_func}"

	mkdir -p "${_func_dir}"
	echo "${_name}" > "${_func_dir}/device_name"
	echo "${_name}" > "${_func_dir}/function_name"
	if [ -n "$STREAMING_MAXPACKET" ]; then
		echo "$STREAMING_MAXPACKET" > "${_func_dir}/streaming_maxpacket"
	elif [ "$CHANNELS" -gt 1 ]; then
		echo 2048 > "${_func_dir}/streaming_maxpacket"
	else
		echo 3072 > "${_func_dir}/streaming_maxpacket"
	fi
	if [ -n "$STREAMING_INTERVAL" ]; then
		echo "$STREAMING_INTERVAL" > "${_func_dir}/streaming_interval"
	fi
	echo 2 > "${_func_dir}/uvc_num_request"

	mkdir -p "${_func_dir}/control/header/h"
	ln -sf "${_func_dir}/control/header/h" "${_func_dir}/control/class/fs/h"
	ln -sf "${_func_dir}/control/header/h" "${_func_dir}/control/class/ss/h"

	mkdir -p "${_func_dir}/streaming/header/h"
	if [ "$FORMAT" = "MJPEG" ]; then
		# Rockchip rkipc_usb_config.sh: streaming/mjpeg/m/<WxH>p
		_res_dir="${_func_dir}/streaming/mjpeg/m/${WIDTH}_${HEIGHT}p"
		mkdir -p "${_func_dir}/streaming/mjpeg/m"
		mkdir -p "${_res_dir}"
		echo "$WIDTH" > "${_res_dir}/wWidth"
		echo "$HEIGHT" > "${_res_dir}/wHeight"
		echo "$DEFAULT_INTERVAL" > "${_res_dir}/dwDefaultFrameInterval"
		echo $((WIDTH * HEIGHT * 10)) > "${_res_dir}/dwMinBitRate"
		echo $((WIDTH * HEIGHT * 10)) > "${_res_dir}/dwMaxBitRate"
		echo "${MJPEG_DECLARED_MAX}" > "${_res_dir}/dwMaxVideoFrameBufferSize"
		# Keep a single discrete interval to avoid host fallback to 5fps.
		# This follows the stable negotiation strategy used in earlier working versions.
		echo "$DEFAULT_INTERVAL" > "${_res_dir}/dwFrameInterval"
		ln -sf "${_func_dir}/streaming/mjpeg/m" "${_func_dir}/streaming/header/h/m"
	else
		_res_dir="${_func_dir}/streaming/framebased/f1/${WIDTH}_${HEIGHT}p"
		mkdir -p "${_func_dir}/streaming/framebased/f1"
		mkdir -p "${_res_dir}"
		echo "$WIDTH" > "${_res_dir}/wWidth"
		echo "$HEIGHT" > "${_res_dir}/wHeight"
		echo "$DEFAULT_INTERVAL" > "${_res_dir}/dwDefaultFrameInterval"
		echo $((WIDTH * HEIGHT * 10)) > "${_res_dir}/dwMinBitRate"
		echo $((WIDTH * HEIGHT * 10)) > "${_res_dir}/dwMaxBitRate"
		printf '%s\n' 333333 400000 500000 666666 1000000 2000000 > "${_res_dir}/dwFrameInterval"
		echo -ne '\x48\x32\x36\x34\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71' > \
			"${_func_dir}/streaming/framebased/f1/guidFormat"
		ln -sf "${_func_dir}/streaming/framebased/f1" "${_func_dir}/streaming/header/h/f1"
	fi
	ln -sf "${_func_dir}/streaming/header/h" "${_func_dir}/streaming/class/fs/h"
	ln -sf "${_func_dir}/streaming/header/h" "${_func_dir}/streaming/class/hs/h"
	ln -sf "${_func_dir}/streaming/header/h" "${_func_dir}/streaming/class/ss/h"

	ln -sf "${_func_dir}" "$GADGET_DIR/configs/b.1/f${_idx}"
}

_i=1
while [ "$_i" -le "$CHANNELS" ]; do
	configure_one_uvc "$_i"
	_i=$((_i + 1))
done

UDC="$(ls /sys/class/udc | head -n 1)"
if [ -z "$UDC" ]; then
	echo "No UDC found"
	exit 1
fi
logv "bind UDC: ${UDC}"
echo "$UDC" > "$GADGET_DIR/UDC"
FINAL_UDC="$(cat "$GADGET_DIR/UDC" 2>/dev/null || true)"
logv "final UDC state: '${FINAL_UDC}'"

echo "Configured UVC ${FORMAT} ${WIDTH}x${HEIGHT}@${FPS}fps channels=${CHANNELS} on UDC=${UDC}"
