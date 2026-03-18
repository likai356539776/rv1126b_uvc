#!/bin/sh
set -eu

FORMAT="H.264"
WIDTH="640"
HEIGHT="480"
FPS="25"
GADGET_DIR="/sys/kernel/config/usb_gadget/rockchip"
FUNC_NAME="uvc.gs1"
VERBOSE=0
DO_UNBIND=1

usage() {
	echo "Usage: $0 [-w width] [-h height] [-p fps] [--verbose] [--no-unbind]"
	echo "Example: $0 -w 640 -h 480"
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

while [ $# -gt 0 ]; do
	case "$1" in
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
	--verbose)
		VERBOSE=1
		shift
		;;
	--no-unbind)
		DO_UNBIND=0
		shift
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

if [ "$FORMAT" != "H.264" ]; then
	echo "Only H.264 is supported in v1"
	exit 1
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
	rm -rf "$GADGET_DIR/functions/$FUNC_NAME" || true
else
	mkdir -p "$GADGET_DIR"
fi

mkdir -p "$GADGET_DIR/strings/0x409"
mkdir -p "$GADGET_DIR/configs/b.1/strings/0x409"
mkdir -p "$GADGET_DIR/functions/$FUNC_NAME"

echo 0x2207 > "$GADGET_DIR/idVendor"
echo 0x0016 > "$GADGET_DIR/idProduct"
echo 0x0310 > "$GADGET_DIR/bcdDevice"
echo 0x0200 > "$GADGET_DIR/bcdUSB"
echo "myuvc0001" > "$GADGET_DIR/strings/0x409/serialnumber"
echo "rockchip" > "$GADGET_DIR/strings/0x409/manufacturer"
echo "my_uvc" > "$GADGET_DIR/strings/0x409/product"
echo 500 > "$GADGET_DIR/configs/b.1/MaxPower"

echo "my_uvc" > "$GADGET_DIR/functions/$FUNC_NAME/device_name"
echo "my_uvc" > "$GADGET_DIR/functions/$FUNC_NAME/function_name"
echo 3072 > "$GADGET_DIR/functions/$FUNC_NAME/streaming_maxpacket"
echo 2 > "$GADGET_DIR/functions/$FUNC_NAME/uvc_num_request"

mkdir -p "$GADGET_DIR/functions/$FUNC_NAME/control/header/h"
ln -sf "$GADGET_DIR/functions/$FUNC_NAME/control/header/h" \
	"$GADGET_DIR/functions/$FUNC_NAME/control/class/fs/h"
ln -sf "$GADGET_DIR/functions/$FUNC_NAME/control/header/h" \
	"$GADGET_DIR/functions/$FUNC_NAME/control/class/ss/h"

mkdir -p "$GADGET_DIR/functions/$FUNC_NAME/streaming/header/h"
mkdir -p "$GADGET_DIR/functions/$FUNC_NAME/streaming/framebased/f1"
RES_DIR="$GADGET_DIR/functions/$FUNC_NAME/streaming/framebased/f1/${WIDTH}_${HEIGHT}p"
mkdir -p "$RES_DIR"

DEFAULT_INTERVAL="$(fps_to_interval "$FPS")"
logv "fps=${FPS}, default interval=${DEFAULT_INTERVAL}"

echo "$WIDTH" > "$RES_DIR/wWidth"
echo "$HEIGHT" > "$RES_DIR/wHeight"
echo "$DEFAULT_INTERVAL" > "$RES_DIR/dwDefaultFrameInterval"
echo $((WIDTH * HEIGHT * 10)) > "$RES_DIR/dwMinBitRate"
echo $((WIDTH * HEIGHT * 10)) > "$RES_DIR/dwMaxBitRate"
# For stable host negotiation, expose a single interval matching selected FPS.
echo "$DEFAULT_INTERVAL" > "$RES_DIR/dwFrameInterval"
echo -ne '\x48\x32\x36\x34\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71' > \
	"$GADGET_DIR/functions/$FUNC_NAME/streaming/framebased/f1/guidFormat"

ln -sf "$GADGET_DIR/functions/$FUNC_NAME/streaming/framebased/f1" \
	"$GADGET_DIR/functions/$FUNC_NAME/streaming/header/h/f1"
ln -sf "$GADGET_DIR/functions/$FUNC_NAME/streaming/header/h" \
	"$GADGET_DIR/functions/$FUNC_NAME/streaming/class/fs/h"
ln -sf "$GADGET_DIR/functions/$FUNC_NAME/streaming/header/h" \
	"$GADGET_DIR/functions/$FUNC_NAME/streaming/class/hs/h"
ln -sf "$GADGET_DIR/functions/$FUNC_NAME/streaming/header/h" \
	"$GADGET_DIR/functions/$FUNC_NAME/streaming/class/ss/h"

ln -sf "$GADGET_DIR/functions/$FUNC_NAME" "$GADGET_DIR/configs/b.1/f1"

UDC="$(ls /sys/class/udc | head -n 1)"
if [ -z "$UDC" ]; then
	echo "No UDC found"
	exit 1
fi
logv "bind UDC: ${UDC}"
echo "$UDC" > "$GADGET_DIR/UDC"
FINAL_UDC="$(cat "$GADGET_DIR/UDC" 2>/dev/null || true)"
logv "final UDC state: '${FINAL_UDC}'"

echo "Configured UVC H.264 ${WIDTH}x${HEIGHT}@${FPS}fps on UDC=${UDC}"
